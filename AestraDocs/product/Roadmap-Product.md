# Aestra Product Roadmap

**Status:** Internal — Execution plan
**Last Updated:** 2026-05-14
**Owner:** Dylan

Cross-ref:
- Technical roadmap: `docs/technical/roadmap.md`
- Task list: `docs/technical/v1_beta_task_list.md`
- Architecture audit: `architecture/ARCHITECTURE_AUDIT_2026Q2.md`
- Audio-quality plan: `../audio/Path-to-All-A.md` (per-grade action plan)
- PDC v2 design: `PDC-v2-Design.md` (currently in flight)

---

## Timeline Overview

```
2026                              2027
Q1    Q2    Q3    Q4              Q1    Q2    Q3+
Jan   Apr   Jul   Sep   Dec      Jan   Apr   Jul
│     │     │     │     │        │     │     │
│◄──PHASE 1──►│     │            │     │     │
│  Foundation  │     │            │     │     │
│     │◄──PHASE 2──►│            │     │     │
│     │ Project+Undo │            │     │     │
│     │     │◄──PHASE 3──►│      │     │     │
│     │     │ Record+Export│      │     │     │
│     │     │     │◄─PHASE 4─►│   │     │     │
│     │     │     │ Hardening  │   │     │     │
│     │     │     │     │◄v1 BETA►│     │     │
│     │     │     │     │ FREE DAW │     │     │
│     │     │     │     │        │◄v1.0►│     │
│     │     │     │     │        │Cards+│     │
│     │     │     │     │        │Sub   │     │
│     │     │     │     │        │  │◄v1.1►│   │
│     │     │     │     │        │  │Muse │   │
│     │     │     │     │        │  │  │◄─v1.2+►│
│     │     │     │     │        │  │  │Cloud+ │
│     │     │     │     │        │  │  │Mobile │
```

---

## Phase 1: Foundation (Jan–Mar 2026) ✅ Mostly Done

Technical work: refactor risk reduction, smoke tests, logging.

**Product impact:** None visible to users. Internal stability.

---

## Phase 2: Project + Undo/Redo (Apr–Jun 2026) ← Current

Technical work: project save/load, autosave/recovery, undo/redo consistency.

**Product impact:** Users can trust that their work is safe. This is the trust foundation.

### Key deliverables:
- [x] ProjectSerializer with round-trip smoke test
- [x] AutosaveManager with crash-safe writes
- [x] CommandHistory fully wired across core UX
- [x] CommandTransaction for multi-step undo
- [x] Routing bugs (send smoothing, cycle detection) — fixed 2026-04-11
- [ ] **PDC v2 — graph-aware solver + engine integration (in flight, see `PDC-v2-Design.md`)**
- [ ] Project schema v1 fixture corpus + roundtrip test (audit §3.2)
- [ ] Group bus tracks
- [ ] Return/aux tracks

**Audio-quality work running in parallel** — see Path-to-All-A. PDC v2 is the gating item for Phase 3.

---

## Phase 3: Recording + Export (Jul–Sep 2026)

Technical work: recording hardening, export trust, stress tests.

**Product impact:** Users can record, export, and trust the output.

### Key deliverables:
- [ ] Multitrack recording validation
- [ ] Export parity tests through routing
- [ ] Device stress tests (low-spec Linux machines)
- [x] PDC v1 (flat-chain compensation) — shipped; see `AudioEngine::calculateLatencyCompensation`
- [ ] **PDC v2 phases P4–P10 complete** (graph-aware, sidechain, smooth recompute, mute stability, oversize buffer, dual reported-latency, domain plumbing)
- [ ] True Peak export validation — meter is integrated (`AudioEngine.cpp:1174-1179`); needs export-side ceiling enforcement
- [ ] Oversampling infrastructure for nonlinear DSP (AestraComp, safety limiter)
- [ ] Centralized export bit-depth conversion + mandatory dither

---

## Phase 4: Hardening (Oct–Nov 2026)

Technical work: freeze, bug fixes, performance optimization.

**Product impact:** Nothing new. Everything existing works better.

### Key deliverables:
- [ ] No new subsystems
- [ ] Performance profiling on target hardware (i5-3337U, 4GB)
- [ ] Linux packaging (AppImage, .deb, AUR)
- [ ] Crash reporter
- [ ] Documentation for public launch
- [ ] Architecture audit P0/P1 items resolved (license layering, RT guard consolidation, AudioEngine fallback singleton)
- [ ] All audio-quality grades ≥ A (see `../audio/Path-to-All-A.md`)
- [ ] Pan Law configurability (settings UI)

---

## Phase 5: v1 Beta Launch (December 2026)

**This is the public moment.** Everything before this was internal.

### What ships:
- Full free DAW (Core tier)
- All routing (groups, sends, sidechain, visualizer)
- Audition mode (album listening, DSP presets)
- Version control (Takes, Snapshots)
- Recording + export
- Basic plugins
- Linux support (primary)

### What does NOT ship:
- Card system (v1.0)
- Supporter tier (v1.0)
- Muse AI (v1.1)
- Cloud Takes (v1.2)
- macOS (post-Beta)

