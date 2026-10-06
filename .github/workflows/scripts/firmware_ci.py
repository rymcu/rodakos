"""Linux CI orchestration for the existing Board Manager and signed OTA contracts."""

import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[3]
IDF_COMMIT = "7101770dc6db2667b3c477cc31365dd1acd6db4e"
REVIEWED_REGISTRY_URLS = ("https://components.espressif.com", "https://components.espressif.com/")
LAYOUT = {"app": (0x2A0000, 0xD50000), "recovery": (0x20000, 0x280000), "otadata": (0xF000, 0x2000)}
HOME_MARKER = b"RODAKOS_HOME_HARDWARE_TEST_POPULATION_ACTIVE"
FAULT_MARKER = b"RODAKOS_RELEASE_FAULT_INJECTION_ACTIVE"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(*args, cwd=ROOT, capture=False):
    command = [str(arg) for arg in args]
    print("+ " + " ".join(command), flush=True)
    result = subprocess.run(command, cwd=cwd, check=True, text=True,
                            stdout=subprocess.PIPE if capture else None)
    return result.stdout.strip() if capture else None


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def environment(idf):
    require(run("git", "-C", idf, "rev-parse", "HEAD", capture=True) == IDF_COMMIT,
            "Official image does not contain the reviewed ESP-IDF 6.0.2 commit")
    run("git", "-C", idf, "diff", "--exit-code", "HEAD")
    version_cmake = (idf / "tools/cmake/version.cmake").read_text()
    parts = [re.search(rf"set\(IDF_VERSION_{part}\s+(\d+)\)", version_cmake).group(1)
             for part in ("MAJOR", "MINOR", "PATCH")]
    require(".".join(parts) == "6.0.2", "ESP-IDF version differs")
    tools = json.loads((idf / "tools/tools.json").read_text())["tools"]
    tool = next(item for item in tools if item["name"] == "xtensa-esp-elf")
    recommended = [item["name"] for item in tool["versions"] if item["status"] == "recommended"]
    require(len(recommended) == 1, "Ambiguous recommended Xtensa toolchain")
    compiler = Path(shutil.which(tool["version_cmd"][0]) or "missing").resolve()
    expected = Path(os.environ["IDF_TOOLS_PATH"]) / "tools/xtensa-esp-elf" / recommended[0]
    require(compiler.is_relative_to(expected.resolve()), "Compiler is outside the recommended IDF tool directory")
    output = run(*tool["version_cmd"], capture=True)
    match = re.search(tool["version_regex"], output)
    require(match is not None and match.group(1) == recommended[0], "Xtensa compiler version differs")
    return {"idfCommit": IDF_COMMIT, "idfVersion": "6.0.2", "compiler": str(compiler),
            "compilerVersion": output, "recommendedToolchain": recommended[0],
            "sourceCommit": run("git", "rev-parse", "HEAD", capture=True)}


def validate_registry_source(name, source):
    require(isinstance(source, dict) and source.get("registry_url") in REVIEWED_REGISTRY_URLS,
            f"Unreviewed component Registry: {name}")


def locked_component_constraints(original_lock):
    constraints = []
    for name, entry in sorted(original_lock["dependencies"].items()):
        if entry["source"]["type"] == "service":
            validate_registry_source(name, entry["source"])
            constraints.append(f"{name}=={entry['version']}")
    require(constraints, "No locked Registry versions to constrain")
    return "\n".join(constraints) + "\n"


def verify_dependency_graph(original_lock, current_lock, output):
    # Preserve diagnostics before rejecting drift, including failed cold resolutions.
    write_json(output / "source-lock.json", original_lock)
    write_json(output / "resolved-lock.json", current_lock)
    original = {k: v for k, v in original_lock.items() if k != "manifest_hash"}
    current = {k: v for k, v in current_lock.items() if k != "manifest_hash"}
    if original != current:
        difference = "\n".join(difflib.unified_diff(
            json.dumps(original, sort_keys=True, indent=2).splitlines(),
            json.dumps(current, sort_keys=True, indent=2).splitlines(),
            fromfile="source-lock", tofile="resolved-lock", lineterm="")) + "\n"
        (output / "dependency-diff.txt").write_text(difference, encoding="utf-8")
        print(difference, flush=True)
        raise RuntimeError("Dependency graph drifted; see dependency-diff.txt")


