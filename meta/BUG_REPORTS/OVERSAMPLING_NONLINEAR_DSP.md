# Oversampling for Nonlinear DSP

**Issue:** Nonlinear DSP modules (AestraComp, limiter) lack oversampling, which can cause aliasing artifacts on high-frequency content.

**Impact:** Aliasing distortion on aggressive compression/limiting, especially at high sample rates or with bright material.

**Current State:**
- AestraComp: No oversampling
- Limiter: No oversampling
- Processing at native sample rate only

**TODO:**
- Implement 2x/4x oversampling option for AestraComp
- Implement 2x/4x oversampling option for master limiter
- Add quality preset to control oversampling factor
- Benchmark performance impact

**Reference:** Identified in 2026-05 audio quality audit follow-up
