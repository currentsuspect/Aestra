# Audio Quality Fix Implementation - Master Plan

**Date:** 2026-05-09  
**Status:** Planning Complete, Ready for Implementation  
**Target:** v1 Beta Release

---

## Overview

This document coordinates all audio quality fixes identified in the comprehensive audit. Each phase has a detailed implementation plan in separate documents.

---

## Implementation Phases

### Phase 1: Plugin Delay Compensation (PDC) ✅ PLANNED
**Status:** Detailed plan complete  
**Priority:** CRITICAL (Blocking v1 Beta)  
**Effort:** 2-3 weeks  
**Document:** `pdc_implementation_plan.md`

**Deliverables:**
- [x] Design complete
- [ ] DelayLine.h RT-safe ring buffer
- [ ] Latency calculation in AudioEngine
- [ ] Compensation applied in render path
- [ ] 4 comprehensive tests
- [ ] Performance validation

**Blocking Issues:** None  
**Dependencies:** None

---

### Phase 2: True Peak Metering ✅ PLANNED
**Status:** Detailed plan complete  
**Priority:** HIGH (Export/Mastering Credibility)  
**Effort:** 1 week  
**Document:** `true_peak_implementation_plan.md`

**Deliverables:**
- [x] Design complete
- [ ] TruePeakMeter with ITU-R BS.1770-4 compliance
- [ ] Integration into AudioEngine
- [ ] Export validation
- [ ] 3 comprehensive tests

**Blocking Issues:** None  
**Dependencies:** Can run in parallel with Phase 1

---

### Phase 3: Oversampling Infrastructure 📋 PLANNED
**Status:** Design summary below, detailed plan needed  
**Priority:** MEDIUM (Sound Quality)  
**Effort:** 1-2 weeks  
**Document:** `oversampling_implementation_plan.md` (to be created)

**Deliverables:**
- [ ] Reusable Oversampler class (2x, 4x)
- [ ] Apply to AestraComp (nonlinear)
- [ ] Apply to safety limiter
- [ ] Quality mode selection (realtime vs offline)
- [ ] Tests for aliasing reduction

**Blocking Issues:** None  
**Dependencies:** None (can run in parallel)

**Design Summary:**
```cpp
// Reusable oversampling wrapper
class Oversampler {
public:
    enum class Quality { Realtime, Offline };
    
    void initialize(uint32_t sampleRate, uint32_t factor, Quality quality);
    
    // RT-safe: process with oversampling
    void process(const float* input, float* output, uint32_t numFrames,
                 std::function<void(float*, uint32_t)> processor);
};

// Usage in AestraComp
void AestraComp::processBlock(...) {
    if (m_oversamplingEnabled) {
        m_oversampler.process(input, output, numFrames, [this](float* buf, uint32_t n) {
            // Nonlinear processing at higher sample rate
            applyCompression(buf, n);
        });
    } else {
        applyCompression(input, numFrames);
    }
}
```

---

### Phase 4: Export Pipeline Enforcement 📋 PLANNED
**Status:** Design summary below, detailed plan needed  
**Priority:** MEDIUM (Consistency)  
**Effort:** 3-5 days  
**Document:** `export_pipeline_implementation_plan.md` (to be created)

**Deliverables:**
- [ ] Centralized bit-depth conversion
- [ ] Mandatory dithering for 16-bit export
- [ ] Optional dithering for 24-bit export
- [ ] Export settings validation
- [ ] Tests for dithering correctness

**Blocking Issues:** None  
**Dependencies:** Phase 2 (true peak validation)

**Design Summary:**
```cpp
struct ExportConfig {
    enum class BitDepth { Float32, Int24, Int16 };
    enum class DitheringMode { None, TPDF, NoiseShaped };
    
    BitDepth bitDepth{BitDepth::Float32};
    DitheringMode dithering{DitheringMode::None};
    
    bool validateTruePeak{true};
    float truePeakCeilingdBTP{-1.0f};
    
    // Validation: 16-bit MUST have dithering
    bool isValid() const {
        if (bitDepth == BitDepth::Int16 && dithering == DitheringMode::None) {
            return false;  // Invalid: 16-bit requires dithering
        }
        return true;
    }
};
```

---