def locked_components(original_lock, *, download=True):
    from idf_component_tools.hash_tools.validate import validate_hash_eq_hashdir
    from idf_component_tools.manifest import SolvedComponent
    from idf_component_tools.sources.fetcher import ComponentFetcher
    for name, entry in original_lock["dependencies"].items():
        if entry["source"]["type"] != "service":
            continue
        validate_registry_source(name, entry["source"])
        component = SolvedComponent(name=name, **entry)
        fetcher = ComponentFetcher(component, ROOT / "managed_components")
        downloaded = fetcher.download() if download else fetcher.component_path
        require(downloaded is not None, f"Missing component download: {name}")
        validate_hash_eq_hashdir(downloaded, component.component_hash)
        print(f"Verified {name}@{component.version}: {component.component_hash}", flush=True)


def normalize_board_paths(root):
    # Same substitutions as fix_gen_paths.ps1; only generated files are changed.
    generated = root / "components/gen_bmgr_codes"
    board = (root / "components/brookesia_hal_boards").as_posix()
    managed = (root / "managed_components/espressif__brookesia_hal_boards").as_posix()
    for name in ("idf_component.yml", "CMakeLists.txt"):
        path = generated / name
        text = path.read_text(encoding="utf-8-sig")
        if name.endswith(".yml"):
            text = text.replace(managed, "../../components/brookesia_hal_boards")
            text = text.replace(board, "../brookesia_hal_boards")
        else:
            text = text.replace("../../managed_components/espressif__brookesia_hal_boards",
                                "../../components/brookesia_hal_boards")
            text = text.replace(managed, "${CMAKE_SOURCE_DIR}/components/brookesia_hal_boards")
            text = text.replace(board, "${CMAKE_SOURCE_DIR}/components/brookesia_hal_boards")
        require(root.as_posix() not in text and "espressif__brookesia_hal_boards" not in text,
                f"Generated Board Manager path is not portable: {name}")
        path.write_text(text, encoding="utf-8", newline="\n")
    require('Selected Board: rymcu_bigsmart' in (generated / "CMakeLists.txt").read_text(),
            "Generated Board Manager target is not BigSmart")


def normal_flavor(build, *, main):
    cache = (build / "CMakeCache.txt").read_text()
    options = ("RODAKOS_HOME_HARDWARE_TEST_POPULATION", "RODAKOS_RELEASE_TESTS") if main else ()
    for option in options:
        require(re.search(rf"^{option}:BOOL=OFF$", cache, re.M), f"Unexpected build flavor: {option}")
    require(re.search(r"^RODAK_OTA_FAULT_INJECTION_PHASE:STRING=$", cache, re.M),
            "Fault injection is not explicitly disabled")


