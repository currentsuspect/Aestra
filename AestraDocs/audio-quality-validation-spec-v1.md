# Aestra Audio Quality Validation Spec v1

## Goal

Verify that Aestra preserves the highest possible audio fidelity from:

**Input → Recording → Processing → Mixing → Master Bus → Export**

with no unintended degradation, distortion, clipping, timing errors, or realtime instability.

---

# 1. Core Audio Engine Integrity

## Sample accuracy

Test:

* 44.1 kHz
* 48 kHz
* 88.2 kHz
* 96 kHz
* 192 kHz (if supported)

Verify:

* No hidden resampling
* Engine sample rate matches project rate
* Plugins receive correct sample rate
* Export preserves requested sample rate

Pass criteria:

```text
Input sine wave:
-1 dBFS @ 1kHz

Output:

Amplitude difference < 0.001 dB
Frequency deviation < 0.001%
```

---

# 2. Bit Depth Handling

Test:

Import:

* 16-bit WAV
* 24-bit WAV
* 32-bit float WAV

Process:

* Gain +20dB
* Gain -20dB
* Normalize
* Export

Verify:

* No truncation
* No accidental integer processing
* No unnecessary dithering

Preferred internal pipeline:

```text
Everything:
32-bit float minimum

Better:
64-bit float mixing engine
```

---

# 3. Summing Engine Test

This is huge.

Create:

100 identical sine tracks:

```text
1kHz
-20dBFS
```

Expected:

Digital summing should equal mathematical addition.

Compare:

Aestra output

vs

offline Python/Numpy reference summation

Pass:

Difference:

```text
< -140dB RMS error
```

---

# 4. Pan Law Validation

Test:

Mono signal:

```text
-6dBFS pink noise
```

Pan:

Center → Left → Right

Measure:

Center energy vs side energy.

Supported laws:

-3dB
-4.5dB
-6dB

Verify user setting matches actual math.

---

# 5. Gain Stage Accuracy

Test:

Audio through:

```text
Track gain
Clip gain
Plugin gain
Bus gain
Master gain
```

Compare:

Expected:

```text
Output = Input × gain coefficient
```

No:

* rounding errors
* hidden saturation
* accidental clipping

---

# 6. Mixer Headroom Test

Push:

```text
200 tracks
each -3dBFS
```

Expected:

Mixer internally handles overflow.

No:

* wraparound
* integer clipping
* distortion

Check:

```text
Internal peak > 0dBFS
```

should be okay.

Only final output clips.

---

# 7. Plugin Processing Validation

For every plugin:

Test:

Bypass:

```text
Input → Plugin bypass
```

must equal:

```text
Input → no plugin
```

Difference:

< -120dB

Latency:

Measure:

Impulse:

```text
single sample spike
```

Track:

Input position

vs

Output position

Report:

```text
Plugin latency: X samples
```

---

# 8. Real-Time vs Offline Rendering

Critical.

Render same project:

Realtime:

```text
Play → Record output
```

Offline:

```text
Export
```

Compare:

Bit identical.

Expected:

```text
Difference = 0
```

or negligible floating point noise.

---

# 9. Automation Accuracy

Test:

Volume automation:

```text
-∞ → 0dB
```

Fast:

```text
10ms ramp
```

Slow:

```text
10 seconds
```

Check:

No:

* zipper noise
* clicks
* jumps

---

# 10. Recording Path Test

Input:

Audio interface loopback.

Record:

1kHz sine.

Verify:

Latency:

```text
input timestamp
-
record timestamp
```

Measure:

* buffer latency
* compensation accuracy

---

# 11. Export Validation

Formats:

WAV:

* 16 bit
* 24 bit
* 32 float

FLAC

MP3/AAC if supported

Verify:

Metadata:

* sample rate
* channels
* bit depth

---

# 12. Noise Floor Test

Create silence project:

No tracks.

Export:

Measure:

Noise floor.

Expected:

```text
Below -140dBFS
```

---

# 13. CPU Stress Audio Stability

Run:

100+ tracks

Heavy plugins.

Monitor:

* buffer underruns
* dropouts
* glitches

Required:

Realtime audio thread:

NEVER:

* allocate memory
* lock mutexes
* do filesystem work

---

# 14. Denormal Handling

Test:

Very quiet signals:

```text
-300dB sine
```

Run plugins.

Measure:

CPU spikes.

Must have:

```text
Denormal protection enabled
```

---

# 15. Golden Reference Test Suite

Create:

```text
tests/audio/reference/
```

Containing:

* sine waves
* impulses
* pink noise
* white noise
* complex music stems

Every engine change runs:

```text
Aestra Audio Regression Suite
```

Compare:

```text
new.wav
reference.wav
```

Using:

RMS error

Peak error

Frequency spectrum difference

---

# Final "Aestra Audio Grade"

Score:

## S Tier

* 64-bit float engine
* sample accurate automation
* zero-copy audio paths
* deterministic offline render
* perfect latency compensation
* no realtime thread violations

## A Tier

* 32-bit float engine
* correct summing
* reliable exports
* good plugin handling

## Below

Anything with:

* hidden clipping
* resampling surprises
* inconsistent offline rendering
* realtime glitches

---

*Spec prepared as an engineering checklist for systematic audio quality validation.
Companion to `AestraDocs/audio-quality-audit-2026-05.md` (current audit findings).*
