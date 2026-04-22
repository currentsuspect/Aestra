# Aestra Comp v2 — Implementation Plan

## Master Index

| Phase | Document | Description | Agent | Status |
|-------|----------|-------------|-------|--------|
| 0 | [phase-0-foundation.md](phase-0-foundation.md) | Fix correctness bugs | Codex 5.3 | Ready |
| 0 | [codex-phase-0-prompt.md](codex-phase-0-prompt.md) | Codex handoff prompt | — | Ready |
| 1 | [phase-1-core-dsp.md](phase-1-core-dsp.md) | Core DSP rebuild | Codex 5.3 | Spec written |
| 2 | — | Lookahead & latency | Codex 5.3 | Pending |
| 3 | — | Quality modes & styles | Codex 5.3 | Pending |
| 4 | — | Editor / UI | GPT 5.4/5.5 | Pending |
| 5 | — | Presets | Codex 5.3 | Pending |
| 6 | — | Testing & hardening | Codex 5.3 | Pending |

## Phase Dependencies

```
Phase 0 (cleanup) ──→ Phase 1 (core DSP) ──┬──→ Phase 2 (lookahead)
                                             ├──→ Phase 3 (styles/quality)
                                             └──→ Phase 5 (presets, after 3)

Phase 1 (core DSP) ──→ Phase 4 (UI) [once params are stable]
Phase 2 + 3 + 4 ──→ Phase 6 (testing)
```

## Files

| File | Location |
|------|----------|
| Compressor source | `AestraAudio/include/Plugin/AestraComp.h` |
| Test (Phase 0) | `Tests/AestraAudio/AestraCompPhase0Test.cpp` |
| CMake (tests) | `Tests/CMakeLists.txt` |
| EQ (reuse BiquadFilter) | `AestraAudio/include/Plugin/AestraEQ.h` |
| Plugin interface | `AestraAudio/include/Plugin/PluginHost.h` |
| Plugin registration | `AestraAudio/src/Plugin/BuiltInPlugins.cpp` |
| Plugin factory | `AestraAudio/src/Plugin/PluginFactory.cpp` |

## Codex Workflow

1. Dylan hands Codex the phase prompt + relevant source files
2. Codex implements the changes
3. Dylan builds + runs tests
4. If issues, Dylan feeds failure output back to Codex
5. Once tests pass, move to next phase

## Key Decisions

- Styles are presets, not algorithms — same DSP, different defaults
- Lookahead is a delay line on the wet path, dry path gets equal delay
- Stereo link at gain computation stage, not detection/output
- BiquadFilter from AestraEQ.h reused for sidechain filters
- ParamSmoother class for all smoothed params
- No multiband, no upward compression, no vintage emulations in v1
