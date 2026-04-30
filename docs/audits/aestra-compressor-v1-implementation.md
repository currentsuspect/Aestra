# Aestra Compressor V1 Implementation

Status: implemented for `com.Aestrastudios.comp`.

## Final V1 Control Surface

- Threshold
- Ratio
- Attack
- Release
- Makeup Gain
- Knee
- Mix
- Bypass
- Input Gain
- Output Gain
- Detector HPF

The user-facing name is now `Aestra Compressor`. The plugin ID remains `com.Aestrastudios.comp` and is the compatibility anchor.

## Deprecated And Hidden Parameters

The old broad compressor state included detector mode, topology, hold, auto release, range, lookahead, stereo link, link law, sidechain HPF/LPF/listen, output trim, style, quality, and hidden soft clipping behavior.

V1 hides or ignores the non-V1 controls:

- Detector mode: V1 uses one clear peak detector.
- Topology: V1 is feed-forward only.
- Hold, auto release, range: ignored.
- Lookahead: ignored; V1 latency is zero samples.
- Stereo link/link law: fixed linked stereo detection.
- SC LPF and SC listen: ignored.
- Style and quality: ignored.
- Soft clipping/saturation: removed from the compressor path.

Old SC HPF maps to the V1 Detector HPF because it is internal detector filtering, not external sidechain UX.

## DSP Model

The V1 compressor is a scalar zero-latency feed-forward compressor:

- input sanitization for NaN/Inf/extreme samples;
- input gain before detection and compression;
- linked stereo peak detection using the maximum detector level;
- optional one-pole detector HPF with coefficients updated outside the sample loop;
- sample-rate-derived attack/release envelope;
- threshold/ratio/knee gain computer in dB;
- makeup gain after gain reduction;
- dry/wet mix;
- output gain after mix;
- denormal flushing on output.

`process()` performs no vector resizing, state serialization, logging, locks, or heap allocation. The previous `RMSDetector::setWindowSize()` process-path allocation risk was removed with the RMS detector.

## State Compatibility

Current saves use the existing compressor V2 magic with a V3 version field and the legacy 22-float storage shape. V1 parameters roundtrip through this blob.

Old V1 blobs load the original first 8 controls and default the new V1 input/output/Detector HPF controls. Old V2 blobs load the first 8 controls, map old output trim to Output Gain, map old SC HPF to Detector HPF, and ignore deprecated behavior fields. Invalid or truncated blobs fail without mutating into an unsafe state.

## Tests

Updated tests:

- `AestraCompPhase0Test`: V1 DSP behavior.
- `AestraCompPhase1Test`: public parameter surface, clamping, state, invalid state, and process-path buffer-risk contract.
- `AestraCompUpgradeTest`: old V1/V2 blob compatibility and plugin identity.

Covered behavior includes silence, bypass parity, static gain reduction, hard/soft knee behavior, attack, release, 44.1/48/96 kHz consistency, mix positions, input/output/makeup gain behavior, linked stereo imaging, NaN/Inf/extreme sample handling, normalized clamping, state roundtrip, and old blob loading.

## Material Lab

Baseline files:

- `labs/compressor/quality/compressor_quality_baseline.md`
- `labs/compressor/quality/compressor_quality_baseline.json`

Materials include silence, sine tone, bass pulse, transient/snare, vocal-ish sustain, chord/pad, simple mix bus, and an extreme sweep case.

## Known Limitations

- No external sidechain UX.
- No lookahead.
- No saturation, clipper, or limiter.
- No modes, style, or quality choices.
- No auto gain.
- No advanced stereo link controls.
- No transfer curve display.
- No custom premium UI in this pass.

## Deferred Features

- Modes.
- Auto gain.
- External sidechain UX.
- Lookahead.
- Saturation/clipper as a separate explicit processor if needed.
- Advanced stereo link controls.
- Transfer curve display.
- Custom premium UI.