def inspect_builds(idf, output):
    main, recovery = ROOT / "build", ROOT / "recovery/build"
    journal = (ROOT / "components/rodak_ota_state/include/rodak_ota_state.h").read_text()
    schema = re.search(r"kOtaJournalSchemaVersion\s*=\s*(\d+)", journal)
    require(schema is not None and schema.group(1) == "1", "Recovery journal ABI is no longer v1")
    projects = [json.loads((path / "project_description.json").read_text()) for path in (main, recovery)]
    flash = [json.loads((path / "flasher_args.json").read_text()) for path in (main, recovery)]
    require(all(project["target"] == "esp32s3" for project in projects), "Unexpected firmware target")
    require(flash[0]["flash_settings"] == flash[1]["flash_settings"], "Main/Recovery flash settings differ")
    require(flash[0]["flash_settings"]["flash_size"] == "16MB", "Flash size must be 16MB")
    for key, expected in (("bootloader", "0x0"), ("partition-table", "0x8000")):
        require(all(item[key]["offset"] == expected for item in flash), f"Wrong {key} offset")
    for build, project in zip((main, recovery), projects):
        normal_flavor(build, main=build == main)
        config = Path(project["config_file"]).read_text()
        require("CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y" in config, "Rollback support missing")
        (output / (project["project_name"] + "-sdkconfig.txt")).write_text(config)
    partition = recovery / "partition_table/partition-table.bin"
    require(digest(partition) == digest(main / "partition_table/partition-table.bin"), "Partition tables differ")
    for name, expected in LAYOUT.items():
        values = run(sys.executable, idf / "components/partition_table/parttool.py",
                     "--partition-table-file", partition, "get_partition_info", "--partition-name", name,
                     "--info", "offset", "size", capture=True).split()
        require(tuple(int(value, 16) for value in values) == expected, f"Wrong {name} partition layout")
    for subtype, image in (("ota_0", main / "rodakos.bin"), ("factory", recovery / "rodakos_recovery.bin")):
        run(sys.executable, idf / "components/partition_table/check_sizes.py", "partition",
            "--type", "app", "--subtype", subtype, partition, image)
        require(HOME_MARKER not in image.read_bytes() and FAULT_MARKER not in image.read_bytes(),
                "A hardware/fault-test binary entered the normal flavor")
    symbols = run("xtensa-esp32s3-elf-nm", "--defined-only", main / "rodakos.elf", capture=True)
    (output / "main-symbols.txt").write_text(symbols + "\n")
    addresses = {parts[-1]: int(parts[0], 16) for line in symbols.splitlines()
                 if len(parts := line.split()) == 3 and re.fullmatch(r"[0-9a-fA-F]+", parts[0])}
    for name in ("g_esp_board_devices", "g_esp_board_peripherals",
                 "_binary_rodakos_voice_models_start", "_binary_rodakos_voice_models_end"):
        require(name in addresses, f"Missing linked board/voice symbol: {name}")
    model = main / "srmodels/srmodels.bin"
    require(model.stat().st_size > 0 and addresses["_binary_rodakos_voice_models_end"] -
            addresses["_binary_rodakos_voice_models_start"] == model.stat().st_size,
            "Linked voice model bounds disagree with generated model bundle")
    commands = json.loads((main / "compile_commands.json").read_text())
    compiled = {Path(entry["file"]).resolve() for entry in commands}
    generated = ROOT / "components/gen_bmgr_codes"
    for name in ("gen_board_device_config.c", "gen_board_periph_config.c"):
        require((generated / name).resolve() in compiled, f"Generated board source was not compiled: {name}")
    setup = ROOT / "components/brookesia_hal_boards/boards/rymcu/rymcu_bigsmart/setup_device.c"
    require(setup.resolve() in compiled, "Local BigSmart setup_device.c was not compiled")
    overlays = {
        "esp_codec_dev/esp_codec_dev.c": "espressif__esp_codec_dev/esp_codec_dev.c",
        "esp_codec_dev/platform/audio_codec_data_i2s.c": "espressif__esp_codec_dev/platform/audio_codec_data_i2s.c",
        "esp_mqtt/mqtt_client.c": "espressif__mqtt/mqtt_client.c",
        "esp_websocket_client/esp_websocket_client.c": "espressif__esp_websocket_client/esp_websocket_client.c",
    }
    patch_evidence = {}
    for generated_name, original_name in overlays.items():
        generated_path = main / "rodak_patches" / generated_name
        original_path = ROOT / "managed_components" / original_name
        require(generated_path.resolve() in compiled and original_path.resolve() not in compiled,
                f"Reviewed overlay is not the actual compiled source: {generated_name}")
        patch_evidence[generated_name] = digest(generated_path)
    write_json(output / "compiled-overlays.json", patch_evidence)
    shutil.copytree(generated, output / "generated-board")
    for build, label in ((main, "main"), (recovery, "recovery")):
        for name in ("project_description.json", "flasher_args.json", "CMakeCache.txt"):
            shutil.copy2(build / name, output / f"{label}-{name}")
    return projects[0]["project_version"]


def metadata(path):
    return {"fileName": path.name, "fileSize": path.stat().st_size,
            "checksumType": "sha256", "checksumValue": digest(path)}


