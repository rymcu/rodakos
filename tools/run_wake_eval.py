"""Score wake-word clips on a device through the real MultiNet runtime.

Each clip is uploaded as diagnostic audio and replayed into the idle wake recognizer with
`wake_eval`, which logs every match with its probability and never opens a voice session.
Clip names start with `pos_` (should wake) or `neg_` (should not). The run sweeps the
requested thresholds and lead-in offsets; offsets show whether the recognizer's timeout
window cuts phrases that arrive at an unlucky moment. This replaces the microphone signal,
so it measures the recognizer and wake phrase, not room acoustics or the analog front end.
"""

import argparse
import json
import math
import re
import sys
import time
import wave
from pathlib import Path

try:
    import serial
except ImportError:  # pragma: no cover - reported at runtime
    serial = None

SAMPLE_RATE = 16000
PREFIX = "RODAK_VOICE_TEST_V1 "
RESULT_PREFIX = "RODAK_VOICE_TEST_RESULT "
FINISHED = re.compile(
    r"Wake evaluation finished: reason=(\w+) threshold_permille=(\d+) fed_ms=(\d+) "
    r"detections=(\d+) timeouts=(\d+)(?: realignments=\d+)? restored=(\d)")
DETECTED = re.compile(r"Wake evaluation detected: at_ms=(\d+) matched=(\d) prob=([0-9.]+)")
TIMEOUT = re.compile(r"Wake evaluation timeout: at_ms=(\d+)")


