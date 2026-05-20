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
| `v0.4.0-alpha`       | Current   | 2026-05-20 | Hardening milestone: security audit, audio quality session, repo hygiene mega-pass |

---

## Milestone History

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
