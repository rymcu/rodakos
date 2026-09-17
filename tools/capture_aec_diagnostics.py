#!/usr/bin/env python3
"""Capture real TDM/AFE PCM; optional injected prompt ends before acoustic capture."""

import argparse
import json
from pathlib import Path
import time
import wave

import serial

from run_serial_voice_test import Session, load_pcm


def request(session, command):
    session.port.write(("RODAK_VOICE_TEST_V1 " + command + "\n").encode("ascii"))
    session.port.flush()
    payload = None
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        line = session.line(deadline)
        if line is None:
            break
        if line.startswith("RODAK_AEC_DATA "):
            payload = json.loads(line[len("RODAK_AEC_DATA "):])
        if line.startswith("RODAK_VOICE_TEST_RESULT "):
            result = json.loads(line[len("RODAK_VOICE_TEST_RESULT "):])
            if not result.get("ok"):
                raise RuntimeError(f"Device rejected {command}: {result}")
            return payload
    raise TimeoutError(f"No response to {command}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--duration-ms", type=int, default=6000)
    parser.add_argument("--wait-playback-seconds", type=int, default=180)
    parser.add_argument("--observe-after-capture-seconds", type=int, default=0)
    parser.add_argument("--prompt-wav", type=Path,
                        help="Optional software wake/prompt; physical microphones resume before capture")
    args = parser.parse_args()
    if (not 1000 <= args.duration_ms <= 6000 or args.wait_playback_seconds <= 0
            or not 0 <= args.observe_after_capture_seconds <= 60):
        parser.error("duration must be 1000..6000 ms and playback wait must be positive")
    args.output.mkdir(parents=True, exist_ok=True)
    with (args.output / "serial.log").open("wb") as log:
        port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
        port.port = args.port
        port.dtr = False
        port.rts = False
        port.open()
        session = Session(port, log)
        try:
            request(session, "aec_clear")
            if args.prompt_wav:
                pcm = bytes(32000) + load_pcm(args.prompt_wav)
                if len(pcm) > 320000:
                    raise ValueError("Prompt including pre-roll must fit within ten seconds")
                session.command(f"audio_begin {len(pcm) // 2}")
                for offset in range(0, len(pcm), 512):
                    session.command(f"audio_chunk {offset // 2} {pcm[offset:offset + 512].hex()}")
                session.command("wake")
                print("Test prompt sent; waiting for playback before restoring physical microphones.", flush=True)
            else:
                print("Ready: use real wake and ask for a long reply, then remain silent.", flush=True)
            session.wait_for("Playback started: playback_epoch=", args.wait_playback_seconds)
            if args.prompt_wav:
                session.command("audio_live")
            request(session, f"aec_arm {args.duration_ms}")
            print("Recording real microphones, reference and AFE output.", flush=True)
            session.observe(args.duration_ms / 1000 + 1)
            request(session, "aec_stop")
            session.observe(args.observe_after_capture_seconds)
            session.command("stop")
            session.observe(3)
            status = request(session, "aec_status")
            if not status or not status["raw_samples"] or not status["afe_samples"]:
                raise RuntimeError(f"Capture contains no usable PCM: {status}")
            status["prompt_injected"] = bool(args.prompt_wav)
            status["physical_microphones_during_capture"] = True
            (args.output / "metadata.json").write_text(
                json.dumps(status, indent=2), encoding="utf-8")
            for channel in range(5):
                total = status["raw_samples" if channel < 4 else "afe_samples"]
                name = f"tdm-slot-{channel}.wav" if channel < 4 else "afe-output.wav"
                with wave.open(str(args.output / name), "wb") as target:
                    target.setparams((1, 2, 16000, 0, "NONE", "not compressed"))
                    for offset in range(0, total, 256):
                        count = min(256, total - offset)
                        data = request(session, f"aec_read {channel} {offset} {count}")
                        if (not data or data["channel"] != channel or data["offset"] != offset
                                or data["samples"] != count):
                            raise RuntimeError("Mismatched diagnostic chunk")
                        pcm = bytes.fromhex(data["hex"])
                        if len(pcm) != count * 2:
                            raise RuntimeError("Truncated diagnostic chunk")
                        target.writeframesraw(pcm)
                print(f"Saved {name}: {total} samples", flush=True)
            request(session, "aec_clear")
        finally:
            try:
                request(session, "aec_stop")
            finally:
                try:
                    if args.prompt_wav:
                        session.command("audio_live")
                finally:
                    port.close()
    print("Capture complete; channels have independent sample origins in metadata.json.")


if __name__ == "__main__":
    main()
