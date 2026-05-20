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

- Security audit
- Audio quality session (BS.1770 K-weighting, dither, export quantization, denormal protection)
- Repo hygiene mega-pass
- CI: nightly builds, versioning/tagging policy

### v0.1.0-foundation — Engine Foundation (Oct 2025)

- Initial engine architecture
- Core audio pipeline
- Basic plugin hosting
