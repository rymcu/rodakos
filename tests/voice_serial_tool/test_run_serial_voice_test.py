import contextlib
import io
import json
import sys
import unittest
from pathlib import Path
from unittest.mock import patch


TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))

import run_serial_voice_test as tool  # noqa: E402
from run_serial_voice_test import Session, validate_same_session_turns  # noqa: E402


SESSION_ID = "session-1"


def result(command, ok=True):
    return f'RODAK_VOICE_TEST_RESULT {json.dumps({"ok": ok, "command": command})}'


def valid_log(turns=2):
    lines = [
        result("wake"),
        f"I (100) RodakRealtimeVoice: Realtime voice session ready: session={SESSION_ID}",
        f"I (101) RodakRealtimeVoice: Sent speech input start: session={SESSION_ID} mode=realtime",
        "I (102) VoiceAssistantService: Interaction started: trigger=wake-word focus_token=1",
    ]
    for turn in range(turns):
        epoch = turn + 1
        lines.extend(
            [
                f"I ({110 + turn * 10}) VoiceAssistantService: Playback started: playback_epoch={epoch}",
                f"I ({111 + turn * 10}) VoiceAssistantService: Playback audio stats: packets=3 decoded_frames=3 pcm_bytes=12 write_failures=0",
            ]
        )
        if turn + 1 < turns:
            lines.extend(
                [
                    f"I ({112 + turn * 10}) RodakRealtimeVoice: Sent speech input start: session={SESSION_ID} mode=realtime",
                    f"I ({113 + turn * 10}) VoiceAssistantService: Follow-up listening started: completed_turns={turn + 1} timeout_ms=30000",
                    result("audio_replay"),
                ]
            )
    lines.extend(
        [
            result("stop"),
            "I (140) RodakRealtimeVoice: Voice websocket cleanup started",
            "I (141) RodakRealtimeVoice: Voice websocket destroy completed: result=ESP_OK",
            "I (142) VoiceAssistantService: Interaction stopped: focus_token=1",
            "I (143) VoiceAudioFrontend: Always-on wake monitoring armed for 你好达克 on TDM slot 2 (MIC2)",
            result("audio_clear"),
        ]
    )
    return "\n".join(lines) + "\n"


class FakePort:
    def __init__(self, data):
        self.data = bytearray(data)
        self.writes = []
        self.in_waiting = len(self.data)

    def write(self, value):
        self.writes.append(value)

    def flush(self):
        return None

    def read(self, size):
        chunk = bytes(self.data[:size])
        del self.data[:size]
        self.in_waiting = len(self.data)
        return chunk


class SerialVoiceToolTests(unittest.TestCase):
    def test_valid_turn_trace_requires_one_session_and_cleanup(self):
        trace = validate_same_session_turns(valid_log(), 2)
        self.assertTrue(trace["passed"], trace)
        self.assertEqual(trace["session_id"], SESSION_ID)
        self.assertEqual(trace["follow_up_events"], [1])

    def test_ready_id_must_match_every_input_start(self):
        text = valid_log().replace(
            f"Sent speech input start: session={SESSION_ID}",
            "Sent speech input start: session=other-session",
            1,
        )
        trace = validate_same_session_turns(text, 2)
        self.assertFalse(trace["passed"])
        self.assertIn("session id", " ".join(trace["errors"]))

    def test_stats_failure_and_cleanup_order_cannot_be_hidden(self):
        text = valid_log().replace("write_failures=0", "write_failures=1", 1)
        text = text.replace(
            "I (142) VoiceAssistantService: Interaction stopped: focus_token=1\n"
            "I (143) VoiceAudioFrontend: Always-on wake monitoring armed",
            "I (143) VoiceAudioFrontend: Always-on wake monitoring armed\n"
            "I (142) VoiceAssistantService: Interaction stopped: focus_token=1",
        )
        trace = validate_same_session_turns(text, 2)
        self.assertFalse(trace["passed"])
        self.assertTrue(any("stats report failure" in error for error in trace["errors"]))
        self.assertTrue(any("rearmed" in error for error in trace["errors"]))

    def test_extra_stats_inside_session_cannot_be_dropped_by_turn_slice(self):
        text = valid_log().replace(
            result("stop"),
            "I (139) VoiceAssistantService: Playback audio stats: packets=1 decoded_frames=1 pcm_bytes=4 write_failures=0\n"
            + result("stop"),
            1,
        )
        trace = validate_same_session_turns(text, 2)
        self.assertFalse(trace["passed"])
        self.assertTrue(any("playback stats events" in error for error in trace["errors"]))

    def test_command_ignores_stale_result_for_another_command(self):
        port = FakePort(
            (result("audio_replay") + "\n" + result("stop") + "\n").encode("utf-8")
        )
        log = io.BytesIO()
        session = Session(port, log)
        reply = session.command("stop")
        self.assertEqual(reply["command"], "stop")
        self.assertEqual(port.writes, [b"RODAK_VOICE_TEST_V1 stop\n"])

    def test_turns_rejects_playback_interruption_modes(self):
        with self.assertRaises(SystemExit) as context:
            with patch.object(
                sys,
                "argv",
                [
                    "run_serial_voice_test.py",
                    "--turns",
                    "2",
                    "--barge-in",
                    "--wav",
                    "missing.wav",
                    "--log",
                    "missing.log",
                ],
            ):
                with contextlib.redirect_stderr(io.StringIO()):
                    tool.main()
        self.assertEqual(context.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
