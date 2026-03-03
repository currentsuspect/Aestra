# CI Workflows Map

This is the canonical map of GitHub Actions workflows for Aestra.

## Core Pipelines

### 1) `build.yml` — **Aestra Build & Test**
- Triggers: push + PR on `main`/`develop`
- Purpose: primary engineering signal
- Lanes:
  - Linux build + stable CTest suite (**blocking**)
  - Windows build (**blocking**)
  - macOS build preview (**non-blocking**, currently `continue-on-error`)

### 2) `public-ci.yml` — **Public CI (Aestra Core)**
- Triggers: push to `main`
- Purpose: guardrail for public distribution hygiene
- Key checks:
  - rejects private folders in public tree
  - builds core mode on Windows

## Documentation Pipelines

### 3) `deploy-docs.yml` — **Deploy Documentation**
- Triggers: docs/mkdocs changes on push + PR
- Purpose: build MkDocs and deploy docs/pages on push

### 4) `api-docs.yml` — **Generate API Documentation**
- Triggers: source/header changes + Doxyfile updates
- Purpose: Doxygen generation + quality report + artifacts

### 5) `docs-check.yml` — **Documentation Check**
- Triggers: PR docs/source changes
- Purpose: link/docs consistency checks via `scripts/docs-check.sh`

## Security Pipelines

### 6) `gitleaks-pr.yml` — **PR Secret Scan**
- Triggers: PR opened/synchronize/reopened
- Purpose: secret scanning in pull requests with report artifact

### 7) `gitleaks.yml` — **Gitleaks (mainline push scan)**
- Triggers: push to `main`/`develop`
- Purpose: baseline branch protection scan

## Release Pipeline

### 8) `private-release.yml` — **Private Release (Aestra Plus)**
- Triggers: manual (`workflow_dispatch`) only
- Purpose: private packaging/signing workflow for plus/internal distribution
- Notes:
  - requires private folders to exist
  - signing is optional and skipped when secrets are absent

---

## CI Policy Summary

- **Required for engineering health:** `Aestra Build & Test` (Linux + Windows)
- **Best-effort preview:** macOS build lane
- **Release-specific/manual:** private release workflow
- **Docs and API pipelines:** separate from core build gate
