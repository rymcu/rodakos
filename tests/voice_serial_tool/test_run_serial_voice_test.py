import ast
import contextlib
import io
import json
import re
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch


TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))

import run_serial_voice_test as tool  # noqa: E402
from run_serial_voice_test import Session, validate_cycle_sessions, validate_same_session_turns  # noqa: E402


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


def cycle_log(playbacks=1, cycle=1, final=False):
    text = result("audio_begin") + "\n" + valid_log(playbacks).replace(SESSION_ID, f"cycle-session-{cycle}")
    text = text.replace("focus_token=1", f"focus_token={cycle}")
    if not final:
        text = text.replace(result("audio_clear") + "\n", "")
    return text


def with_interruptions(text, counts):
    lines = text.splitlines()
    position = next(index for index, line in enumerate(lines) if "Playback started:" in line) + 1
    markers = ("TTS interrupted:", "AFE VAD confirmed:", "VAD end sent:")
    lines[position:position] = [f"I (111) VoiceAssistantService: {marker}"
                                for marker, count in zip(markers, counts) for _ in range(count)]
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


class CycleSessionTests(unittest.TestCase):
    def test_each_cycle_allows_multiple_completed_playbacks(self):
        trace = validate_cycle_sessions(cycle_log(2) + cycle_log(3, 2, True), 2)
        self.assertTrue(trace["passed"], trace)
        self.assertEqual([cycle["playback_stats_events"] for cycle in trace["sessions"]], [2, 3])

    def test_actual_legacy_gate_accepts_stats_from_the_wrong_cycle(self):
        second = "\n".join(line for line in cycle_log(1, 2, True).splitlines()
                           if "Playback started:" not in line and "Playback audio stats:" not in line)
        text = cycle_log(2) + second
        summary = tool.parse_log(text)
        summary.update(vad_interruptions=0, afe_vad_confirmations=0, vad_ends=0)
        stats = [{key: int(value) for key, value in re.findall(
            r"(packets|decoded_frames|pcm_bytes|write_failures)=(\d+)", line)}
            for line in text.splitlines() if "Playback audio stats:" in line]
        trace = validate_cycle_sessions(text, 2)
        context = dict(turns=None, error=None, summary=summary, cycles=2, completed=2,
                       cycle_trace=trace, stats=stats,
                       args=SimpleNamespace(live_mic_playback=False, barge_in=False, interruptions=1))
        legacy = Path(__file__).with_name("legacy-cycle-gate.expr").read_text(encoding="utf-8")
        self.assertTrue(eval(compile(ast.parse(legacy, mode="eval"), "legacy-cycle-gate", "eval"), context))
        source = ast.parse((TOOLS / "run_serial_voice_test.py").read_text(encoding="utf-8"))
        assignment = next(node for node in ast.walk(source) if isinstance(node, ast.Assign)
                          and any(isinstance(target, ast.Name) and target.id == "cycle_gate"
                                  for target in node.targets))
        current = compile(ast.Expression(assignment.value), "current-cycle-gate", "eval")
        self.assertFalse(eval(current, context))
        self.assertTrue(trace["sessions"][0]["passed"])
        self.assertFalse(trace["sessions"][1]["passed"])

    def test_barge_in_counts_cannot_be_borrowed_from_another_cycle(self):
        source = ast.parse((TOOLS / "run_serial_voice_test.py").read_text(encoding="utf-8"))
        expressions = {
            name: compile(ast.Expression(next(node.value for node in ast.walk(source)
                          if isinstance(node, ast.Assign) and any(
                              isinstance(target, ast.Name) and target.id == name for target in node.targets))),
                          name, "eval")
            for name in ("cycle_trace", "cycle_gate")
        }
        legacy = Path(__file__).with_name("legacy-cycle-gate.expr").read_text(encoding="utf-8")
        legacy_gate = compile(ast.parse(legacy, mode="eval"), "legacy-cycle-gate", "eval")
        for shifted_counter in (0, 1, 2, None):
            with self.subTest(shifted_counter=shifted_counter):
                first = [2 if shifted_counter in (index, None) else 1 for index in range(3)]
                second = [0 if shifted_counter in (index, None) else 1 for index in range(3)]
                text = with_interruptions(cycle_log(1), first) + with_interruptions(cycle_log(1, 2, True), second)
                summary = tool.parse_log(text)
                summary.update(vad_interruptions=text.count("TTS interrupted:"),
                               afe_vad_confirmations=text.count("AFE VAD confirmed:"),
                               vad_ends=text.count("VAD end sent:"))
                context = dict(turns=None, text=text, error=None, summary=summary, cycles=2, completed=2,
                               stats=[dict(packets=3, decoded_frames=3, pcm_bytes=12, write_failures=0)] * 2,
                               validate_cycle_sessions=validate_cycle_sessions,
                               args=SimpleNamespace(live_mic_playback=False, barge_in=True, interruptions=1))
                self.assertTrue(eval(legacy_gate, context))
                context["cycle_trace"] = eval(expressions["cycle_trace"], context)
                self.assertFalse(eval(expressions["cycle_gate"], context))
                self.assertTrue(all(not cycle["passed"] for cycle in context["cycle_trace"]["sessions"]))
                context["args"].barge_in = False
                context["cycle_trace"] = eval(expressions["cycle_trace"], context)
                self.assertTrue(eval(expressions["cycle_gate"], context))

    def test_barge_in_requires_the_requested_count_in_each_cycle(self):
        text = with_interruptions(cycle_log(1), (1, 1, 1)) + with_interruptions(cycle_log(1, 2, True), (1, 1, 1))
        trace = validate_cycle_sessions(text, 2, expected_interruptions=1)
        self.assertTrue(trace["passed"], trace)
        self.assertEqual([(cycle["vad_interruptions"], cycle["afe_vad_confirmations"], cycle["vad_ends"])
                          for cycle in trace["sessions"]], [(1, 1, 1), (1, 1, 1)])
        self.assertFalse(validate_cycle_sessions(text, 2, expected_interruptions=2)["passed"])

    def test_missing_lifecycle_evidence_cannot_be_borrowed_from_another_cycle(self):
        for cycle in range(2):
            for marker in ("Realtime voice session ready:", "Sent speech input start:",
                           "Interaction started:", "Interaction stopped:", "Playback started:",
                           "Playback audio stats:", "Voice websocket cleanup started",
                           "Voice websocket destroy completed:", "Always-on wake monitoring armed"):
                with self.subTest(cycle=cycle + 1, marker=marker):
                    pieces = [cycle_log(2), cycle_log(2, 2, True)]
                    pieces[cycle] = "\n".join(line for line in pieces[cycle].splitlines()
                                                if marker not in line) + "\n"
                    trace = validate_cycle_sessions("".join(pieces), 2)
                    self.assertFalse(trace["passed"], trace)
                    self.assertFalse(trace["sessions"][cycle]["passed"])

    def test_each_stats_record_requires_positive_audio_and_successful_writes(self):
        for cycle in range(2):
            for before, after in (("packets=3", "packets=0"), ("decoded_frames=3", "decoded_frames=0"),
                                  ("pcm_bytes=12", ""), ("write_failures=0", "write_failures=1")):
                with self.subTest(cycle=cycle + 1, failure=after):
                    pieces = [cycle_log(2), cycle_log(2, 2, True)]
                    pieces[cycle] = pieces[cycle].replace(before, after, 1)
                    self.assertFalse(validate_cycle_sessions("".join(pieces), 2)["passed"])

    def test_interrupted_playback_need_not_publish_completion_stats(self):
        text = cycle_log(2, final=True)
        text = "\n".join(line for line in text.splitlines() if "I (111)" not in line)
        trace = validate_cycle_sessions(text, 1)
        self.assertTrue(trace["passed"], trace)
        self.assertEqual(trace["sessions"][0]["playback_start_events"], 2)
        self.assertEqual(trace["sessions"][0]["playback_stats_events"], 1)

    def test_natural_timeout_may_stop_and_rearm_before_explicit_stop_reply(self):
        text = cycle_log(2, final=True).replace(result("stop") + "\n", "")
        text = text.replace(result("audio_clear"), result("stop") + "\n" + result("audio_clear"))
        trace = validate_cycle_sessions(text, 1)
        self.assertTrue(trace["passed"], trace)

    def test_async_startup_and_playback_logs_need_not_follow_wake_ack_or_start_log(self):
        lines = cycle_log(1, final=True).splitlines()
        wake = lines.pop(lines.index(result("wake")))
        start = next(line for line in lines if "Interaction started:" in line)
        lines.remove(start)
        position = next(index for index, line in enumerate(lines) if "Playback started:" in line) + 1
        lines[position:position] = [wake, start]
        trace = validate_cycle_sessions("\n".join(lines), 1)
        self.assertTrue(trace["passed"], trace)

    def test_inflight_io_stats_may_arrive_during_transport_cleanup(self):
        lines = cycle_log(1, final=True).splitlines()
        stats = next(line for line in lines if "Playback audio stats:" in line)
        lines.remove(stats)
        position = next(index for index, line in enumerate(lines) if "Voice websocket destroy completed:" in line)
        lines.insert(position, stats)
        self.assertTrue(validate_cycle_sessions("\n".join(lines), 1)["passed"])

    def test_wrong_session_focus_or_destroy_result_is_not_success(self):
        for old, new in (("input start: session=cycle-session-2", "input start: session=old-session"),
                         ("Interaction stopped: focus_token=2", "Interaction stopped: focus_token=1"),
                         ("destroy completed: result=ESP_OK", "destroy completed: result=ESP_FAIL")):
            with self.subTest(replacement=new):
                text = cycle_log(2) + cycle_log(1, 2, True).replace(old, new)
                self.assertFalse(validate_cycle_sessions(text, 2)["passed"])

    def test_replayed_session_cannot_be_counted_as_another_cycle(self):
        text = cycle_log(1) + cycle_log(1, 2, True).replace("cycle-session-2", "cycle-session-1")
        self.assertFalse(validate_cycle_sessions(text, 2)["passed"])

    def test_playback_and_cleanup_evidence_must_be_in_lifecycle_order(self):
        for marker, insert_before in (("Playback audio stats:", "Playback started:"),
                                      ("Voice websocket destroy completed:", "Voice websocket cleanup started"),
                                      ("Always-on wake monitoring armed", "Interaction stopped:")):
            with self.subTest(marker=marker):
                lines = cycle_log(1, final=True).splitlines()
                moved = next(line for line in lines if marker in line)
                lines.remove(moved)
                position = next(index for index, line in enumerate(lines) if insert_before in line)
                lines.insert(position, moved)
                self.assertFalse(validate_cycle_sessions("\n".join(lines), 1)["passed"])

    def test_each_cycle_needs_its_stop_reply_and_final_input_cleanup(self):
        for cycle in range(2):
            pieces = [cycle_log(1), cycle_log(1, 2, True)]
            pieces[cycle] = pieces[cycle].replace(result("stop") + "\n", "")
            self.assertFalse(validate_cycle_sessions("".join(pieces), 2)["passed"])
        for final_clear in ("", result("audio_clear", False)):
            text = cycle_log(1, final=True).replace(result("audio_clear"), final_clear)
            self.assertFalse(validate_cycle_sessions(text, 1)["passed"])

    def test_missing_extra_or_unowned_interactions_do_not_pass(self):
        self.assertFalse(validate_cycle_sessions("", 1)["passed"])
        self.assertFalse(validate_cycle_sessions(cycle_log(1, final=True), 2)["passed"])
        self.assertFalse(validate_cycle_sessions(cycle_log(1) + cycle_log(1, 2, True), 1)["passed"])
        text = "I (10) VoiceAssistantService: Interaction stopped: focus_token=9\n" + cycle_log(1, final=True)
        self.assertFalse(validate_cycle_sessions(text, 1)["passed"])


if __name__ == "__main__":
    unittest.main()
