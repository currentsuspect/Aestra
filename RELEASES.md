# RELEASES.md — Aestra Release History and Tag Inventory

This file tracks the current tag inventory, milestone history, and release status.
AGENTS.md references this file for versioning policy (Section 27).

---

## Version Scheme

```text
v0.MINOR.PATCH-alpha    — active development (current phase)
v0.MINOR.PATCH-beta     — public beta (target: Dec 2026)
v1.0.0                  — initial public release
```

---

## Tag Inventory

| Tag                  | Status    | Date       | Notes                                     |
| -------------------- | --------- | ---------- | ----------------------------------------- |
| `v0.1.0-foundation`  | Keep      | 2025-10-26 | Historical engine foundation              |
| `v0.1.0-alpha`       | Deleted   | —          | Redundant with foundation tag             |
| `v1.0.0`             | Deleted   | —          | Premature — do not recreate until release |
| `v0.4.0-alpha`       | Superseded | 2026-05-20 | Hardening milestone: security audit, audio quality session, repo hygiene mega-pass |
| `v0.5.0-alpha`       | Superseded | 2026-05-23 | Takes system, CLAP parameters, audio quality, CI hardening, 11 PRs merged |
| `v0.6.0-alpha`       | Current   | 2026-05-29 | Security & RT hardening, plugin host crash resilience, callback-safety architecture, 26 PRs merged |

---

## Milestone History

### v0.6.0-alpha — Security & RT Hardening (May 2026)

**Security** — Take snapshot path traversal guards, plugin ID shadowing prevention, premium lease hardening, CLAP SIGPIPE guard, nightly token permissions.

**RT Safety** — Waveform callback lifetime, bounded preview decodes, mixer state clamping, master limiter reshaped to cubic Hermite knee, ARM64 denormals guarded.

**Plugin Hosting** — VST3 crash handling hardened, non-finite output quarantine, effect-chain fault state ownership.

**Callback Safety** — Triple-buffer EngineState, double-buffered routing snapshot, PDC edge ownership, TSan CI.

**Serialization** — Pattern restore, BPM sync, migration roundtrip proven.

**CI/Build** — LSan advisory job, GitHub Pages deploy toggle renamed to `DISABLE_GITHUB_PAGES`.

**PRs merged** — #308, #310–#312, #331–#335, #337–#347, #350, #358, #360, #365, #367–#368 (26 PRs).

### v0.5.0-alpha — Feature & CI Milestone (May 2026)

**Takes system** — Multi-take recording with manifest, snapshots, transactional switching, path traversal guards, and UI integration.

**CLAP parameter support** — ClapParamInfo, ClapPluginParams structs, CLAP core constant definitions, null paramsExt fix.

**Audio quality** — K-weight race fix, ARM64 denormals, send gain smoother coefficient fix, audition queue deadlock fix, autosave atomic rename.

**CI hardening** — Removed jwlawson/actions-setup-cmake@v2 from all jobs, system cmake, DelayLine off-by-one fix (Capacity+1 buffer), Windows path separator fix, 13 tests registered, platform guards.

**PRs merged** — #291-#305 (11 PRs), develop→main merge (#304, 51 commits).

### v0.4.0-alpha — Hardening Milestone (May 2026)

**Security audit** — 11 findings, 8 fixed (project load hardening, crash recovery, archive extraction), 3 deferred with justification.

**Audio quality** — BS.1770 K-weighting, TPDF dither, export quantization fix, denormal protection, Boost removal.

**RT safety** — Lock-free GC flush, SPSC retirement, effect chain snapshot race fix, fadeOutActive atomic, EngineSupervisor, RTGuard, AsyncCleanupManager.

**CI/CD** — Nightly builds with ASan/UBSan, versioning/tagging policy, macOS test exclusions, CodeRabbit review fixes.

**Project infrastructure** — 69 issues opened with full taxonomy, GitHub Projects board with 5 sprints through December beta.

**Known P0 beta-blockers** — OOP plugin parameters no-ops (#238), autosave data race (#239), routing bugs (#240-#243), CLAP MIDI unimplemented (#244).

### v0.1.0-foundation — Engine Foundation (Oct 2025)

- Initial engine architecture
- Core audio pipeline
- Basic plugin hosting
