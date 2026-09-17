#!/usr/bin/env python3
"""Exercise physical-device voice sessions with explicitly injected PCM, not acoustic wake."""

import argparse
import json
import math
import re
from pathlib import Path
import time
import wave

import serial

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
                if not reply.get("ok"):
                    raise RuntimeError(f"Device rejected {command.split()[0]}: {reply}")
                return
        raise TimeoutError(f"No diagnostic reply for {command.split()[0]}")

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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--wav", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--cycles", type=int, default=2)
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
    if (args.cycles < 1 or args.seconds <= 0 or not math.isfinite(args.seconds)
            or not 0 <= args.lead_in_ms <= 5000 or not 1 <= args.interruptions <= 3):
        parser.error("cycles and seconds must be positive")
    pcm = bytes(args.lead_in_ms * 32) + load_pcm(args.wav)
    if len(pcm) > 320000:
        parser.error("Audio including silent pre-roll must fit within ten seconds")
    args.log.parent.mkdir(parents=True, exist_ok=True)
    error = None
    completed = 0
    with args.log.open("wb") as log:
        port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
        port.port = args.port
        port.dtr = False
        port.rts = False
        try:
            port.open()
            session = Session(port, log)
            session.observe(2)
            for cycle in range(args.cycles):
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
    summary = {"synthetic_microphone": True, "acoustic_wake_test": False,
               "late_follow_up": args.late_follow_up,
               "physical_microphones_during_playback": args.live_mic_playback,
               "completed_cycles": completed, "error": error,
               **parse_log(args.log.read_bytes())}
    text = args.log.read_text(encoding="utf-8", errors="replace")
    summary["vad_interruptions"] = text.count("TTS interrupted:")
    summary["afe_vad_confirmations"] = text.count("AFE VAD confirmed:")
    summary["vad_ends"] = text.count("VAD end sent:")
    stats = []
    for line in text.splitlines():
        if "Playback audio stats:" in line:
            fields = dict(re.findall(r"(packets|decoded_frames|pcm_bytes|write_failures)=(\d+)", line))
            if fields:
                stats.append({k: int(v) for k, v in fields.items()})
    summary["playback_audio_stats"] = stats
    summary["session_gate_passed"] = (
        not error and not summary["failure"]
        and not summary["failure_flags"]["reset"]
        and summary["interaction_started"] >= args.cycles
        and summary["interaction_stopped"] >= args.cycles
        and (not args.live_mic_playback or summary["vad_interruptions"] == 0)
        and (not args.barge_in or (
            summary["vad_interruptions"] == args.cycles * args.interruptions
            and summary["afe_vad_confirmations"] == args.cycles * args.interruptions
            and summary["vad_ends"] == args.cycles * args.interruptions))
        and len(stats) >= args.cycles
        and all(s.get("packets", 0) > 0 and s.get("decoded_frames", 0) > 0
                and s.get("pcm_bytes", 0) > 0 and s.get("write_failures", 1) == 0 for s in stats)
    )
    args.log.with_suffix(".summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0 if summary["session_gate_passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
