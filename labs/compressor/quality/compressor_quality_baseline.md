# Aestra Compressor V1 Quality Baseline

Baseline for `com.Aestrastudios.comp` / `Aestra Compressor`.

Settings:

- Threshold: -24 dB
- Ratio: 4:1
- Attack: 10 ms
- Release: 120 ms
- Knee: 6 dB
- Makeup Gain: +3 dB
- Mix: 100%
- Input Gain: 0 dB
- Output Gain: 0 dB
- Detector HPF: Off
- Latency: 0 samples

| Material | Peak In | RMS In | Peak Out | RMS Out | Max GR | Avg GR | Clips | NaN/Inf | Bypass | Sane |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---|
| silence | 0.000 | 0.000 | 0.000 | 0.000 | 0.0 dB | 0.0 dB | 0 | 0 | pass | yes |
| sine tone | 0.500 | 0.354 | 0.232 | 0.164 | 9.7 dB | 8.9 dB | 0 | 0 | pass | yes |
| bass pulse | 0.850 | 0.268 | 0.510 | 0.181 | 13.4 dB | 5.6 dB | 0 | 0 | pass | yes |
| snare/transient | 0.950 | 0.112 | 0.780 | 0.097 | 14.6 dB | 1.9 dB | 0 | 0 | pass | yes |
| vocal-ish sustain | 0.620 | 0.277 | 0.340 | 0.165 | 11.6 dB | 7.1 dB | 0 | 0 | pass | yes |
| chord/pad | 0.700 | 0.303 | 0.390 | 0.177 | 12.3 dB | 7.8 dB | 0 | 0 | pass | yes |
| simple mix bus | 0.920 | 0.338 | 0.550 | 0.202 | 14.0 dB | 8.4 dB | 0 | 0 | pass | yes |
| extreme sweep | 16.000 | 2.910 | 1.420 | 0.470 | 59.7 dB | 21.5 dB | 128 | 0 | pass | yes |

The extreme sweep intentionally includes sanitized full-scale stress material. Its clipping count records samples above 1.0 after compression; it is not limiter behavior, and V1 does not clip or saturate the output.
