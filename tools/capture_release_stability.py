"""Capture bounded release soak evidence without resetting or reconnecting the board."""
import argparse
from collections import deque
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import statistics
import time

MIN_VOICE_SUPERVISOR_STACK_BYTES = 6144
MIN_VOICE_SUPERVISOR_STACK_HEADROOM = 4096


class Evidence:
    def __init__(self):
        self.samples = 0
        self.last_health = None
        self.last_uptime = None
        self.max_health_gap = 0
        self.minima = {}
        self.resource_samples = {}
        self.resource_minima = {}
        self.first = []
        self.last = deque(maxlen=10)
        self.failures = set()
        self.log_counts = {"warning": 0, "error": 0}
        self.log_samples = {"warning": [], "error": []}
        self.exercise_requests = 0
        self.exercise_acks = 0
        self.exercise_completions = 0
        self.exercise_events = []
        self.camera_launch_event = None
        self.camera_instance_event = None
        self.camera_log_uptime = None

    @staticmethod
    def _fields(line):
        return {k: int(v) for k, v in re.findall(r"(\w+)=(-?\d+)", line)}

    def _record_log_level(self, line):
        match = re.search(r"(?:^|\s)([WE])\s*\(\d+\)\s+", line)
        if match is None:
            match = re.match(r"\s*([WE]):", line)
        if not match:
            return
        level = "warning" if match.group(1) == "W" else "error"
        self.log_counts[level] += 1
        if len(self.log_samples[level]) < 20:
            self.log_samples[level].append(line.strip())
        if level == "error":
            self.failures.add("error_log_present")

    def _record_resource(self, source, fields):
        if not fields:
            return
        self.resource_samples[source] = self.resource_samples.get(source, 0) + 1
        minima = self.resource_minima.setdefault(source, {})
        for name, value in fields.items():
            minima[name] = min(minima.get(name, value), value)
            if name == "supervisor_stack_min_free":
                name = "stack_min_free"
            if name in {"internal_free", "internal_min", "internal_largest", "stack_min_free",
                        "psram_free", "psram_min", "psram_largest", "dma_free", "dma_largest"}:
                self.minima[name] = min(self.minima.get(name, value), value)

    def record_app_request(self, app, elapsed):
        self.exercise_requests += 1
        event = {"app": app, "requested_at": round(elapsed, 3),
                 "ack": False, "complete": False}
        if app == "camera":
            event.update(camera_created_uptime=None, camera_preview_uptime=None,
                         camera_preview_sequence=None, camera_observation_closed=False)
        self.exercise_events.append(event)

    def _close_camera_observation(self):
        event = self.camera_instance_event
        if event is not None:
            event["camera_observation_closed"] = True
            if event["camera_preview_uptime"] is None:
                self.failures.add("missing_camera_preview_submission")
        self.camera_instance_event = None
        self.camera_launch_event = None

    def _record_camera_evidence(self, line):
        match = re.search(r"\bI\s*\((\d+)\)\s+(PhoneAppHost|CameraApp): (.*)", line)
        if match is None:
            return
        uptime, tag, message = int(match[1]), match[2], match[3].strip()
        if self.camera_log_uptime is not None and uptime < self.camera_log_uptime:
            return
        self.camera_log_uptime = uptime
        if tag == "PhoneAppHost":
            if message.startswith("Launching app: "):
                self.camera_launch_event = None
                if message == "Launching app: camera":
                    # The serial and LVGL tasks can print admission/startup in either order.
                    # The request must already exist; ACK and completion are checked separately.
                    pending = next((event for event in self.exercise_events
                                    if not event["complete"]), None)
                    if pending is not None and pending["app"] == "camera":
                        self.camera_launch_event = pending
            elif message == "Closing app: camera" or (
                    message.startswith("App launched: ") and message != "App launched: camera"):
                self._close_camera_observation()
            return
        if message == "Destroy: capture guard begin":
            self._close_camera_observation()
        elif message == "Camera app created; preview startup deferred":
            event = self.camera_launch_event
            self.camera_launch_event = None
            if event is None or event["camera_created_uptime"] is not None:
                return
            self._close_camera_observation()
            event["camera_created_uptime"] = uptime
            self.camera_instance_event = event
        else:
            frame = re.fullmatch(r"Camera preview image updated: sequence=(\d+)", message)
            event = self.camera_instance_event
            if frame is None or int(frame[1]) == 0 or event is None:
                return
            if event["camera_preview_uptime"] is None:
                # This log follows lv_image_set_src, not a physical display/camera check.
                event["camera_preview_uptime"] = uptime
                event["camera_preview_sequence"] = int(frame[1])

    def _record_app_ack(self):
        self.exercise_acks += 1
        pending = next((event for event in self.exercise_events if not event["ack"]), None)
        if pending is None:
            if self.exercise_events:
                self.failures.add("unexpected_app_launch_ack")
            return
        pending["ack"] = True

    def _record_app_completion(self, ok):
        if not ok:
            self.failures.add("app_launch_failed")
            return
        self.exercise_completions += 1
        pending = next((event for event in self.exercise_events
                        if event["ack"] and not event["complete"]), None)
        if pending is None:
            if self.exercise_events:
                self.failures.add("app_launch_completion_before_ack")
            return
        pending["complete"] = True

    def accept(self, line, elapsed):
        self._record_log_level(line)
        self._record_camera_evidence(line)
        if re.search(r"Guru Meditation|assert failed|abort\(\)|watchdog.*trigger|rst:|ESP-ROM:|stack overflow|CORRUPT HEAP|panic|brownout|failed to (?:alloc|create)|no mem", line, re.I):
            self.failures.add("reset_or_runtime_failure")
        if 'RODAK_APP_LAUNCH_RESULT {"queued":true}' in line:
            self._record_app_ack()
        if 'RODAK_APP_LAUNCH_RESULT {"queued":false}' in line:
            self.failures.add("app_launch_rejected")
        if 'RODAK_APP_LAUNCH_COMPLETE {"ok":true}' in line:
            self._record_app_completion(True)
        if 'RODAK_APP_LAUNCH_COMPLETE {"ok":false}' in line:
            self._record_app_completion(False)

        fields = self._fields(line)
        if "Voice health:" in line:
            self._record_resource("voice", fields)
        elif "Main health:" in line:
            self._record_resource("main", fields)
        elif "Provisioning health:" in line:
            self._record_resource("provisioning", fields)
        elif "Transport memory " in line:
            self._record_resource("transport", fields)
        elif re.search(r"\b(?:SRAM|internal)\s+free=\d+\s+largest=\d+", line, re.I):
            app_fields = dict(fields)
            if "free" in app_fields:
                app_fields["internal_free"] = app_fields["free"]
            if "largest" in app_fields:
                app_fields["internal_largest"] = app_fields["largest"]
            self._record_resource("app", app_fields)

        if "MQTT health:" not in line:
            return
        required = {"connected", "internal_free", "internal_largest", "stack_min_free", "psram_free", "psram_largest", "telemetry_queued"}
        if not required.issubset(fields):
            self.failures.add("missing_release_health_fields")
            return
        timestamp = re.search(r"\((\d+)\)", line)
        if not timestamp:
            self.failures.add("missing_device_uptime")
            return
        uptime = int(timestamp[1])
        if self.last_uptime is not None and uptime <= self.last_uptime:
            self.failures.add("uptime_regressed" if uptime < self.last_uptime else "uptime_repeated")
            return
        self.last_uptime = uptime
        self.max_health_gap = max(self.max_health_gap, elapsed - (self.last_health or 0))
        self.last_health = elapsed
        self.samples += 1
        if fields["connected"] != 1 or fields["telemetry_queued"] != 1:
            self.failures.add("mqtt_disconnected_or_publish_failed")
        for name in required - {"connected", "telemetry_queued"}:
            self.minima[name] = min(self.minima.get(name, fields[name]), fields[name])
        self._record_resource("mqtt", fields)
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
        voice = self.resource_minima.get("voice", {})
        if voice:
            if voice.get("supervisor_stack_bytes", 0) < MIN_VOICE_SUPERVISOR_STACK_BYTES:
                failures.add("insufficient_voice_supervisor_stack_capacity")
            if voice.get("supervisor_stack_min_free", 0) < MIN_VOICE_SUPERVISOR_STACK_HEADROOM:
                failures.add("insufficient_voice_supervisor_stack_headroom")
        drop = statistics.median(self.first) - statistics.median(self.last) if self.first else None
        if self.samples >= 20 and drop > 8192:
            failures.add("internal_heap_median_drop_over_8KiB")
        if complete and self.exercise_requests != self.exercise_acks:
            failures.add("unacknowledged_app_launch")
        if complete and self.exercise_requests != self.exercise_completions:
            failures.add("uncompleted_app_launch")
        if complete and self.exercise_events:
            if any(not event["ack"] for event in self.exercise_events):
                failures.add("unacknowledged_app_launch")
            if any(not event["complete"] for event in self.exercise_events):
                failures.add("uncompleted_app_launch")
        camera_events = [event for event in self.exercise_events if event["app"] == "camera"]
        if complete and any(event["camera_preview_uptime"] is None for event in camera_events):
            failures.add("missing_camera_preview_submission")
        passed = complete and elapsed >= requested and requested >= 28800 and self.samples >= 960 and not failures
        return dict(status="pass-observed" if passed else "no-go" if failures else "incomplete",
                    elapsed_seconds=round(elapsed, 2), requested_seconds=requested,
                    health_samples=self.samples, max_health_gap_seconds=round(gaps, 2),
                    minima=self.minima, resource_samples=self.resource_samples,
                    resource_minima=self.resource_minima,
                    log_counts=self.log_counts, log_samples=self.log_samples,
                    internal_heap_median_drop=drop,
                    exercise_requests=self.exercise_requests, exercise_acks=self.exercise_acks,
                    exercise_completions=self.exercise_completions,
                    exercise_events=self.exercise_events,
                    camera_requests=len(camera_events),
                    camera_preview_submissions=sum(event["camera_preview_uptime"] is not None
                                                   for event in camera_events),
                    failures=sorted(failures), complete=complete)


