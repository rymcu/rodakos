#!/usr/bin/env python3
"""Exercise physical-device voice sessions with explicitly injected PCM, not acoustic wake."""

import argparse
import json
import math
import re
from pathlib import Path
import time
import wave

try:
    import serial
except ImportError:  # 让离线 trace 测试和 --help 不依赖 pyserial
    serial = None

from capture_voice_stability import parse_log


def load_pcm(path):
    with wave.open(str(path), "rb") as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate(),
                source.getcomptype()) != (1, 2, 16000, "NONE"):
            raise ValueError("Expected mono 16 kHz uncompressed PCM16 WAV")
        if not 0 < source.getnframes() <= 160000:
            raise ValueError("Fixture must contain between one sample and ten seconds")
        return source.readframes(source.getnframes())


class Session:
    def __init__(self, port, log):
        self.port = port
        self.log = log
        self.pending = bytearray()

    def line(self, deadline):
        while time.monotonic() < deadline:
            split = self.pending.find(b"\n")
            if split >= 0:
                line = bytes(self.pending[:split + 1])
                del self.pending[:split + 1]
                return line.decode("utf-8", errors="replace").strip()
            data = self.port.read(self.port.in_waiting or 1)
            if data:
                self.log.write(data)
                self.log.flush()
                self.pending.extend(data)
        return None

    def command(self, command):
        expected_command = command.split(maxsplit=1)[0]
        self.port.write(("RODAK_VOICE_TEST_V1 " + command + "\n").encode("ascii"))
        self.port.flush()
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            line = self.line(deadline)
            if line is None:
                break
            prefix = "RODAK_VOICE_TEST_RESULT "
            if line.startswith(prefix):
                reply = json.loads(line[len(prefix):])
                # Serial output may still contain a reply for a previous command.  Do not
                # let that stale success authorize the command currently being waited for.
                if reply.get("command") != expected_command:
                    continue
                if not reply.get("ok"):
                    raise RuntimeError(f"Device rejected {expected_command}: {reply}")
                return reply
        raise TimeoutError(f"No diagnostic reply for {expected_command}")

    def observe(self, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            self.line(deadline)

    def wait_for(self, marker, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            line = self.line(deadline)
            if line is not None and marker in line:
                return
        raise TimeoutError(f"Missing device marker: {marker}")


def _voice_test_results(text):
    prefix = "RODAK_VOICE_TEST_RESULT "
    results = []
    for line_index, line in enumerate(text.splitlines()):
        if not line.startswith(prefix):
            continue
        try:
            payload = json.loads(line[len(prefix):])
        except json.JSONDecodeError:
            results.append({"line": line_index, "command": None, "ok": False})
            continue
        if isinstance(payload, dict):
            results.append({"line": line_index, **payload})
        else:
            results.append({"line": line_index, "command": None, "ok": False})
    return results


def validate_same_session_turns(text, turns):
    """Validate one synthetic same-session run without inferring success from a stats slice."""

    lines = text.splitlines()
    ready = []
    input_starts = []
    playback = []
    stats = []
    follow_up = []
    started = []
    stopped = []
    cleanup_started = []
    cleanup_destroyed = []
    rearmed = []
    for line_index, line in enumerate(lines):
        match = re.search(r"Realtime voice session ready: session=([^\s]+)", line)
        if match:
            ready.append((line_index, match.group(1)))
        match = re.search(r"Sent speech input start: session=([^\s]+)", line)
        if match:
            input_starts.append((line_index, match.group(1)))
        match = re.search(r"Playback started: playback_epoch=(\d+)", line)
        if match:
            playback.append((line_index, int(match.group(1))))
        if "Playback audio stats:" in line:
            fields = dict(re.findall(r"(packets|decoded_frames|pcm_bytes|write_failures)=(\d+)", line))
            stats.append((line_index, {key: int(value) for key, value in fields.items()}))
        match = re.search(r"Follow-up listening started: completed_turns=(\d+)", line)
        if match:
            follow_up.append((line_index, int(match.group(1))))
        if "Interaction stopped:" in line:
            stopped.append(line_index)
        if "Interaction started:" in line:
            started.append(line_index)
        if "Voice websocket cleanup started" in line:
            cleanup_started.append(line_index)
        if "Voice websocket destroy completed:" in line:
            cleanup_destroyed.append(line_index)
        if "Always-on wake monitoring armed" in line:
            rearmed.append(line_index)

    errors = []
    results = _voice_test_results(text)
    if len(ready) != 1:
        errors.append(f"expected one realtime ready event, got {len(ready)}")
    if len(stopped) != 1:
        errors.append(f"expected one interaction stop event, got {len(stopped)}")
    if len(started) != 1:
        errors.append(f"expected one interaction start event, got {len(started)}")
    if not ready:
        return {"passed": False, "errors": errors + ["missing realtime ready event"]}

    ready_line, session_id = ready[0]
    stop_line = stopped[0] if stopped else len(lines)
    inputs = [(line, value) for line, value in input_starts if ready_line <= line <= stop_line]
    plays = [(line, value) for line, value in playback if ready_line <= line <= stop_line]
    turn_stats = [(line, value) for line, value in stats if ready_line <= line <= stop_line]
    follows = [(line, value) for line, value in follow_up if ready_line <= line <= stop_line]
    if started and (
        started[0] <= ready_line
        or (inputs and started[0] <= inputs[0][0])
        or (plays and started[0] >= plays[0][0])
    ):
        errors.append("interaction start is not between the first input and playback")
    if len(inputs) != turns:
        errors.append(f"expected {turns} input.start events, got {len(inputs)}")
    if len(inputs) == turns and any(value != session_id for _, value in inputs):
        errors.append("input.start session id does not match the unique ready session")
    if len(plays) != turns:
        errors.append(f"expected {turns} playback start events, got {len(plays)}")
    if len(turn_stats) != turns:
        errors.append(f"expected {turns} playback stats events, got {len(turn_stats)}")
    if [value for _, value in follows] != list(range(1, turns)):
        errors.append("follow-up completed_turns sequence is not exactly 1..turns-1")
    if len(plays) == turns and any(plays[index][1] >= plays[index + 1][1] for index in range(turns - 1)):
        errors.append("playback epochs are not strictly increasing")

    for index in range(min(turns, len(plays), len(turn_stats), len(inputs))):
        if not inputs[index][0] < plays[index][0]:
            errors.append(f"turn {index + 1} playback started before its input.start")
        if not plays[index][0] < turn_stats[index][0]:
            errors.append(f"turn {index + 1} stats precede playback start")
        if index + 1 < turns and index < len(follows):
            if not turn_stats[index][0] < follows[index][0]:
                errors.append(f"turn {index + 1} follow-up started before playback stats")
            if index + 1 < len(plays) and not follows[index][0] < plays[index + 1][0]:
                errors.append(f"turn {index + 2} playback started before follow-up listening")

    command_results = [result for result in results if ready_line <= result["line"] <= stop_line]
    wake_results = [
        result
        for result in results
        if result["line"] < ready_line and result.get("command") == "wake" and result.get("ok") is True
    ]
    if len(wake_results) != 1:
        errors.append("wake command result is missing or not unique before session ready")
    for result in command_results:
        if result.get("ok") is False:
            errors.append(f"voice test command failed: {result.get('command')}")
    replay_results = [result for result in command_results if result.get("command") == "audio_replay"]
    if len(replay_results) != max(0, turns - 1) or not all(result.get("ok") is True for result in replay_results):
        errors.append("audio_replay command result count or status is invalid")
    stop_results = [
        result
        for result in results
        if ready_line <= result["line"] and result.get("command") == "stop" and result.get("ok") is True
    ]
    if len(stop_results) != 1 or stop_results[0]["line"] <= (turn_stats[-1][0] if turn_stats else ready_line):
        errors.append("stop command did not follow the final playback stats exactly once")
    if not cleanup_started or not cleanup_destroyed:
        errors.append("voice websocket cleanup markers are incomplete")
    if stopped and cleanup_destroyed and cleanup_destroyed[0] > stopped[0]:
        errors.append("interaction stopped before websocket destruction completed")
    if not rearmed or (stopped and rearmed[0] <= stopped[0]):
        errors.append("wake monitoring was not rearmed after interaction stop")
    clear_results = [
        result
        for result in results
        if ready_line <= result["line"] and result.get("command") == "audio_clear" and result.get("ok") is True
    ]
    if len(clear_results) != 1 or not rearmed or clear_results[0]["line"] <= rearmed[0]:
        errors.append("audio_clear did not complete after wake rearm exactly once")
    for index, (_, values) in enumerate(turn_stats, start=1):
        required = ("packets", "decoded_frames", "pcm_bytes", "write_failures")
        if any(key not in values for key in required):
            errors.append(f"turn {index} playback stats are incomplete")
        elif not all(values[key] > 0 for key in required[:3]) or values["write_failures"] != 0:
            errors.append(f"turn {index} playback stats report failure or empty audio")
    return {
        "passed": not errors,
        "errors": errors,
        "session_id": session_id,
        "ready_events": len(ready),
        "input_start_events": len(inputs),
        "playback_start_events": len(plays),
        "playback_stats_events": len(turn_stats),
        "follow_up_events": [value for _, value in follows],
    }


def validate_cycle_sessions(text, cycles, *, expected_interruptions=None):
    """Keep each software wake's playback and cleanup inside its own serial window."""
    lines = text.splitlines()
    results = _voice_test_results(text)
    beginnings = [result["line"] for result in results
                  if result.get("command") == "audio_begin" and result.get("ok") is True]
    errors = []
    sessions = []
    if len(beginnings) != cycles:
        errors.append(f"expected {cycles} successful audio_begin results, got {len(beginnings)}")
    if any(result.get("ok") is not True for result in results):
        errors.append("voice test command failed or returned a malformed result")
    if not beginnings:
        return {"passed": False, "errors": errors, "sessions": sessions}
    if any("Interaction started:" in line or "Interaction stopped:" in line
           for line in lines[:beginnings[0]]):
        errors.append("unowned interaction before the first software wake")

    # Each CLI cycle uploads once before sending wake. Use that reply as its boundary:
    # the asynchronous voice task may log ready/start before the later wake ACK prints.
    for cycle, beginning in enumerate(beginnings):
        end = beginnings[cycle + 1] if cycle + 1 < len(beginnings) else len(lines)
        window = lines[beginning:end]
        def matches(pattern):
            return [(index + beginning, match) for index, line in enumerate(window)
                    if (match := re.search(pattern, line)) is not None]

        ready = matches(r"Realtime voice session ready: session=([^\s]+)")
        inputs = matches(r"Sent speech input start: session=([^\s]+)")
        started = matches(r"Interaction started:.*focus_token=(\d+)")
        stopped = matches(r"Interaction stopped:.*focus_token=(\d+)")
        plays = matches(r"Playback started: playback_epoch=(\d+)")
        stats = matches(r"Playback audio stats:(.*)")
        cleanup = matches(r"Voice websocket cleanup started")
        destroyed = matches(r"Voice websocket destroy completed: result=(\S+)")
        rearmed = matches(r"Always-on wake monitoring armed")
        cycle_errors = []
        for name, events in (("ready", ready), ("interaction start", started),
                             ("interaction stop", stopped), ("cleanup start", cleanup),
                             ("websocket destroy", destroyed)):
            if len(events) != 1:
                cycle_errors.append(f"expected one {name} event, got {len(events)}")
        if not inputs or not plays or not stats:
            cycle_errors.append("missing speech input, playback start or non-empty playback stats")
        if len(ready) == 1 and any(match[1] != ready[0][1][1] for _, match in inputs):
            cycle_errors.append("input.start session id differs from this cycle's ready session")
        if len(started) == 1 and len(stopped) == 1 and started[0][1][1] != stopped[0][1][1]:
            cycle_errors.append("interaction stop focus token differs from its start")
        if len(destroyed) == 1 and destroyed[0][1][1] != "ESP_OK":
            cycle_errors.append("websocket destruction did not succeed")
        if all(len(events) == 1 for events in (ready, started, stopped, cleanup, destroyed)):
            # FinishInteraction waits for transport close and I/O retirement before logging
            # focus release. Startup and playback logs come from different tasks.
            if not cleanup[0][0] < destroyed[0][0] < stopped[0][0]:
                cycle_errors.append("session cleanup/stop markers are out of order")
            if not any(index > stopped[0][0] for index, _ in rearmed):
                cycle_errors.append("wake monitoring was not rearmed after this cycle stopped")
            if any(not ready[0][0] < index < stopped[0][0] for index, _ in plays + stats):
                cycle_errors.append("playback evidence falls outside this interaction")
        if any(int(plays[index][1][1]) >= int(plays[index + 1][1][1])
               for index in range(len(plays) - 1)):
            cycle_errors.append("playback epochs are not strictly increasing within this cycle")
        for index, match in stats:
            fields = {key: int(value) for key, value in re.findall(
                r"(packets|decoded_frames|pcm_bytes|write_failures)=(\d+)", match[1])}
            if (not all(fields.get(key, 0) > 0 for key in ("packets", "decoded_frames", "pcm_bytes"))
                    or fields.get("write_failures", 1) != 0):
                cycle_errors.append("playback stats report failure, empty audio or missing fields")
            if not any(play_index < index for play_index, _ in plays):
                cycle_errors.append("playback stats precede this cycle's playback start")
        stops = [result for result in results if beginning <= result["line"] < end
                 and result.get("command") == "stop" and result.get("ok") is True]
        wakes = [result for result in results if beginning <= result["line"] < end
                 and result.get("command") == "wake" and result.get("ok") is True]
        if len(wakes) != 1:
            cycle_errors.append("missing unique successful wake command in this upload window")
        interruption_counts = {
            name: "\n".join(window).count(marker) for name, marker in (
                ("vad_interruptions", "TTS interrupted:"),
                ("afe_vad_confirmations", "AFE VAD confirmed:"),
                ("vad_ends", "VAD end sent:"),
            )
        }
        if expected_interruptions is not None:
            for name, count in interruption_counts.items():
                if count != expected_interruptions:
                    cycle_errors.append(f"expected {expected_interruptions} {name}, got {count}")
        # A follow-up timeout can finish the interaction before the explicit stop ACK.
        if len(stops) != 1 or (ready and stops[0]["line"] <= ready[0][0]):
            cycle_errors.append("missing unique successful stop command after session ready")
        sessions.append({"cycle": cycle + 1, "session_id": ready[0][1][1] if len(ready) == 1 else None,
                         "playback_start_events": len(plays), "playback_stats_events": len(stats),
                         "stop_line": stopped[0][0] if len(stopped) == 1 else None,
                         "rearm_lines": [index for index, _ in rearmed],
                         **interruption_counts,
                         "passed": not cycle_errors, "errors": cycle_errors})
        errors.extend(f"cycle {cycle + 1}: {error}" for error in cycle_errors)

    session_ids = [session["session_id"] for session in sessions if session["session_id"] is not None]
    if len(session_ids) != len(set(session_ids)):
        errors.append("software wake cycles reused a realtime session id")
    clears = [result for result in results
              if result.get("command") == "audio_clear" and result.get("ok") is True]
    last = sessions[-1]
    if (len(clears) != 1 or last["stop_line"] is None
            or not any(last["stop_line"] < index < clears[0]["line"] for index in last["rearm_lines"])):
        errors.append("audio_clear did not complete once after the final stop and wake rearm")
    return {"passed": not errors, "errors": errors, "sessions": sessions}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--wav", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    cycles_group = parser.add_mutually_exclusive_group()
    cycles_group.add_argument("--cycles", type=int, default=None)
    cycles_group.add_argument(
        "--turns",
        type=int,
        default=None,
        help="Keep one realtime voice session and replay the fixture for this many turns",
    )
    parser.add_argument("--seconds", type=float, default=45)
    parser.add_argument("--lead-in-ms", type=int, default=1000,
                        help="Silent pre-roll lets AFE and WebSocket startup settle")
    parser.add_argument("--barge-in", action="store_true",
                        help="Replay the fixture during real TTS; require AFE VAD interruption")
    parser.add_argument("--live-mic-playback", action="store_true",
                        help="Restore physical microphones at TTS start; require no local interruption")
    parser.add_argument("--interruptions", type=int, default=1,
                        help="Repeated playback interruptions in each session (1-3)")
    parser.add_argument("--late-follow-up", action="store_true",
                        help="Replay at 28 seconds of follow-up; require a reply and eventual idle timeout")
    args = parser.parse_args()
    if args.barge_in and args.live_mic_playback:
        parser.error("barge-in injection and live-mic playback are separate tests")
    if args.late_follow_up and (args.barge_in or args.live_mic_playback):
        parser.error("late follow-up must run separately from playback interruption tests")
    cycles = 2 if args.cycles is None and args.turns is None else args.cycles
    turns = args.turns
    if turns is not None and (args.barge_in or args.live_mic_playback or args.late_follow_up):
        parser.error("turns must run alone; use cycles for barge-in, live-mic, or late follow-up")
    if turns is not None and args.interruptions != 1:
        parser.error("interruptions is only valid with barge-in cycles")
    if cycles is not None and cycles < 1:
        parser.error("cycles must be positive")
    if turns is not None and turns < 1:
        parser.error("turns must be positive")
    if (args.seconds <= 0 or not math.isfinite(args.seconds)
            or not 0 <= args.lead_in_ms <= 5000 or not 1 <= args.interruptions <= 3):
        parser.error("cycles/turns and seconds must be positive")
    pcm = bytes(args.lead_in_ms * 32) + load_pcm(args.wav)
    if len(pcm) > 320000:
        parser.error("Audio including silent pre-roll must fit within ten seconds")
    if serial is None:
        raise RuntimeError("pyserial is required for serial voice tests")
    args.log.parent.mkdir(parents=True, exist_ok=True)
    error = None
    completed = 0
    summary_completed_turns = 0
    with args.log.open("wb") as log:
        port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
        port.port = args.port
        port.dtr = False
        port.rts = False
        try:
            port.open()
            session = Session(port, log)
            session.observe(2)
            if turns is not None:
                print(f"Same session: uploading synthetic microphone input for {turns} turns", flush=True)
                session.command(f"audio_begin {len(pcm) // 2}")
                for offset in range(0, len(pcm), 512):
                    session.command(f"audio_chunk {offset // 2} {pcm[offset:offset + 512].hex()}")
                session.command("wake")
                for turn in range(turns):
                    session.wait_for("Playback started: playback_epoch=", 90)
                    session.wait_for("Playback audio stats:", 90)
                    summary_completed_turns = turn + 1
                    if turn + 1 < turns:
                        session.wait_for("Follow-up listening started:", 90)
                        session.command("audio_replay")
                session.command("stop")
                session.observe(5)
                completed = 1
            else:
                for cycle in range(cycles or 0):
                    print(f"Cycle {cycle + 1}: uploading synthetic microphone input", flush=True)
                    session.command(f"audio_begin {len(pcm) // 2}")
                    for offset in range(0, len(pcm), 512):
                        session.command(f"audio_chunk {offset // 2} {pcm[offset:offset + 512].hex()}")
                    session.command("wake")
                    if args.live_mic_playback:
                        session.wait_for("Playback started: playback_epoch=", 45)
                        session.command("audio_live")
                    if args.barge_in:
                        for interruption in range(args.interruptions):
                            session.wait_for("Playback started: playback_epoch=", 45)
                            session.observe(0.4)
                            session.command("audio_replay")
                            session.wait_for("TTS interrupted:", 15)
                    if args.late_follow_up:
                        session.wait_for("Playback started: playback_epoch=", 45)
                        session.wait_for("Follow-up listening started:", 90)
                        session.observe(28)
                        session.command("audio_replay")
                        session.wait_for("Playback started: playback_epoch=", 45)
                        session.wait_for("Follow-up listening started:", 90)
                        session.wait_for("Follow-up window timed out", 35)
                        session.observe(5)
                    else:
                        session.observe(args.seconds)
                    session.command("stop")
                    session.observe(5)
                    completed += 1
            session.command("audio_clear")
        except Exception as exc:
            error = f"{type(exc).__name__}: {exc}"
            if port.is_open:
                try:
                    session.command("stop")
                    session.observe(5)
                    session.command("audio_clear")
                except Exception:
                    pass
        finally:
            port.close()
    text = args.log.read_text(encoding="utf-8", errors="replace")
    input_sessions = re.findall(r"Sent speech input start: session=([^\s]+)", text)
    ready_sessions = re.findall(r"Realtime voice session ready: session=([^\s]+)", text)
    follow_up_turns = [int(value) for value in re.findall(
        r"Follow-up listening started: completed_turns=(\d+)", text)]
    summary = {"synthetic_microphone": True, "acoustic_wake_test": False,
               "late_follow_up": args.late_follow_up,
               "physical_microphones_during_playback": args.live_mic_playback,
               "same_session_turns": turns,
               "expected_turns": turns if turns is not None else cycles,
               "completed_cycles": completed, "error": error,
               **parse_log(args.log.read_bytes())}
    summary["completed_turns"] = summary_completed_turns if turns is not None else None
    summary["speech_input_start_sessions"] = input_sessions
    summary["voice_ready_sessions"] = ready_sessions
    summary["follow_up_completed_turns"] = follow_up_turns
    summary["vad_interruptions"] = text.count("TTS interrupted:")
    summary["afe_vad_confirmations"] = text.count("AFE VAD confirmed:")
    summary["vad_ends"] = text.count("VAD end sent:")
    turn_trace = validate_same_session_turns(text, turns) if turns is not None else None
    summary["same_session_trace"] = turn_trace
    cycle_trace = validate_cycle_sessions(
        text, cycles or 0,
        expected_interruptions=args.interruptions if args.barge_in else None,
    ) if turns is None else None
    summary["cycle_session_trace"] = cycle_trace
    stats = []
    for line in text.splitlines():
        if "Playback audio stats:" in line:
            fields = dict(re.findall(r"(packets|decoded_frames|pcm_bytes|write_failures)=(\d+)", line))
            if fields:
                stats.append({k: int(v) for k, v in fields.items()})
    summary["playback_audio_stats"] = stats
    same_session_gate = turns is not None and (
        not error
        and not summary["failure"]
        and not summary["failure_flags"]["reset"]
        and len(input_sessions) == turns
        and len(set(input_sessions)) == 1
        and len(ready_sessions) == 1
        and ready_sessions[0] == input_sessions[0]
        and follow_up_turns == list(range(1, turns))
        and summary["completed_turns"] == turns
        and bool(turn_trace and turn_trace["passed"])
    )
    cycle_gate = turns is None and (
        not error and not summary["failure"]
        and not summary["failure_flags"]["reset"]
        and completed == cycles
        and bool(cycle_trace and cycle_trace["passed"])
        and summary["interaction_started"] >= (cycles or 0)
        and summary["interaction_stopped"] >= (cycles or 0)
        and (not args.live_mic_playback or summary["vad_interruptions"] == 0)
        and (not args.barge_in or (
            summary["vad_interruptions"] == (cycles or 0) * args.interruptions
            and summary["afe_vad_confirmations"] == (cycles or 0) * args.interruptions
            and summary["vad_ends"] == (cycles or 0) * args.interruptions))
        and len(stats) >= (cycles or 0)
        and all(s.get("packets", 0) > 0 and s.get("decoded_frames", 0) > 0
                and s.get("pcm_bytes", 0) > 0 and s.get("write_failures", 1) == 0 for s in stats)
    )
    summary["session_gate_passed"] = same_session_gate or cycle_gate
    args.log.with_suffix(".summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0 if summary["session_gate_passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