### Phase 5: Pan Law Configurability 📋 PLANNED
**Status:** Design summary below, detailed plan needed  
**Priority:** LOW (Polish)  
**Effort:** 2-3 days  
**Document:** `pan_law_implementation_plan.md` (to be created)

**Deliverables:**
- [ ] Selectable pan law (constant-power, -3dB, -4.5dB, -6dB)
- [ ] Settings persistence
- [ ] UI selector
- [ ] Tests for pan law correctness

**Blocking Issues:** None  
**Dependencies:** None (lowest priority)

**Design Summary:**
```cpp
enum class PanLaw {
    ConstantPower,  // Default (sin/cos)
    Minus3dB,       // -3dB center
    Minus4_5dB,     // -4.5dB center
    Minus6dB        // Linear
};

void calculatePanGains(float pan, PanLaw law, float& leftGain, float& rightGain) {
    switch (law) {
        case PanLaw::ConstantPower:
            fastPan(pan, leftGain, rightGain);  // Existing
            break;
        case PanLaw::Minus3dB:
            leftGain = (1.0f - pan) * 0.707f;
            rightGain = (1.0f + pan) * 0.707f;
            break;
        // ... etc
    }
}
```

---

## Implementation Schedule

### Week 1-2: PDC (Phase 1) - CRITICAL
- Day 1-2: Implement DelayLine.h and unit tests
- Day 3-4: Update AudioGraphState.h and AudioEngine.h
- Day 5-7: Implement calculateLatencyCompensation()
- Day 8-9: Apply compensation in render path
- Day 10-12: Write integration tests and verify

### Week 3: True Peak (Phase 2) - HIGH
- Day 1-2: Implement TruePeakMeter with ITU-R coefficients
- Day 3-4: Integrate into AudioEngine
- Day 5: Add export validation
- Day 6-7: Tests and performance profiling

### Week 4: Oversampling (Phase 3) - MEDIUM
- Day 1-2: Implement Oversampler class
- Day 3-4: Apply to AestraComp and limiter
- Day 5: Quality mode selection
- Day 6-7: Tests and aliasing measurements

### Week 5: Export + Pan Law (Phases 4-5) - POLISH
- Day 1-2: Export pipeline enforcement
- Day 3: Pan law configurability
- Day 4-5: Final integration tests
- Day 6-7: Documentation and code review

---

## RT Safety Checklist

Every implementation must pass this checklist:

### Audio Callback Path
- [ ] No heap allocations (`malloc`, `new`, `std::vector::push_back`)
- [ ] No locks (`std::mutex`, `std::lock_guard`)
- [ ] No blocking calls (`sleep`, `wait`, `join`)
- [ ] No file I/O (`fopen`, `fwrite`, `std::ofstream`)
- [ ] No logging (`std::cout`, `LOG_*` macros)
- [ ] No unbounded loops
- [ ] No container resizing
- [ ] No graph mutation

### State Management
- [ ] All RT state is snapshot-based or atomic
- [ ] State changes published from main thread only
- [ ] RT thread reads immutable snapshots
- [ ] No direct pointer sharing without refcounting

### Memory
- [ ] All buffers preallocated
- [ ] Fixed-size arrays or preallocated vectors
- [ ] No dynamic allocation in hot path
- [ ] Clear ownership semantics

---

## Testing Strategy

### Unit Tests (Per Phase)
- Isolated component testing
- Edge cases and boundary conditions
- Performance benchmarks

### Integration Tests (Cross-Phase)
- PDC + True Peak: Compensated tracks with true peak validation
- PDC + Oversampling: Compensated oversampled plugins
- Export + All: Full pipeline with all features enabled

### Regression Tests
- Existing audio tests must still pass
- No performance degradation
- No RT violations introduced

### Validation Tests
- ThreadSanitizer (TSan) for race detection
- AddressSanitizer (ASan) for memory errors
- Valgrind for leak detection
- CPU profiling for hotspots

---

## Quality Gates

### Before Merge (Each Phase)
- [ ] All unit tests passing
- [ ] Integration tests passing
- [ ] No RT violations (TSan clean)
- [ ] No memory leaks (ASan/Valgrind clean)
- [ ] Performance profiled (< 1% CPU overhead per feature)
- [ ] Code review completed
- [ ] Documentation updated

