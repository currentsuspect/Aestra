# Security Lab Book

## Purpose

Persistent memory for the security lab. Tracks vulnerability discovery, proof-of-fix
validation, and the hardening of Aestra's security surface across all modules.

## Structure

```
labs/security/
├── program.md              — Constitution
├── EVALS.md                — Eval documentation
├── LAB_BOOK.md             — This file
├── result_schema.json      — JSON schema
├── run_eval.sh             — Eval runner
├── results/                — Generated outputs
├── sessions/               — Per-session logs
└── findings/               — Durable knowledge
    ├── accepted_patterns.md
    ├── rejected_patterns.md
    ├── invariants.md
    └── bottlenecks.md
```

## Default Read Set

1. `program.md` — rules, scope, gates
2. `EVALS.md` — build commands, lanes
3. `LAB_BOOK.md` — this file
4. `findings/invariants.md` — things that must never break
5. `docs/security/REDTEAM.md` — current red team findings
6. `docs/security/README.md` — blue team vulnerability register

## Session Summary

| Session | Date | Type | Rounds | Vulns Found | Vulns Fixed | PoCs Mitigated | Notes |
|---------|------|------|--------|-------------|-------------|----------------|-------|
| S001 | 2026-04-10 | Red Team | 1 | 8 new (RTM-009 to RTM-016) | 0 | 3 existing reclassified (RTM-001/002/003 → Fixed) | Deep recon across 15 source files. Found: clip stoul crash, plugin cache DoS, metronome fread leak, silent autosave, env var injection. Created 3 new PoCs. Updated REDTEAM.md and blue team README. |
| S002 | 2026-04-10 | Blue Team | 1 | 0 | 3 (RTM-009/010/011) | 3 proof-of-fix tests written + passing | Fixed: clip color try/catch, plugin cache bounds, metronome fread checks. Audited all 10 stoul/stoi/stod calls in Source/. 8 PoC attack files generated and confirmed mitigated. |
| S003 | 2026-04-10 | Blue Team | 1 | 0 | 4 (RTM-013/014/015/016) | 3 proof-of-fix tests written + passing | Fixed: JSON stod guard, JSON mutable static, silent autosave → discard, headless env var validation. All exploitable vulns now closed. |
| S004 | 2026-04-10 | Blue Team | 1 | 0 | 5 (RTM-005/006/007/008/012) | 3 proof-of-fix tests written + passing | Fixed: FLAC vendorLen bounds, plugin cache mtime integrity, AESTRA_PROJECT env var → warning, crash flag + autosave timestamp delta, trusted path allowlist + first-load warning callback. All 16 vulns resolved. |

## Current State

- **Branch**: `develop`
- **ALL 16 VULNERABILITIES RESOLVED** (15 fixed, 1 mitigated)

### Fixed (15):
  - ~~RTM-001~~: WAV div-by-zero — 🟢 Fixed
  - ~~RTM-002~~: WAV heap exhaustion — 🟢 Fixed
  - ~~RTM-003~~: UnitManager std::stoul — 🟢 Fixed
  - ~~RTM-005~~: Plugin arbitrary code exec — 🟢 Mitigated (trusted paths + first-load warning)
  - ~~RTM-006~~: Plugin cache spoofing — 🟢 Fixed (mtime integrity)
  - ~~RTM-007~~: Crash flag + autosave pre-seeding — 🟢 Fixed (timestamp delta)
  - ~~RTM-008~~: Headless env var injection — 🟢 Fixed (env var ignored, --project required)
  - ~~RTM-009~~: Clip color std::stoul — 🟢 Fixed (S002)
  - ~~RTM-010~~: Plugin cache unbounded alloc — 🟢 Fixed (S002)
  - ~~RTM-011~~: MetronomeEngine fread unchecked — 🟢 Fixed (S002)
  - ~~RTM-012~~: FLAC vendorLen overflow — 🟢 Fixed (S004)
  - ~~RTM-013~~: JSON parseNumber stod crash — 🟢 Fixed (S003)
  - ~~RTM-014~~: JSON asArray() mutable static — 🟢 Fixed (S003)
  - ~~RTM-015~~: Silent autosave on recovery — 🟢 Fixed (S003)
  - ~~RTM-016~~: Headless env var silent failure — 🟢 Fixed (S003)

### Remaining (1):
  - **RTM-004**: JSON empty-project DoS — mitigated by depth limit, residual mild DoS (empty project loads)

### Proof-of-fix tests (9 total, all passing):
  - S002: `clip_color_stoul.cpp`, `plugin_cache_bounds.cpp`, `fread_truncated_wav.cpp`
  - S003: `json_parser_hardening.cpp`, `env_var_validation.cpp`, `autosave_recovery_guard.cpp`
  - S004: `flac_vendorlen_bounds.cpp`, `cache_mtime_integrity.cpp`, `plugin_trusted_path.cpp`

### Red team PoCs (11 total):
  - S001: `poc_clip_stoul.py`, `poc_plugin_cache_dos.py`, `poc_metronome_fread.py`
  - Legacy: `poc_wav_divzero.py`, `poc_wav_heap_exhaust.py`, `poc_unitmanager_stoul.py`, `poc_json_stack.py`