def verify_package(directory):
    directory = Path(directory).resolve()
    run(sys.executable, ROOT / "tools/ota_security.py", "verify-package", "--directory", directory)
    manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8-sig"))
    require(manifest["developmentPackage"] is True and manifest["buildFlavor"] == "production" and
            manifest["imageType"] == "app" and manifest["homeHardwareTestPopulation"] is False and
            manifest["releaseFaultInjection"] is False and manifest["protocolVersion"] == 2,
            "CI package must be development-signed normal flavor")
    for name, (offset, size) in LAYOUT.items():
        entry = manifest[{"app": "appPartition", "recovery": "recoveryPartition", "otadata": "otaDataPartition"}[name]]
        require(entry == {"label": name, "offset": hex(offset), "size": hex(size)}, "Flash schema layout differs")
    require(manifest["fileName"] == "rodakos.bin", "Unexpected OTA application name")
    require(manifest["bootloaderImage"].get("offset") == "0x0", "Bootloader offset differs")
    regions = (("bootloaderImage", "bootloader.bin", 0),
               ("recoveryImage", "rodakos_recovery.bin", LAYOUT["recovery"][0]),
               ("otaDataImage", "ota_data_initial.bin", LAYOUT["otadata"][0]))
    merged = directory / "rodakos_sd_recovery_merged.bin"
    require(manifest["firstFlashImage"] == metadata(merged) and merged.stat().st_size == 16 * 1024 * 1024,
            "Merged image identity or size differs")
    data = merged.read_bytes()
    for field, name, offset in regions:
        expected = metadata(directory / name)
        actual = {key: value for key, value in manifest[field].items() if key != "offset"}
        require(expected == actual, f"Package asset identity differs: {name}")
        image = (directory / name).read_bytes()
        require(data[offset:offset + len(image)] == image, f"Merged region differs: {name}")
    for name, offset in (("partition-table.bin", 0x8000), ("rodakos.bin", LAYOUT["app"][0])):
        image = (directory / name).read_bytes()
        require(data[offset:offset + len(image)] == image, f"Merged region differs: {name}")
    require(manifest["partitionTableChecksum"] == {
        "checksumType": "sha256", "checksumValue": digest(directory / "partition-table.bin")},
        "Partition-table identity differs")
    require((directory / "rodakos.bin").stat().st_size <= LAYOUT["app"][1] and
            (directory / "rodakos_recovery.bin").stat().st_size <= LAYOUT["recovery"][1] and
            (directory / "ota_data_initial.bin").stat().st_size == LAYOUT["otadata"][1], "Package capacity differs")
    for path in directory.rglob("*"):
        if path.is_file() and path.suffix in (".pem", ".key"):
            require(b"PRIVATE KEY" not in path.read_bytes(), "Private key entered a package")
    for name in ("rodakos.bin", "rodakos_recovery.bin"):
        data = (directory / name).read_bytes()
        require(HOME_MARKER not in data and FAULT_MARKER not in data, "Test marker in a normal-flavor package")
    return manifest


def package(output, keys, version, suffix, baseline=None):
    directory = output / ("development-" + suffix)
    directory.mkdir()
    recovery = ROOT / "recovery/build"
    assets = {"rodakos.bin": ROOT / "build/rodakos.bin",
              "rodakos_recovery.bin": recovery / "rodakos_recovery.bin",
              "bootloader.bin": recovery / "bootloader/bootloader.bin",
              "partition-table.bin": recovery / "partition_table/partition-table.bin",
              "ota_data_initial.bin": recovery / "ota_data_initial.bin"}
    if baseline is not None:
        verify_package(baseline)
        assets.update({name: baseline / name for name in assets if name != "rodakos.bin"})
    for name, source in assets.items():
        shutil.copy2(source, directory / name)
    shutil.copy2(keys / "ota-public.pem", directory / "ota-public.pem")
    merged = directory / "rodakos_sd_recovery_merged.bin"
    run(sys.executable, "-m", "esptool", "--chip", "esp32s3", "merge-bin",
        "--flash-mode", "keep", "--flash-freq", "keep", "--flash-size", "keep", "--pad-to-size", "16MB",
        "-o", merged, "0x0", directory / "bootloader.bin", "0x8000", directory / "partition-table.bin",
        "0xf000", directory / "ota_data_initial.bin", "0x20000", directory / "rodakos_recovery.bin",
        "0x2a0000", directory / "rodakos.bin")
    manifest = {"protocolVersion": 2, "manifestVersion": 2, "developmentPackage": True,
                "releaseFaultInjection": False, "otaJournalSchemaVersion": 1, "buildFlavor": "production",
                "homeHardwareTestPopulation": False, "imageType": "app", **metadata(directory / "rodakos.bin"),
                "bootloaderImage": {**metadata(directory / "bootloader.bin"), "offset": "0x0"},
                "recoveryImage": metadata(directory / "rodakos_recovery.bin"),
                "otaDataImage": metadata(directory / "ota_data_initial.bin"),
                "firstFlashImage": metadata(merged),
                "partitionTableChecksum": {"checksumType": "sha256", "checksumValue": digest(directory / "partition-table.bin")}}
    for name, (offset, size) in LAYOUT.items():
        manifest[{"app": "appPartition", "recovery": "recoveryPartition", "otadata": "otaDataPartition"}[name]] = {
            "label": name, "offset": hex(offset), "size": hex(size)}
    write_json(directory / "manifest.json", manifest)
    task = f"ci-{os.environ['GITHUB_RUN_ID']}-{os.environ['GITHUB_RUN_ATTEMPT']}-{suffix}"
    run(sys.executable, ROOT / "tools/ota_security.py", "sign", "--manifest", directory / "manifest.json",
        "--private-key", keys / "ota-private.pem", "--public-key", keys / "ota-public.pem",
        "--task", task, "--version", version, "--image", directory / "rodakos.bin")
    verified = verify_package(directory)
    if baseline is not None:
        for name in assets:
            if name != "rodakos.bin":
                require(digest(directory / name) == digest(baseline / name), f"Immutable asset changed: {name}")
    (directory / "CI-DEVELOPMENT-ONLY.txt").write_text(
        "Ephemeral CI trust root. Not a production release or an installed-device Recovery baseline.\n"
        "Never deploy automatically. Hardware and production-key gates remain open.\n")
    return directory, verified