### Before v1 Beta Release
- [ ] All 5 phases complete
- [ ] Full regression suite passing
- [ ] Performance validated on target hardware
- [ ] User-facing documentation complete
- [ ] Known issues documented

---

## Rollback Plan

### Per-Phase Rollback
Each phase can be disabled independently:

**Phase 1 (PDC):**
```cpp
engine.setLatencyCompensationEnabled(false);
```

**Phase 2 (True Peak):**
```cpp
// Comment out metering in processBlock()
// m_truePeakMeter.process(outputBuffer, numFrames);
```

**Phase 3 (Oversampling):**
```cpp
plugin.setOversamplingEnabled(false);
```

**Phase 4 (Export):**
```cpp
// Revert to old export path
```

**Phase 5 (Pan Law):**
```cpp
// Revert to constant-power default
```

### Full Rollback
If critical issues arise:
1. Revert all commits in reverse order (Phase 5 → 1)
2. Verify existing tests still pass
3. Document issues for future retry

---

## Success Metrics

### Functional
- ✅ PDC: Parallel tracks null within -120 dB
- ✅ True Peak: Detects intersample peaks > sample peaks
- ✅ Oversampling: Reduces aliasing by >20 dB
- ✅ Export: Enforces dithering for 16-bit
- ✅ Pan Law: User-selectable, preserves existing behavior

### Performance
- ✅ PDC: < 1% CPU overhead
- ✅ True Peak: < 1% CPU overhead
- ✅ Oversampling: < 5% CPU overhead (realtime mode)
- ✅ Total: < 10% CPU overhead for all features combined

### Quality
- ✅ No RT violations introduced
- ✅ No memory leaks
- ✅ No race conditions
- ✅ Deterministic behavior (same input → same output)
- ✅ Offline render matches realtime render

---

## Risk Assessment

### High Risk
1. **PDC buffer sizing** - If plugins report >340ms latency, compensation will clip
   - Mitigation: Log warning, clamp to max, document limitation
   
2. **True peak CPU cost** - 4x oversampling may be expensive
   - Mitigation: Profile early, optimize with SIMD, consider separate thread

### Medium Risk
3. **Oversampling quality vs CPU** - High-quality filters are expensive
   - Mitigation: Separate realtime/offline quality modes
   
4. **Latency changes during playback** - Could cause clicks
   - Mitigation: Fade compensation buffer on change

### Low Risk
5. **Pan law compatibility** - Existing projects may sound different
   - Mitigation: Preserve default, make opt-in

---

## Communication Plan

### Internal (Team)
- Weekly status updates during implementation
- Daily standups for blocking issues
- Code reviews within 24 hours
- Performance reports after each phase

### External (Users)
- Changelog entries for each phase
- Beta release notes highlighting new features
- Documentation updates
- Known issues and workarounds

---

## Definition of Done (Overall)

Aestra is ready for v1 Beta when:

- [x] Comprehensive audit complete
- [x] Implementation plans complete
- [ ] Phase 1 (PDC) implemented and tested
- [ ] Phase 2 (True Peak) implemented and tested
- [ ] Phase 3 (Oversampling) implemented and tested
- [ ] Phase 4 (Export) implemented and tested
- [ ] Phase 5 (Pan Law) implemented and tested
- [ ] All quality gates passed
- [ ] Performance validated
- [ ] Documentation complete
- [ ] User-facing features documented
- [ ] Known issues documented

---

## Next Steps

1. **Review plans with team** (1 day)
2. **Begin Phase 1 (PDC)** (Week 1-2)
3. **Parallel Phase 2 (True Peak)** (Week 3)
4. **Continue Phases 3-5** (Week 4-5)
5. **Final integration and testing** (Week 6)
6. **Beta release** (Week 7)

**Total Timeline: 6-7 weeks**

---

## Document Status

- [x] Master plan complete
- [x] Phase 1 (PDC) detailed plan complete
- [x] Phase 2 (True Peak) detailed plan complete
- [ ] Phase 3 (Oversampling) detailed plan needed
- [ ] Phase 4 (Export) detailed plan needed
- [ ] Phase 5 (Pan Law) detailed plan needed

---

**Status:** Ready for team review and implementation kickoff
