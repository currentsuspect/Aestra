# Aestra Agent Notes

This file stores repo-specific workflow rules and continuity notes for future agent passes.

## PR Intake

- Do not merge Jules-generated PRs raw.
- Review Jules PRs for useful changes, then re-implement or cherry-pick only the good parts locally on `develop`.
- Reject misleading suppressions, stale claims, or incomplete fixture/test wiring even if the PR has otherwise useful ideas.
- Prefer clean local commits over merging bot branches directly.

## Current Workflow Preference

- Keep commits surgical and reviewable by concern.
- After each meaningful pass, update the technical docs so roadmap/task status stays current.
- Use `-j2` for local builds unless the user explicitly says otherwise.
- End-of-day ritual: confirm whether the planned agenda actually landed, then ask for tomorrow's intended scope before wrapping.

## Recent Stable Areas

- Recording pipeline now supports armed capture, take commit, monitoring modes, project-relative recordings, and input diagnostics.
- `Loop -> Project` now has empty-project fallback behavior and live extent sync as arrangement content changes.
- Offline export now renders through the live engine path and temporarily suspends the realtime stream during export.
- Last validated on 2026-04-02 / owner: Codex
