import json
import struct
import sys
import tempfile
import unittest
import wave
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))

import run_wake_eval as tool  # noqa: E402


def write_wav(path, samples, rate=16000, channels=1):
    with wave.open(str(path), "wb") as target:
        target.setnchannels(channels)
        target.setsampwidth(2)
        target.setframerate(rate)
        target.writeframes(struct.pack(f"<{len(samples)}h", *samples))


class FakePort:
    """Serial double that answers each command with the scripted device lines."""

    def __init__(self, replies):
        self.replies = replies
        self.pending = bytearray()
        self.written = []

    @property
    def in_waiting(self):
        return len(self.pending)

    def read(self, size):
        data = bytes(self.pending[:size])
        del self.pending[:size]
        return data

    def write(self, data):
        command = data.decode("ascii").strip()
        self.written.append(command)
        verb = command.removeprefix(tool.PREFIX).split(maxsplit=1)[0]
        for line in self.replies.get(verb, [f'{tool.RESULT_PREFIX}{{"ok":true,"command":"{verb}"}}']):
            self.pending.extend((line + "\n").encode("utf-8"))

    def flush(self):
        pass


class LoadClipTest(unittest.TestCase):
    def test_downmixes_and_applies_gain(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "pos_x.wav"
            write_wav(path, [1000, 3000] * 1600, channels=2)
            samples = tool.load_clip(path, -6.0)
        self.assertEqual(len(samples), 1600)
        self.assertTrue(all(abs(value - 1002) <= 1 for value in samples))

    def test_rejects_other_rates_and_overlong_clips(self):
        with tempfile.TemporaryDirectory() as directory:
            other_rate = Path(directory) / "rate.wav"
            write_wav(other_rate, [0] * 4800, rate=48000)
            long = Path(directory) / "long.wav"
            write_wav(long, [0] * 160001)
            for path in (other_rate, long):
                with self.assertRaises(ValueError):
                    tool.load_clip(path, 0.0)


class EvaluateTest(unittest.TestCase):
    def test_collects_detections_and_timeouts_until_finished(self):
        port = FakePort({"wake_eval": [
            f'{tool.RESULT_PREFIX}{{"ok":true,"command":"wake_eval"}}',
            "I (1) VoiceAudioFrontend: Wake evaluation timeout: at_ms=3040",
            "I (2) VoiceAudioFrontend: Wake evaluation detected: at_ms=1312 matched=1 prob=0.996 text=nihaodake",
            "I (3) VoiceAudioFrontend: Wake evaluation finished: reason=complete threshold_permille=140 "
            "fed_ms=3296 detections=1 timeouts=1 realignments=2 restored=1",
        ]})
        with tempfile.TemporaryFile() as log:
            detections, timeouts = tool.Device(port, log).evaluate(140, 0, "hai da ke", 5)
        self.assertEqual(port.written[-1], tool.PREFIX + "wake_eval 140 0 hai da ke")
        self.assertEqual(detections, [{"at_ms": 1312, "matched": True, "prob": 0.996}])
        self.assertEqual(timeouts, [3040])

    def test_unrestored_or_cancelled_evaluation_is_an_error(self):
        for finished in ("reason=complete threshold_permille=140 fed_ms=1 detections=0 timeouts=0 restored=0",
                         "reason=cancelled threshold_permille=140 fed_ms=1 detections=0 timeouts=0 restored=1"):
            port = FakePort({"wake_eval": [
                f'{tool.RESULT_PREFIX}{{"ok":true,"command":"wake_eval"}}',
                f"I (1) VoiceAudioFrontend: Wake evaluation finished: {finished}",
            ]})
            with tempfile.TemporaryFile() as log, self.assertRaises(RuntimeError):
                tool.Device(port, log).evaluate(140, 0, "", 5)

    def test_device_rejection_is_reported(self):
        port = FakePort({"wake_eval": [
            f'{tool.RESULT_PREFIX}{{"ok":true,"command":"wake_eval"}}',
            "W (1) VoiceAudioFrontend: Wake evaluation rejected: not listening or incomplete audio",
        ]})
        with tempfile.TemporaryFile() as log, self.assertRaises(RuntimeError):
            tool.Device(port, log).evaluate(140, 0, "", 5)

    def test_outcome_logged_before_the_reply_is_not_lost(self):
        for outcome, error in (
            ("W (1) VoiceAudioFrontend: Wake evaluation rejected: not listening or incomplete audio",
             RuntimeError),
            ("I (1) VoiceAudioFrontend: Wake evaluation finished: reason=complete threshold_permille=140 "
             "fed_ms=1 detections=0 timeouts=0 restored=1", None),
        ):
            port = FakePort({"wake_eval": [
                outcome, f'{tool.RESULT_PREFIX}{{"ok":true,"command":"wake_eval"}}']})
            with tempfile.TemporaryFile() as log:
                device = tool.Device(port, log)
                if error:
                    with self.assertRaises(error):
                        device.evaluate(140, 0, "", 1)
                else:
                    self.assertEqual(device.evaluate(140, 0, "", 1), ([], []))

    def test_stale_reply_for_another_command_is_ignored(self):
        port = FakePort({"audio_clear": [
            f'{tool.RESULT_PREFIX}{{"ok":true,"command":"audio_begin"}}',
            f'{tool.RESULT_PREFIX}{{"ok":false,"command":"audio_clear"}}',
        ]})
        with tempfile.TemporaryFile() as log, self.assertRaises(RuntimeError):
            tool.Device(port, log).command("audio_clear")


class SummaryTest(unittest.TestCase):
    def test_counts_a_trial_once_and_bounds_the_interval(self):
        rows = [
            {"label": "pos_a", "threshold": 140,
             "detections": [{"matched": True}, {"matched": True}]},
            {"label": "pos_a", "threshold": 140, "detections": [{"matched": False}]},
        ]
        report = tool.summarize(rows, [140])
        cell = report["pos_a"]["140"]
        self.assertEqual((cell["trials"], cell["woke"], cell["rate"]), (2, 1, 0.5))
        self.assertTrue(0 <= cell["ci95"][0] < 0.5 < cell["ci95"][1] <= 1)
        json.dumps(report)


if __name__ == "__main__":
    unittest.main()
