# Aestra Product Roadmap

**Status:** Internal — Execution plan
**Last Updated:** 2026-04-11
**Owner:** Dylan

Cross-ref: Technical roadmap at `docs/technical/roadmap.md`, Task list at `docs/technical/v1_beta_task_list.md`

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
- [ ] Routing bugs (send smoothing, cycle detection) — in progress
- [ ] Group bus tracks
- [ ] Return/aux tracks

---

## Phase 3: Recording + Export (Jul–Sep 2026)

Technical work: recording hardening, export trust, stress tests.

**Product impact:** Users can record, export, and trust the output.

### Key deliverables:
- [ ] Multitrack recording validation
- [ ] Export parity tests through routing
- [ ] Device stress tests (low-spec Linux machines)
- [ ] PDC (plugin delay compensation) through routing

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