class SerialEvidenceStream:
    def __init__(self, evidence):
        self.evidence = evidence
        self.pending = bytearray()

    def accept(self, data, elapsed):
        self.pending.extend(data)
        while b"\n" in self.pending:
            line, _, self.pending = self.pending.partition(b"\n")
            self.evidence.accept(line.decode("utf-8", errors="replace"), elapsed)
        if len(self.pending) > 65536:
            self.evidence.failures.add("unframed_serial_overflow")
            self.pending.clear()

    def finish(self, elapsed):
        # A reset or panic can end the transport before its final newline arrives.
        if self.pending:
            self.evidence.accept(self.pending.decode("utf-8", errors="replace"), elapsed)
            self.pending.clear()


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
    stream = SerialEvidenceStream(evidence)
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
    save()
    try:
        port.open()
        with (args.output / "serial.log").open("ab") as log:
            while time.monotonic() - started < args.duration:
                elapsed = time.monotonic() - started
                data = port.read(min(port.in_waiting or 1, 4096))
                if data:
                    log.write(data); log.flush()
                    stream.accept(data, elapsed)
                if args.exercise_apps and elapsed >= next_action:
                    app = apps[evidence.exercise_requests % len(apps)]
                    port.write(f"RODAK_APP_LAUNCH_V1 {app}\n".encode())
                    evidence.record_app_request(app, elapsed)
                    next_action += 300
                if elapsed >= next_status:
                    save(); next_status += 30
    except (Exception, KeyboardInterrupt) as error:
        evidence.failures.add(type(error).__name__)
        stream.finish(time.monotonic() - started)
        save()
        raise
    finally:
        stream.finish(time.monotonic() - started)
        port.close()
    report = save(True)
    print(json.dumps(report, indent=2))
    return 0 if report["status"] == "pass-observed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
