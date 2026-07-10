#!/usr/bin/env python3
"""
kokoro_worker — persistent Kokoro neural TTS (stdin line → stdout raw s16le PCM).

Mirrors the Piper CLI contract so Polymath's TtsPiper driver can talk to either:
  * one UTF-8 text line on stdin (terminated by \\n)
  * raw little-endian int16 mono PCM on stdout for that utterance
  * idle gap after last byte → utterance complete (driver times out)

Usage:
  python kokoro_worker.py --model kokoro-v1.0.onnx --voices voices-v1.0.bin \\
      --voice af_sky --sample-rate 24000

Env (optional):
  KOKORO_VOICE, KOKORO_SPEED
"""
from __future__ import annotations

import argparse
import os
import struct
import sys
import traceback


def eprint(*args: object) -> None:
    print(*args, file=sys.stderr, flush=True)


def float_to_s16le(samples) -> bytes:
    # samples: numpy float32 array in [-1, 1]
    import numpy as np

    clipped = np.clip(samples, -1.0, 1.0)
    pcm = (clipped * 32767.0).astype(np.int16)
    return pcm.tobytes()


def main() -> int:
    ap = argparse.ArgumentParser(description="Kokoro persistent TTS worker")
    ap.add_argument("--model", required=True, help="Path to kokoro-v1.0.onnx")
    ap.add_argument("--voices", required=True, help="Path to voices-v1.0.bin")
    ap.add_argument("--voice", default=os.environ.get("KOKORO_VOICE", "af_sky"))
    ap.add_argument("--speed", type=float, default=float(os.environ.get("KOKORO_SPEED", "1.0")))
    ap.add_argument("--lang", default="en-us")
    ap.add_argument("--sample-rate", type=int, default=24000,
                    help="Reported rate (Kokoro is 24 kHz; kept for logs)")
    args = ap.parse_args()

    try:
        from kokoro_onnx import Kokoro
    except ImportError:
        eprint("kokoro-onnx not installed. Run setup-kokoro.ps1 first.")
        return 2

    if not os.path.isfile(args.model):
        eprint(f"model not found: {args.model}")
        return 2
    if not os.path.isfile(args.voices):
        eprint(f"voices not found: {args.voices}")
        return 2

    eprint(f"kokoro_worker: loading model={args.model} voice={args.voice}")
    try:
        kokoro = Kokoro(args.model, args.voices)
    except Exception:
        eprint("kokoro_worker: failed to load model")
        traceback.print_exc(file=sys.stderr)
        return 3

    eprint(f"kokoro_worker: ready voice={args.voice} sr={args.sample_rate}")
    # Signal readiness (driver ignores stderr; useful for logs).
    sys.stderr.flush()

    # Binary stdout for PCM.
    if hasattr(sys.stdout, "buffer"):
        out = sys.stdout.buffer
    else:
        out = sys.stdout

    # Text stdin.
    if hasattr(sys.stdin, "buffer"):
        # Read text lines decoded as UTF-8.
        stdin = sys.stdin
    else:
        stdin = sys.stdin

    voice = args.voice
    speed = args.speed

    while True:
        try:
            line = stdin.readline()
        except Exception:
            break
        if line == "":
            break  # EOF
        text = line.strip()
        if not text:
            continue

        # Optional inline control:  !voice=af_bella
        if text.startswith("!voice="):
            voice = text.split("=", 1)[1].strip() or voice
            eprint(f"kokoro_worker: voice -> {voice}")
            continue
        if text.startswith("!speed="):
            try:
                speed = float(text.split("=", 1)[1].strip())
            except ValueError:
                pass
            continue

        try:
            samples, sr = kokoro.create(text, voice=voice, speed=speed, lang=args.lang)
            raw = float_to_s16le(samples)
            out.write(raw)
            out.flush()
            eprint(f"kokoro_worker: synth {len(text)} chars -> {len(raw)} bytes @{sr}Hz")
        except Exception:
            eprint(f"kokoro_worker: synth failed for {text[:80]!r}")
            traceback.print_exc(file=sys.stderr)
            # Emit a short silence so the driver doesn't hang forever waiting.
            out.write(struct.pack("<" + "h" * 240, *([0] * 240)))
            out.flush()

    eprint("kokoro_worker: exit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