def load_clip(path, gain_db):
    with wave.open(str(path), "rb") as source:
        channels, width, rate = source.getnchannels(), source.getsampwidth(), source.getframerate()
        raw = source.readframes(source.getnframes())
    # Resampling here would need an anti-alias filter; require clips made at 16 kHz instead.
    if width != 2 or rate != SAMPLE_RATE:
        raise ValueError(f"{path.name}: expected 16 kHz PCM16")
    samples = memoryview(raw).cast("h").tolist()
    if channels > 1:
        samples = [sum(samples[i:i + channels]) // channels for i in range(0, len(samples), channels)]
    scale = 10 ** (gain_db / 20)
    samples = [max(-32768, min(32767, int(round(value * scale)))) for value in samples]
    if not 0 < len(samples) <= 160000:
        raise ValueError(f"{path.name}: clip must contain between one sample and ten seconds")
    return samples


def peak_dbfs(samples):
    peak = max(1, max(abs(value) for value in samples))
    return 20 * math.log10(peak / 32768)


class Device:
    def __init__(self, port, log):
        self.port, self.log, self.pending = port, log, bytearray()

    def line(self, deadline):
        while time.monotonic() < deadline:
            split = self.pending.find(b"\n")
            if split >= 0:
                raw = bytes(self.pending[:split + 1])
                del self.pending[:split + 1]
                return raw.decode("utf-8", errors="replace").strip()
            data = self.port.read(self.port.in_waiting or 1)
            if data:
                self.log.write(data)
                self.log.flush()
                self.pending.extend(data)
        return None

    def command(self, command, timeout=15, early=None):
        """Send a command and wait for its reply; lines that arrive first go to `early`."""
        expected = command.split(maxsplit=1)[0]
        self.port.write((PREFIX + command + "\n").encode("ascii"))
        self.port.flush()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = self.line(deadline)
            if line is None:
                break
            if not line.startswith(RESULT_PREFIX):
                if early is not None:
                    early.append(line)
                continue
            if line.startswith(RESULT_PREFIX):
                reply = json.loads(line[len(RESULT_PREFIX):])
                if reply.get("command") != expected:
                    continue
                if not reply.get("ok"):
                    raise RuntimeError(f"device rejected {expected}")
                return
        raise TimeoutError(f"no reply for {expected}")

    def upload(self, samples):
        pcm = b"".join(int(value).to_bytes(2, "little", signed=True) for value in samples)
        self.command(f"audio_begin {len(samples)}")
        for offset in range(0, len(pcm), 512):
            self.command(f"audio_chunk {offset // 2} {pcm[offset:offset + 512].hex()}")

    def evaluate(self, threshold, lead_ms, wake_command, timeout):
        arguments = f"{threshold} {lead_ms}" + (f" {wake_command}" if wake_command else "")
        # The evaluation runs on a higher-priority task and may log before the reply prints.
        early = []
        self.command("wake_eval " + arguments, early=early)
        detections, timeouts = [], []
        deadline = time.monotonic() + timeout

        def lines():
            yield from early
            while time.monotonic() < deadline:
                line = self.line(deadline)
                if line is None:
                    return
                yield line

        for line in lines():
            match = DETECTED.search(line)
            if match:
                detections.append({"at_ms": int(match.group(1)), "matched": match.group(2) == "1",
                                   "prob": float(match.group(3))})
                continue
            match = TIMEOUT.search(line)
            if match:
                timeouts.append(int(match.group(1)))
                continue
            if "Wake evaluation rejected" in line:
                raise RuntimeError(line)
            match = FINISHED.search(line)
            if match:
                if match.group(1) != "complete" or match.group(6) != "1":
                    raise RuntimeError(line)
                return detections, timeouts
        raise TimeoutError("wake evaluation did not finish")


def wilson(successes, total):
    if total == 0:
        return (0.0, 0.0)
    z = 1.96
    p = successes / total
    centre = (p + z * z / (2 * total)) / (1 + z * z / total)
    margin = z * math.sqrt(p * (1 - p) / total + z * z / (4 * total * total)) / (1 + z * z / total)
    return (max(0.0, centre - margin), min(1.0, centre + margin))


def summarize(rows, thresholds):
    report = {}
    for label in sorted({row["label"] for row in rows}):
        group = [row for row in rows if row["label"] == label]
        report[label] = {}
        for threshold in thresholds:
            hits = [row for row in group if row["threshold"] == threshold]
            woke = sum(1 for row in hits if any(d["matched"] for d in row["detections"]))
            low, high = wilson(woke, len(hits))
            report[label][str(threshold)] = {"trials": len(hits), "woke": woke,
                                             "rate": round(woke / len(hits), 3) if hits else None,
                                             "ci95": [round(low, 3), round(high, 3)]}
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--clips", type=Path, required=True, help="directory of pos_/neg_ WAV clips")
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--command", default="", help="pinyin command override, e.g. 'hai da ke'")
    parser.add_argument("--thresholds", default="140", help="comma-separated permille values")
    parser.add_argument("--leads", default="0,750,1500,2250", help="comma-separated lead-in ms")
    parser.add_argument("--gain-db", type=float, default=0.0, help="level change before upload")
    parser.add_argument("--match", default="", help="only clips whose name matches this regex")
    args = parser.parse_args()
    thresholds = [int(value) for value in args.thresholds.split(",") if value]
    leads = [int(value) for value in args.leads.split(",") if value]
    if any(not 1 <= value <= 999 for value in thresholds) or any(not 0 <= value <= 3000 for value in leads):
        parser.error("thresholds must be 1-999 permille and leads 0-3000 ms")
    if args.command and not re.fullmatch(r"[a-z]+( [a-z]+){0,15}", args.command):
        parser.error("command must be lowercase space-separated pinyin")
    clips = sorted(path for path in args.clips.glob("*.wav")
                   if path.name.startswith(("pos_", "neg_")) and re.search(args.match, path.name))
    if not clips:
        parser.error("no pos_/neg_ clips found")
    if serial is None:
        raise RuntimeError("pyserial is required")
    args.log.parent.mkdir(parents=True, exist_ok=True)
    rows = []
    with args.log.open("wb") as log:
        port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
        port.port, port.dtr, port.rts = args.port, False, False
        port.open()
        device = Device(port, log)
        try:
            deadline = time.monotonic() + 2
            while device.line(deadline) is not None:
                pass
            for index, clip in enumerate(clips):
                samples = load_clip(clip, args.gain_db)
                device.upload(samples)
                clip_ms = len(samples) * 1000 // SAMPLE_RATE
                label = clip.name.split("__", 1)[0]
                for threshold in thresholds:
                    for lead in leads:
                        timeout = (lead + clip_ms + 1500) / 1000 * 3 + 10
                        detections, timeouts = device.evaluate(threshold, lead, args.command, timeout)
                        rows.append({"clip": clip.name, "label": label, "threshold": threshold,
                                     "lead_ms": lead, "clip_ms": clip_ms,
                                     "peak_dbfs": round(peak_dbfs(samples), 1),
                                     "detections": detections, "timeouts": timeouts})
                woke = sum(1 for row in rows[-len(thresholds) * len(leads):]
                           if any(d["matched"] for d in row["detections"]))
                print(f"[{index + 1}/{len(clips)}] {clip.name}: woke {woke}/{len(thresholds) * len(leads)}",
                      flush=True)
            device.command("audio_clear")
        except BaseException:
            # Stop an evaluation still feeding diagnostic audio so the microphone takes over.
            try:
                device.command("audio_live")
                device.command("audio_clear")
            except Exception:
                pass
            raise
        finally:
            port.close()
    report = {"command": args.command or "(active identity)", "gain_db": args.gain_db,
              "thresholds": thresholds, "leads_ms": leads, "acoustic_path": False,
              "summary": summarize(rows, thresholds), "rows": rows}
    summary_path = args.log.with_suffix(".summary.json")
    summary_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report["summary"], ensure_ascii=False, indent=2))
    print(f"summary: {summary_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
