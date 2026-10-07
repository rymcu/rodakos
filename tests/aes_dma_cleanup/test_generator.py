"""Exercise source pins, minimal diff and exact CMake target replacement offline."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

parser = argparse.ArgumentParser()
parser.add_argument('--idf-path', required=True, type=Path)
args = parser.parse_args()
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('aes_patch', ROOT / 'tools/prepare_aes_dma_cleanup_patch.py')
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)
PINS = json.loads((patch.PATCH_DIR / 'provenance.json').read_text())


class GeneratorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.idf = self.base / 'idf'
        for name in PINS['idf_source_hashes_lf']:
            target = self.idf / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(args.idf_path / name, target)

    def test_exact_minimal_change_and_idempotence(self):
        out = patch.prepare(self.idf, self.base / 'generated')
        original = (self.idf / patch.SOURCE).read_bytes().replace(b'\r\n', b'\n')
        self.assertEqual(out.read_bytes(), original.decode().replace(patch.BEFORE, patch.AFTER, 1).encode())
        before = out.stat().st_mtime_ns
        patch.prepare(self.idf, out.parent)
        self.assertEqual(out.stat().st_mtime_ns, before)
        self.assertEqual(hashlib.sha256(out.read_bytes()).hexdigest(), PINS['generated_sha256_lf'])

    def test_every_pin_rejects_drift_before_output(self):
        for index, name in enumerate(PINS['idf_source_hashes_lf']):
            with self.subTest(name=name):
                path = self.idf / name
                original = path.read_bytes()
                path.write_bytes(original + b'\n/* unreviewed */\n')
                output = self.base / f'refused-{index}'
                with self.assertRaisesRegex(ValueError, 'Unreviewed AES'):
                    patch.prepare(self.idf, output)
                self.assertFalse(output.exists())
                path.write_bytes(original)

    def test_sdk_output_is_forbidden(self):
        with self.assertRaisesRegex(ValueError, 'outside the IDF'):
            patch.prepare(self.idf, self.idf / 'generated')

    def test_crlf_only_is_normalized(self):
        for name in PINS['idf_source_hashes_lf']:
            path = self.idf / name
            path.write_bytes(path.read_bytes().replace(b'\r\n', b'\n').replace(b'\n', b'\r\n'))
        patch.prepare(self.idf, self.base / 'generated')

    def configure(self, variant):
        source = self.base / ('cmake-' + variant)
        source.mkdir()
        original = (self.idf / patch.SOURCE).as_posix()
        target = f'add_library(tfpsacrypto STATIC "{original}")\nset_property(TARGET tfpsacrypto PROPERTY OUTPUT_NAME tfpsacrypto)'
        if variant == 'missing': target = ''
        if variant == 'imported': target = 'add_library(tfpsacrypto STATIC IMPORTED)'
        if variant == 'shared': target = f'add_library(tfpsacrypto SHARED "{original}")'
        if variant == 'duplicate': target += f'\ntarget_sources(tfpsacrypto PRIVATE "{original}")'
        if variant == 'wrong-source': target = f'add_library(tfpsacrypto STATIC "{(self.idf / "components/mbedtls/port/aes/esp_aes.c").as_posix()}")\nset_property(TARGET tfpsacrypto PROPERTY OUTPUT_NAME tfpsacrypto)'
        if variant == 'wrong-output': target += '\nset_property(TARGET tfpsacrypto PROPERTY OUTPUT_NAME other)'
        text = f'''cmake_minimum_required(VERSION 3.20)
project(aes_target_contract C)
set(PROJECT_DIR "{ROOT.as_posix()}")
function(idf_build_get_property result property)
    if(property STREQUAL "IDF_PATH")
        set(${{result}} "{self.idf.as_posix()}" PARENT_SCOPE)
    elseif(property STREQUAL "IDF_TARGET")
        set(${{result}} "{'esp32p4' if variant == 'wrong-chip' else 'esp32s3'}" PARENT_SCOPE)
    else()
        set(${{result}} "{sys.executable}" PARENT_SCOPE)
    endif()
endfunction()
{target}
include("{(ROOT / 'cmake/aes_dma_cleanup_patch.cmake').as_posix()}")
get_target_property(result tfpsacrypto SOURCES)
file(WRITE "${{CMAKE_BINARY_DIR}}/resolved-sources.txt" "${{result}}")
'''
        (source / 'CMakeLists.txt').write_text(text)
        result = subprocess.run(['cmake', '-S', str(source), '-B', str(source / 'build'), '-G', 'Ninja'], capture_output=True, text=True)
        return source, result

    def test_cmake_exact_target_and_source(self):
        source, result = self.configure('valid')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        resolved = (source / 'build/resolved-sources.txt').read_text()
        self.assertEqual(resolved, (source / 'build/rodak_patches/aes_dma_cleanup/esp_aes_dma_core.c').as_posix())

    def test_cmake_rejects_target_source_and_chip_drift(self):
        for variant in ('missing', 'imported', 'shared', 'duplicate', 'wrong-source', 'wrong-output', 'wrong-chip'):
            with self.subTest(variant=variant):
                source, result = self.configure(variant)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('AES', result.stdout + result.stderr)
                self.assertFalse((source / 'build/rodak_patches/aes_dma_cleanup/esp_aes_dma_core.c').exists())


unittest.main(argv=[sys.argv[0]])
