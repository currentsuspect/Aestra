# Aestra Automation Policy

This file documents the public automation expectations for this repository.

## Scope

- Automation should keep changes small, reviewable, and aligned with the current public roadmap.
- Public contributors and bots should prefer matching docs to the real repository state instead of inventing new process or release claims.
- Feature work should land through normal review, not through hidden automation-only flows.

## Pull Request Hygiene

- Do not merge bot-generated pull requests blindly.
- Review automation output the same way you would review a human contribution.
- Reject misleading suppressions, generated artifacts that should not be tracked, and claims that are not supported by the code or tests.

## Repo Expectations

- Keep build, test, and contributor docs aligned with the real CMake options and GitHub Actions workflows.
- Update public docs when contributor workflow, CI posture, or release-status language changes.
- Prefer surgical commits grouped by concern.

## Freshness

Last reviewed: 2026-04-04 by Codex.
