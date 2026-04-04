# Aestra Development Workflow

## The Pipeline

```
You (low-effort text)
  ↓
"research: [issue]" → I spawn Researcher subagent
  ↓
Brief written to .workflow/{type}/YYYY-MM-DD-title.md
  ↓
"execute" or "pick up latest brief" → I read, verify, implement
  ↓
Fix → Test → Commit → Changelog → Close brief
```

## How to Use It

### Trigger Research
Just say: `research: the mixer meters clip at -6dB`

I will:
1. Spawn a Researcher subagent with full repo context
2. It analyzes the codebase, finds relevant files, forms a hypothesis
3. Writes a structured brief to `.workflow/{type}/YYYY-MM-DD-title.md`
4. Returns the brief for your review

### Trigger Execution
Just say: `execute` or `pick up the latest brief`

I will:
1. Read the latest open brief
2. **Independently verify** the Researcher's analysis (not blindly follow)
3. Confirm or correct the root cause hypothesis
4. Implement the fix/feature
5. Build and test
6. Commit surgically with brief ID reference
7. Update CHANGELOG.md
8. Update workflow docs if the fix changes how things work
9. Mark the brief as complete

## Issue Types

| Type | Directory | Branch prefix | Example |
|---|---|---|---|
| **Bug** | `.workflow/bug-report/` | `bug/` | `bug/mixer-meter-clipping` |
| **Hotfix** | `.workflow/hotfix/` | `hotfix/` | `hotfix/crash-on-load` |
| **Feature** | `.workflow/feature/` | `feat/` | `feat/transport-mute-btn` |
| **Refactor** | `.workflow/refactor/` | `refactor/` | `refactor/driver-registry` |

## Severity Levels

| Level | Definition | Response |
|---|---|---|
| **P0** | Crash, data loss, security | Immediate hotfix |
| **P1** | Broken core feature, no workaround | Same session |
| **P2** | Annoyance, workaround exists | Next session |
| **P3** | Cosmetic, nice-to-have | Backlog |

## Brief Format (Enforced)

Every brief MUST have this structure:

```yaml
---
id: "BRIEF-NNN"
type: "bug"
severity: "P2"
date: "2026-04-04"
status: "open"
---
```

```markdown
## User Input
> Raw user message

## Context
- Relevant files: [paths with line numbers]
- Affected layer: [Engine | UI | Driver | Build | Docs]
- Reproduction: [steps if bug]
- Current: [what happens]
- Expected: [what should happen]

## Root Cause Hypothesis
[Analysis]

## Suggested Approach
[Recommended fix — approach, not code]

## Risks
- [What could break]

## Acceptance Criteria
- [ ] Criterion 1

## Resolution
[Filled by Executor after implementation]
- What was done
- What differed from brief (if anything)
- Commit hash
```

## Rules

- **No brief, no code** — every change starts with a brief
- **Executor verifies independently** — the brief is a suggestion, not a bible
- **One brief = one branch = one PR**
- **Surgical commits** — grouped by concern, reference brief ID
- **Always document** — CHANGELOG.md updated, workflow docs updated if relevant
- **Briefs are permanent** — they stay as historical record
- **main and develop are NEVER deleted**

## Branch Flow

```
feature branches → develop → main (snapshot PR)
```

## Documentation Rules

- Every fix → update CHANGELOG.md
- Fix changes build/workflow/CI → update AGENTS.md
- Fix changes user-facing behavior → update docs/
- Brief files stay in `.workflow/` forever
