"""Capture bounded release soak evidence without resetting or reconnecting the board."""
import argparse
from collections import deque
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import statistics
import time


class Evidence:
    def __init__(self):
        self.samples = 0
        self.last_health = None
        self.last_uptime = None
        self.max_health_gap = 0
        self.minima = {}
        self.first = []
        self.last = deque(maxlen=10)
        self.failures = set()
        self.exercise_requests = 0
        self.exercise_acks = 0

    def accept(self, line, elapsed):
        if re.search(r"Guru Meditation|assert failed|abort\(\)|watchdog.*trigger|rst:|ESP-ROM:|stack overflow|CORRUPT HEAP", line, re.I):
            self.failures.add("reset_or_runtime_failure")
        if 'RODAK_APP_LAUNCH_RESULT {"queued":true}' in line:
            self.exercise_acks += 1
        if "MQTT health:" not in line:
            return
        fields = {k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", line)}
        required = {"connected", "internal_free", "internal_largest", "stack_min_free", "psram_free", "psram_largest", "telemetry_queued"}
        if not required.issubset(fields):
            self.failures.add("missing_release_health_fields")
            return
        timestamp = re.search(r"\((\d+)\)", line)
        if timestamp:
            uptime = int(timestamp[1])
            if self.last_uptime is not None and uptime < self.last_uptime:
                self.failures.add("uptime_regressed")
            self.last_uptime = uptime
        self.max_health_gap = max(self.max_health_gap, elapsed - (self.last_health or 0))
        self.last_health = elapsed
        self.samples += 1
        if fields["connected"] != 1 or fields["telemetry_queued"] != 1:
            self.failures.add("mqtt_disconnected_or_publish_failed")
        for name in required - {"connected", "telemetry_queued"}:
            self.minima[name] = min(self.minima.get(name, fields[name]), fields[name])
        if len(self.first) < 10:
            self.first.append(fields["internal_free"])
        self.last.append(fields["internal_free"])

    def report(self, elapsed, requested, complete=False):
        gaps = max(self.max_health_gap, elapsed - (self.last_health or 0))
        failures = set(self.failures)
        if gaps > 90:
            failures.add("health_gap_over_90_seconds")
        if self.minima.get("internal_largest", 8192) < 8192 or self.minima.get("stack_min_free", 512) < 512:
            failures.add("insufficient_memory_or_stack_headroom")
        drop = statistics.median(self.first) - statistics.median(self.last) if self.first else None
        if self.samples >= 20 and drop > 8192:
            failures.add("internal_heap_median_drop_over_8KiB")
        if complete and self.exercise_requests != self.exercise_acks:
            failures.add("unacknowledged_app_launch")
        passed = complete and elapsed >= requested and requested >= 28800 and self.samples >= 960 and not failures
        return dict(status="pass-observed" if passed else "no-go" if failures else "incomplete",
                    elapsed_seconds=round(elapsed, 2), requested_seconds=requested,
                    health_samples=self.samples, max_health_gap_seconds=round(gaps, 2),
                    minima=self.minima, internal_heap_median_drop=drop,
                    exercise_requests=self.exercise_requests, exercise_acks=self.exercise_acks,
                    failures=sorted(failures), complete=complete)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM13")
    parser.add_argument("--duration", type=float, default=28800)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--build-id", required=True, help="Identity from the verified flash record; never auto-inferred")
    parser.add_argument("--exercise-apps", action="store_true", help="Cycle home/photos/camera/home/music/home every 5 minutes")
    args = parser.parse_args()
    if args.duration <= 0:
        parser.error("duration must be positive")
    import serial
    args.output.mkdir(parents=True, exist_ok=False)
    evidence = Evidence()
    started = time.monotonic()
    identity = dict(started_utc=datetime.now(timezone.utc).isoformat(), build_id=args.build_id, port=args.port)
    def save(complete=False):
        report = evidence.report(time.monotonic() - started, args.duration, complete)
        report.update(identity)
        temporary = args.output / "status.json.part"
        temporary.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        temporary.replace(args.output / "status.json")
        return report
    port = serial.serial_for_url(args.port, baudrate=115200, timeout=0.25, do_not_open=True)
    port.dtr = port.rts = False
    next_status = 0
    next_action = 300
    apps = ("home", "photos", "camera", "home", "music", "home")
    pending = bytearray()
    save()
    try:
        port.open()
        with (args.output / "serial.log").open("ab") as log:
            while time.monotonic() - started < args.duration:
                elapsed = time.monotonic() - started
                data = port.read(min(port.in_waiting or 1, 4096))
                if data:
                    log.write(data); log.flush(); pending.extend(data)
                    while b"\n" in pending:
                        line, _, pending = pending.partition(b"\n")
                        evidence.accept(line.decode("utf-8", errors="replace"), elapsed)
                    if len(pending) > 65536:
                        evidence.failures.add("unframed_serial_overflow"); pending.clear()
                if args.exercise_apps and elapsed >= next_action:
                    app = apps[evidence.exercise_requests % len(apps)]
                    port.write(f"RODAK_APP_LAUNCH_V1 {app}\n".encode())
                    evidence.exercise_requests += 1
                    next_action += 300
                if elapsed >= next_status:
                    save(); next_status += 30
    except (Exception, KeyboardInterrupt) as error:
        evidence.failures.add(type(error).__name__)
        save()
        raise
    finally:
        port.close()
    report = save(True)
    print(json.dumps(report, indent=2))
    return 0 if report["status"] == "pass-observed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
