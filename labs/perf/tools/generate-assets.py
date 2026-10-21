#!/usr/bin/env python3
# © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#
# Deterministic asset generator for the Folio baseline fixture.
#
# Writes short, original, copyright-clean WAV assets. Every parameter that could
# vary is fixed here and recorded in the manifest the validator checks:
# sample rate, channel count, sample count, amplitude, phase, waveform recipe,
# and the seed for the one asset containing pseudo-random content.
#
# WHY AN EXPLICIT LCG RATHER THAN random.seed()
#
# CPython's Mersenne Twister is stable in practice, but "stable in practice
# across the versions we happened to run" is not a reproducibility guarantee an
# apparatus should rest on. The generator owns its number source so that byte
# identity depends on this file alone, not on the interpreter that ran it.
#
# WHY SHORT ASSETS
#
# The measurement window is 120 s, but the assets are not. Short clips repeated
# across the arrangement exercise decoding, clip scheduling and simultaneous
# playback while keeping the repository small. A 120-second asset would prove
# nothing extra and cost tens of megabytes.

import argparse
import math
import os
import struct
import wave

SAMPLE_RATE = 48000
CHANNELS = 2
SAMPLE_WIDTH = 2  # 16-bit PCM
FULL_SCALE = 32767


class Lcg:
    """Numerical Recipes LCG. Fixed constants, fixed seed, reproducible anywhere."""

    def __init__(self, seed):
        self.state = seed & 0xFFFFFFFF

    def next_unit(self):
        # Returns a float in [-1, 1).
        self.state = (1664525 * self.state + 1013904223) & 0xFFFFFFFF
        return (self.state / 2147483648.0) - 1.0


def write_wav(path, frames):
    """frames: list of (left, right) floats in [-1, 1]."""
    with wave.open(path, "wb") as handle:
        handle.setnchannels(CHANNELS)
        handle.setsampwidth(SAMPLE_WIDTH)
        handle.setframerate(SAMPLE_RATE)
        payload = bytearray()
        for left, right in frames:
            l = max(-1.0, min(1.0, left))
            r = max(-1.0, min(1.0, right))
            payload += struct.pack("<hh", int(l * FULL_SCALE), int(r * FULL_SCALE))
        handle.writeframes(bytes(payload))


def make_kick(duration_s=0.320, amplitude=0.85):
    """Percussive transient: exponentially decaying pitch sweep, zero start phase."""
    n = int(SAMPLE_RATE * duration_s)
    out = []
    for i in range(n):
        t = i / SAMPLE_RATE
        # 110 Hz -> 42 Hz sweep, decaying amplitude. Deterministic, no noise.
        freq = 42.0 + (110.0 - 42.0) * math.exp(-t * 28.0)
        env = math.exp(-t * 9.0)
        value = amplitude * env * math.sin(2.0 * math.pi * freq * t)
        out.append((value, value))
    return out


def make_chord(duration_s=2.000, amplitude=0.28):
    """Sustained tonal content: fixed partials, fixed phases, slight L/R detune."""
    n = int(SAMPLE_RATE * duration_s)
    partials = [(220.0, 1.00), (277.18, 0.72), (329.63, 0.55), (440.0, 0.34)]
    out = []
    for i in range(n):
        t = i / SAMPLE_RATE
        # Short fade in/out so repeated clips do not click at boundaries.
        env = min(1.0, t / 0.010, (duration_s - t) / 0.010)
        left = right = 0.0
        for freq, weight in partials:
            left += weight * math.sin(2.0 * math.pi * freq * t)
            right += weight * math.sin(2.0 * math.pi * (freq * 1.002) * t)
        norm = sum(w for _, w in partials)
        out.append((amplitude * env * left / norm, amplitude * env * right / norm))
    return out


def make_texture(duration_s=1.500, amplitude=0.22, seed=20260805):
    """Filtered pseudo-random texture. Seeded LCG; one-pole low-pass, fixed state."""
    n = int(SAMPLE_RATE * duration_s)
    rng = Lcg(seed)
    out = []
    zl = zr = 0.0
    coeff = 0.18  # one-pole smoothing, fixed
    for i in range(n):
        t = i / SAMPLE_RATE
        env = min(1.0, t / 0.020, (duration_s - t) / 0.020)
        zl += coeff * (rng.next_unit() - zl)
        zr += coeff * (rng.next_unit() - zr)
        out.append((amplitude * env * zl, amplitude * env * zr))
    return out


ASSETS = [
    ("kick.wav", make_kick),
    ("chord.wav", make_chord),
    ("texture.wav", make_texture),
]


def main():
    parser = argparse.ArgumentParser(description="Generate Folio baseline assets.")
    parser.add_argument("--out", required=True, help="output directory (created if absent)")
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)
    for name, builder in ASSETS:
        frames = builder()
        path = os.path.join(args.out, name)
        write_wav(path, frames)
        print(f"  {name}: {len(frames)} frames, {SAMPLE_RATE} Hz, {CHANNELS}ch, "
              f"{SAMPLE_WIDTH * 8}-bit -> {os.path.getsize(path)} bytes")
    print(f"wrote {len(ASSETS)} asset(s) to {args.out}")


if __name__ == "__main__":
    main()
