# Issue Governance Policy

Established: 2026-05-23
Supersedes: informal label conventions (pre-2026-05)

## Authority Split

| Layer | Authority | Purpose | Tool |
|---|---|---|---|
| Issue labels | Structural truth | Machine schema, filtering, automation | `canonical_schema.json` |
| Project board | Planning truth | Sprint decisions, human intent | GitHub Projects v2 |

Labels and board fields are **soft-aligned**, not hard-enforced. When they diverge,
the board is authoritative for priority and sprint assignment.

## Label Schema

See `scripts/canonical_schema.json` for the frozen taxonomy.

### Invariants

Every open issue MUST have:
- Exactly one `type:` label
- Exactly one `priority:` label
- At least one `comp:` label
- No deprecated legacy labels (`bug`, `feature`, `ui`, `dsp`, `plugin`, `performance`,
  `documentation`, `audio-thread`, `rt-safety`, `breaking-change`)

### Exceptions

Issues that genuinely span two `type:` categories may carry both, but this should be
rare and deliberate. Current known exceptions (3 issues total):

| Issue | Dual types | Rationale |
|---|---|---|
| #239 | `bug` + `hardening` | Autosave data race is both a correctness bug and an RT safety violation |
| #247 | `bug` + `hardening` | Plugin crash gap is both broken behavior and missing RT protection |
| #251 | `hardening` + `refactor` | Replacing deprecated atomic API is structural cleanup with RT safety impact |

No new dual-type exceptions should be added without updating this document.

## Board-to-Label Reconciliation

Priority is the only field that should be actively reconciled. Procedure:
1. Board is updated during sprint planning (human intent)
2. Labels are synced afterward (structural reflection)
3. Do NOT auto-sync — each sync is a deliberate decision

## Audit Cadence

- `scripts/audit_issues.py` — schema compliance (manual, pre-sprint)
- `scripts/audit_project_integrity.py` — project alignment (manual, pre-sprint)
- Snapshots saved to `.aestra/audit/snapshots/` for historical comparison

## Change Policy

Changes to `scripts/canonical_schema.json` require:
- Understanding why the old label drifted
- A migration plan for existing issues
- Snapshot before mutation