def build():
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import rsa
    from ruamel.yaml import YAML
    require(os.environ.get("GITHUB_ACTIONS") == "true", "Build mode is restricted to an ephemeral Actions checkout")
    require(ROOT == Path.cwd().resolve(), "Run from the checked-out RodakOS root")
    for path in ("build", "recovery/build", "components/gen_bmgr_codes"):
        require(not (ROOT / path).exists(), f"Fresh build required; refusing existing {path}")
    idf = Path(os.environ["IDF_PATH"]).resolve()
    output = ROOT / "firmware-ci-results"
    output.mkdir(exist_ok=True)
    identity = environment(idf)
    identity["pythonPackages"] = run(sys.executable, "-m", "pip", "freeze", capture=True)
    write_json(output / "identity.json", identity)
    original_lock = YAML(typ="safe").load((ROOT / "dependencies.lock").read_text())
    constraints = output / "component-constraints.txt"
    constraints.write_text(locked_component_constraints(original_lock), encoding="utf-8")
    os.environ["IDF_COMPONENT_CONSTRAINT_FILES"] = str(constraints)
    os.environ.pop("IDF_COMPONENT_CONSTRAINTS", None)
    locked_components(original_lock)
    # Board Manager owns regeneration; saved tracked configs are evidence, never build inputs.
    for path, name in ((ROOT / "sdkconfig", "source-main-sdkconfig.txt"),
                       (ROOT / "recovery/sdkconfig", "source-recovery-sdkconfig.txt")):
        if path.exists():
            path.replace(output / name)
    os.environ["IDF_EXTRA_ACTIONS_PATH"] = str(ROOT / "components/esp_board_manager")
    os.environ["IDF_TARGET"] = "esp32s3"
    with tempfile.TemporaryDirectory(prefix="rodakos-ci-keys-", dir=os.environ["RUNNER_TEMP"]) as temporary:
        keys = Path(temporary)
        key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        private = keys / "ota-private.pem"
        with os.fdopen(os.open(private, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "wb") as secret:
            secret.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                           serialization.NoEncryption()))
        public = keys / "ota-public.pem"
        public.write_bytes(key.public_key().public_bytes(serialization.Encoding.PEM,
                                                        serialization.PublicFormat.SubjectPublicKeyInfo))
        for _ in range(2):
            run("idf.py", "bmgr", "-b", "rymcu_bigsmart", "-c",
                ROOT / "components/brookesia_hal_boards/boards/rymcu/rymcu_bigsmart")
            normalize_board_paths(ROOT)
        flags = [f"-DRODAK_OTA_PUBLIC_KEY={public}", "-DRODAK_OTA_FAULT_INJECTION_PHASE="]
        run("idf.py", *flags, "-DRODAKOS_HOME_HARDWARE_TEST_POPULATION=OFF", "-DRODAKOS_RELEASE_TESTS=OFF", "reconfigure")
        verify_dependency_graph(original_lock,
            YAML(typ="safe").load((ROOT / "dependencies.lock").read_text()), output)
        run("idf.py", "build")
        # Do not inject the main application's board extension into minimal Recovery.
        del os.environ["IDF_EXTRA_ACTIONS_PATH"]
        run("idf.py", "-C", "recovery", *flags, "build")
        current_lock = YAML(typ="safe").load((ROOT / "dependencies.lock").read_text())
        verify_dependency_graph(original_lock, current_lock, output)
        locked_components(current_lock, download=False)
        version = inspect_builds(idf, output)
        baseline, _ = package(output, keys, version, "baseline")
        _, manifest = package(output, keys, version, "refresh", baseline)
        write_json(output / "result.json", {"sourceCommit": identity["sourceCommit"], "version": version,
                   "developmentPackage": True, "immutableRecoveryPreserved": True,
                   "main": {"bytes": manifest["fileSize"], "sha256": manifest["checksumValue"]}})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("build")
    verify = sub.add_parser("verify-package")
    verify.add_argument("directory", type=Path)
    args = parser.parse_args()
    if args.command == "build":
        build()
    else:
        verify_package(args.directory)


if __name__ == "__main__":
    main()
