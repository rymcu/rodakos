#!/usr/bin/env python3
"""Observe bounded Camera/Home software paths after a separately verified flash.

Before running, the previous capture/monitor must have exited and released the port.
This tool opens it once, never resets or provisions, and never kills another owner.
Package/flash/boot inputs are operator-supplied context, not installed-image readback.
First-frame and close logs are software observations, not physical camera acceptance.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time

from capture_release_stability import Evidence
from run_serial_voice_test import Session, serial


CLOSE_MARKERS = (
    "CameraService: CloseStream: STREAMOFF complete",
    "CameraService: CloseStream: fd close complete",
    "CameraService: CloseStream: device release complete",
    "CameraService: Camera preview stopped:",
    "CameraApp: Destroy: preview stop complete",
    "CameraApp: Destroy: audio release complete",
)
HEALTH_FIELDS = {
    "MQTT": {"connected", "telemetry_queued", "stack_min_free", "internal_largest"},
    "Main": {"stack_min_free", "internal_free", "internal_largest"},
    "Voice": {"enabled", "listening", "supervisor_stack_min_free", "internal_free", "internal_largest"},
}


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def package_context(package, flash_record, boot_log):
    from capture_first_boot import validate_log

    package, flash_record, boot_log = package.resolve(), flash_record.resolve(), boot_log.resolve()
    manifest_path = package / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    if (manifest.get("buildFlavor") != "production"
            or manifest.get("releaseFaultInjection") is not False
            or manifest.get("homeHardwareTestPopulation") is not False):
        raise ValueError("Camera smoke requires ordinary production flavor with both test options OFF")
    for name in (manifest.get("fileName"), "rodakos_recovery.bin", "ota-public.pem"):
        if not isinstance(name, str) or Path(name).name != name or not (package / name).is_file():
            raise ValueError(f"Missing package file or invalid basename: {name}")
    for path in (flash_record, boot_log):
        if not path.is_file() or path.stat().st_size == 0:
            raise ValueError(f"Missing non-empty flash context: {path}")
    failure, missing = validate_log(boot_log.read_text(encoding="utf-8", errors="replace"))
    if failure or missing:
        raise ValueError(f"Supplied first-boot log did not validate: {failure or missing}")
    subprocess.run([sys.executable, str(Path(__file__).with_name("ota_security.py")),
                    "verify-package", "--directory", str(package)], check=True, capture_output=True, text=True)
    return {"package": str(package), "manifest_sha256": sha256(manifest_path),
            "task": manifest["taskNo"], "version": manifest["version"],
            "main_sha256": manifest["checksumValue"],
            "flash_record": str(flash_record), "flash_record_sha256": sha256(flash_record),
            "boot_log": str(boot_log), "boot_log_sha256": sha256(boot_log),
            "installed_image_verified": False, "binding_verified": False,
            "identity_boundary": "Operator-supplied verified-flash context; no installed-image readback or device binding verification"}


def close_evidence(entries, first_frame_uptime):
    observed = []
    for elapsed, line in entries:
        timestamp = re.search(r"\((\d+)\)", line)
        previous = observed[-1]["uptime"] if observed else first_frame_uptime
        if timestamp is None or int(timestamp[1]) < previous:
            continue
        if len(observed) < len(CLOSE_MARKERS) and CLOSE_MARKERS[len(observed)] in line:
            observed.append({"marker": CLOSE_MARKERS[len(observed)], "uptime": int(timestamp[1]),
                             "elapsed_seconds": elapsed})
    return observed


def health_evidence(entries, started, ended, after_uptime):
    evidence = Evidence()
    samples = {source: [] for source in HEALTH_FIELDS}
    failures = set()
    for elapsed, line in entries:
        evidence.accept(line, max(0, elapsed - started))
        match = re.search(r"\b(MQTT|Main|Voice) health:", line)
        if match is None:
            continue
        source = match[1]
        timestamp = re.search(r"\((\d+)\)", line)
        fields = Evidence._fields(line)
        if timestamp is None or not HEALTH_FIELDS[source].issubset(fields):
            failures.add(f"missing_{source.lower()}_health_fields")
            continue
        uptime = int(timestamp[1])
        if uptime <= after_uptime:
            continue
        previous = samples[source][-1] if samples[source] else after_uptime
        if uptime <= previous:
            failures.add(f"stale_{source.lower()}_health")
            continue
        samples[source].append(uptime)
        if source == "Voice" and (fields["enabled"] != 1 or fields["listening"] != 1):
            failures.add("voice_wake_not_listening_after_camera")
    duration = ended - started
    if duration < 60:
        failures.add("post_home_observation_under_60_seconds")
    for source, uptimes in samples.items():
        if len(uptimes) < (2 if source == "MQTT" else 1):
            failures.add(f"missing_fresh_{source.lower()}_health")
    report = evidence.report(duration, duration)
    failures.update(report["failures"])
    return {"duration_seconds": round(duration, 3), "uptimes": samples,
            "failures": sorted(failures), "resources": report,
            "software_health_observed": not failures}


class CameraSmokeSession(Session):
    def __init__(self, port, log):
        super().__init__(port, log)
        self.started = time.monotonic()
        self.evidence = Evidence()
        self.entries = []
        self.cycles = []
        self.home_confirmed = False
        self.last_app = None
        self.last_request = None
        self.cleanup = None

    def line(self, deadline):
        line = super().line(deadline)
        if line is not None:
            self.accept(line)
        return line

    def accept(self, line):
        elapsed = time.monotonic() - self.started
        self.entries.append((elapsed, line))
        self.evidence.accept(line, elapsed)

    def finish(self):
        for line in self.pending.decode("utf-8", errors="replace").splitlines():
            self.accept(line)
        self.pending.clear()

    def launch(self, app):
        self.evidence.record_app_request(app, time.monotonic() - self.started)
        self.home_confirmed = False
        self.last_app = app
        self.last_request = self.evidence.exercise_events[-1]
        self.port.write(f"RODAK_APP_LAUNCH_V1 {app}\n".encode("ascii"))
        self.port.flush()
        return self.last_request

    def until(self, condition, seconds, label):
        deadline = time.monotonic() + seconds
        while not condition() and time.monotonic() < deadline:
            self.line(deadline)
        if not condition():
            raise TimeoutError(f"Missing software evidence: {label}")

    def cleanup_home(self, timeout):
        if self.home_confirmed:
            self.cleanup = {"home_confirmed": True, "additional_request": False}
            return
        requested = self.last_app != "home"
        home = self.launch("home") if requested else self.last_request
        self.cleanup = {"home_confirmed": False, "additional_request": requested, "request": home}
        self.until(lambda: home["ack"] and home["complete"], timeout, "bounded Home cleanup")
        self.home_confirmed = True
        self.cleanup["home_confirmed"] = True

    def run(self, cycles, timeout, observation):
        self.observe(2)
        home = self.launch("home")
        self.until(lambda: home["ack"] and home["complete"], timeout, "initial Home navigation")
        self.home_confirmed = True
        for index in range(cycles):
            cycle = {"cycle": index + 1, "failures": [], "close_markers": []}
            self.cycles.append(cycle)
            camera = self.launch("camera")
            cycle["camera"] = camera
            try:
                self.until(lambda: camera["ack"] and camera["complete"]
                           and camera["camera_preview_uptime"] is not None, timeout, "Camera preview submission")
            except TimeoutError as error:
                cycle["failures"].append(str(error))
            # A failed preview still gets one bounded Home cleanup request; it is never retried.
            boundary = len(self.entries)
            home = self.launch("home")
            cycle["home"] = home
            try:
                self.until(lambda: home["ack"] and home["complete"], timeout, "Home navigation after Camera")
                self.home_confirmed = True
                first_frame = camera["camera_preview_uptime"]
                if first_frame is not None:
                    self.until(lambda: len(close_evidence(self.entries[boundary:], first_frame)) == len(CLOSE_MARKERS),
                               timeout, "Camera close stages")
                    cycle["close_markers"] = close_evidence(self.entries[boundary:], first_frame)
                else:
                    cycle["failures"].append("Camera did not submit a preview frame")
                observation_beginning = len(self.entries)
                started = time.monotonic() - self.started
                after_uptime = (cycle["close_markers"][-1]["uptime"] if cycle["close_markers"]
                                else self.evidence.last_uptime or 0)
                self.observe(observation)
                cycle["health"] = health_evidence(self.entries[observation_beginning:], started,
                                                  time.monotonic() - self.started, after_uptime)
                cycle["failures"].extend(cycle["health"]["failures"])
            except TimeoutError as error:
                cycle["failures"].append(str(error))
                if camera["camera_preview_uptime"] is not None:
                    cycle["close_markers"] = close_evidence(self.entries[boundary:], camera["camera_preview_uptime"])
            cycle["software_path_observed"] = bool(camera["ack"] and camera["complete"]
                and camera["camera_preview_uptime"] is not None and home["ack"] and home["complete"]
                and len(cycle["close_markers"]) == len(CLOSE_MARKERS))
            if cycle["failures"] or self.evidence.report(time.monotonic() - self.started, 0)["failures"]:
                break

    def report(self, requested_cycles, error=None):
        elapsed = time.monotonic() - self.started
        resources = self.evidence.report(elapsed, elapsed, True)
        failures = set(resources["failures"])
        for cycle in self.cycles:
            failures.update(cycle["failures"])
        if len(self.cycles) != requested_cycles or any(not cycle.get("software_path_observed") for cycle in self.cycles):
            failures.add("camera_cycles_incomplete")
        if error:
            failures.add(error)
        if self.cleanup and self.cleanup.get("error"):
            failures.add(self.cleanup["error"])
        return {"status": "no-go" if failures else "software-smoke-observed",
                "requested_cycles": requested_cycles, "cycles": self.cycles,
                "cleanup": self.cleanup,
                "resources": resources, "failures": sorted(failures), "eight_hour_gate_passed": False,
                "evidence_boundary": "Bounded software preview/close/health observations; no physical image, acoustic, resource-exhaustion or eight-hour acceptance"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--flash-record", type=Path, required=True)
    parser.add_argument("--boot-log", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cycles", type=int, default=3)
    parser.add_argument("--stage-timeout", type=float, default=30)
    parser.add_argument("--observe-seconds", type=float, default=65)
    args = parser.parse_args()
    if not 1 <= args.cycles <= 10 or not 1 <= args.stage_timeout <= 120 or not 60 <= args.observe_seconds <= 300:
        parser.error("cycles must be 1..10, timeout 1..120 and observation 60..300 seconds")
    identity = package_context(args.package, args.flash_record, args.boot_log)
    if serial is None:
        raise RuntimeError("pyserial is required for the single serial session")
    args.output.mkdir(parents=True, exist_ok=False)
    error = None
    opened = False
    with (args.output / "serial.log").open("wb") as log:
        port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
        port.port, port.dtr, port.rts = args.port, False, False
        session = CameraSmokeSession(port, log)
        try:
            port.open()
            opened = True
            session.run(args.cycles, args.stage_timeout, args.observe_seconds)
        except (Exception, KeyboardInterrupt) as exception:
            error = f"{type(exception).__name__}: {exception}"
        finally:
            try:
                if opened:
                    session.cleanup_home(min(args.stage_timeout, 10))
            except (Exception, KeyboardInterrupt) as exception:
                if session.cleanup is None:
                    session.cleanup = {"home_confirmed": False}
                session.cleanup["error"] = f"Home cleanup failed: {type(exception).__name__}: {exception}"
            finally:
                try:
                    session.finish()
                except (Exception, KeyboardInterrupt) as exception:
                    error = f"{error + '; ' if error else ''}Final log parsing failed: {exception}"
                finally:
                    try:
                        port.close()
                    except Exception as exception:
                        error = f"{error + '; ' if error else ''}Serial close failed: {exception}"
    report = session.report(args.cycles, error)
    report.update(identity=identity, port=args.port, serial_sessions=int(opened),
                  captured_utc=datetime.now(timezone.utc).isoformat(),
                  serial_sha256=sha256(args.output / "serial.log"))
    (args.output / "summary.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["status"] == "software-smoke-observed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
