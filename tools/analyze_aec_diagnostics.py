#!/usr/bin/env python3
"""Report captured signal levels and lag correlation; not an acoustic acceptance score."""

import argparse
import json
from pathlib import Path
import wave

import numpy as np


def read(path):
    with wave.open(str(path), "rb") as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate()) != (1, 2, 16000):
            raise ValueError("Expected mono PCM16 at 16 kHz")
        return np.frombuffer(source.readframes(source.getnframes()), dtype="<i2").astype(float)


def describe(samples):
    rms = float(np.sqrt(np.mean(samples * samples)))
    return {"samples": len(samples), "rms": round(rms, 2),
            "rms_dbfs": round(20 * np.log10(max(rms, 1e-9) / 32768), 2),
            "peak": int(np.max(np.abs(samples))),
            "clipped_fraction": round(float(np.mean(np.abs(samples) >= 32760)), 6)}


def correlation(reference, signal):
    # Decimate only for a bounded diagnostic lag search; keep original WAVs for further analysis.
    a, b = reference[::8], signal[::8]
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    best = (0.0, 0)
    for lag in range(-300, 301):
        x, y = (a[:n-lag], b[lag:]) if lag >= 0 else (a[-lag:], b[:n+lag])
        if len(x) < 200 or np.std(x) < 1e-6 or np.std(y) < 1e-6:
            continue
        score = float(np.corrcoef(x, y)[0, 1])
        if abs(score) > abs(best[0]):
            best = score, lag
    return {"coefficient": round(best[0], 4), "signal_lag_ms": best[1] * 0.5}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    names = [f"tdm-slot-{i}" for i in range(4)] + ["afe-output"]
    audio = {name: read(args.directory / f"{name}.wav") for name in names}
    metadata = json.loads((args.directory / "metadata.json").read_text(encoding="utf-8"))
    report = {"metadata": metadata, "levels": {k: describe(v) for k, v in audio.items()},
              "reference_slot_1_correlation": {
                  k: correlation(audio["tdm-slot-1"], v) for k, v in audio.items()
                  if k != "tdm-slot-1"},
              "note": "AFE/raw start times are independent software estimates; RMS differences are not ERLE."}
    (args.directory / "analysis.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