### Launch strategy:
- **Message:** "Aestra is free. Full stop. No Lite version, no feature gates."
- **Channels:** Reddit (r/hiphopheads, r/WeAreTheMusicMakers, r/linuxaudio), Twitter/X producer community, YouTube producer channels
- **Viral angle:** "Free DAW with animated routing visualizer" — screenshots and short clips
- **Target:** 10K users in first month

---

## Phase 6: v1.0 — Cards + Subscriptions (Q1 2027)

### What ships:
- Card system (Core Grey, Campus Blue)
- Supporter tier ($5/mo, $50/yr) with Silver card
- Founder tier ($129 one-time, limited) with Gold card + physical card
- Premium plugins (AestraRumble + 1-2 new plugins)
- Community features (Discord integration, roles)

### Launch strategy:
- **Message:** "Beta is over. Aestra stays free. Your Gold card is waiting."
- **Special:** Beta users get first access to Founder tier (2-week exclusive window)
- **Target:** 200-500 Founders, 5-8% Supporter conversion

---

## Phase 7: v1.1 — Muse AI (Q2 2027)

### What ships:
- Muse AI (Supporters only)
- Piano roll prediction
- Mixer suggestions (frequency masking, gain staging)
- Arrangement assist (pattern variation, transition suggestions)

### Prerequisites:
- Minimum 3-6 months of Aestra usage data for training
- Prediction quality must meet "useful, not annoying" bar
- Opt-in data collection from Supporters (with clear privacy policy)

### Launch strategy:
- **Message:** "Muse doesn't make music for you. It helps you make music better."
- **Demo:** Side-by-side video — producer using Muse vs. not using Muse
- **Risk:** If predictions are bad, delay launch. Bad AI is worse than no AI.

---

## Phase 8: v1.2+ — Platform Expansion (Q3 2027+)

### What ships (prioritized by demand):
- Cloud Takes (sync version control to cloud)
- macOS support
- Collaboration features (share Takes with other Aestra users)
- Mobile/tablet beta (Founders first)
- Plugin marketplace (third-party plugins)
- Community sound pack submissions

---

## Audio Quality Status (as of 2026-05-14)

The 12-layer scorecard from `implementation/audio_quality_executive_summary.md`, updated with current state:

| # | Layer | Current | Target | Path |
|---|-------|--------:|-------:|------|
| 1 | Signal Integrity | A | A | Regression test only |
| 2 | Resampling Quality | A- | A | `sinc64-optimization-tasks.md` |
| 3 | Timing Integrity | A+ | A+ | Hold |
| 4 | Plugin Delay Compensation | **C** (v1 ships, v2 in flight) | A+ | **`PDC-v2-Design.md` P4a/P4b currently active** |
| 5 | Automation Smoothing | A+ | A+ | Hold |
| 6 | Denormal Handling | A+ | A+ | Move FTZ/DAZ to thread-entry (audit §2.6) |
| 7 | Clipping Behavior | B+ | A | True Peak export ceiling + soft-limiter polish |
| 8 | Intersample Peaks | A- (meter live in RT) | A | Wire meter into export validation |
| 9 | Dithering / Export | A- | A | Centralize bit-depth conversion + mandatory 16-bit dither |
| 10 | CPU Efficiency | A+ | A+ | Hold |
| 11 | Pan Law | A- | A | User-selectable pan law |
| 12 | Oversampling (nonlinear DSP) | C | A | Reusable `Oversampler` for AestraComp + limiter |

Full per-layer plan with file citations: see [`Path-to-All-A.md`](../audio/Path-to-All-A.md).

---

## Design System: Borrowed Metaphors

Each system uses a metaphor from outside audio. These ship incrementally.

| System | Metaphor | Ships |
|--------|----------|-------|
| Routing Visualizer | Unreal Blueprints | v1 Beta |
| Audition Mode | Spotify | v1 Beta |
| Version Control (Takes) | Git | v1 Beta |
| Timeline Modes (C\|E\|A) | Google Maps | v1 Beta |
| Signal Flow | Terminal piping | v1 Beta |
| Card System | Gaming (Fortnite) | v1.0 |
| Muse AI | Code Copilot | v1.1 |

---

## Non-Goals (Do NOT Build for v1)

These are explicitly out of scope. Do not spend time on them.

- Surround / Atmos / ambisonics
- Video scoring / film post-production
- Full modular synth environment (The Grid competitor)
- Generative AI music creation
- Hardware controller integration (MIDI learn is enough)
- iOS/mobile (post v1.2)
- VST2 support (legal risk, CLAP/VST3 only)

---

## Decision Gates

| Gate | Date | Decision |
|------|------|----------|
| Plugin scope | Sep 2026 | Cut plugins from Beta if they threaten stability |
| Muse quality | Mar 2027 | Delay v1.1 if predictions aren't useful |
| Founder window | Jun 2027 | Close Founder tier permanently |
| macOS timeline | Q3 2027 | Start macOS only when Linux is stable |

---

*This document is internal. Align with `docs/technical/roadmap.md` for engineering timeline.*
