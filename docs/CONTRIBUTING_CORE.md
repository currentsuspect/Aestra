## Contributing to Nomad Core (public)

This document explains how to build and contribute to the `nomad-core` public repository.

Key points:

- `nomad-core` should contain all non-sensitive code: NomadCore, NomadPlat, NomadUI, build scripts for the core, tests, and small mock assets.
- Premium assets, trained models, and private signing pipelines must live in private repositories (e.g., `nomad-premium`, `nomad-build`).

Local setup to build core when private repos are not present:

1. Clone the core repo:

   git clone <public-core-url>
   cd NOMAD

2. Ensure mock assets exist (they are included in `assets_mock`):

   The CMake helper `cmake/NomadPremiumFallback.cmake` automatically defines `NOMAD_CORE_ONLY` when `NomadMuse` is missing and points `NOMAD_ASSETS_DIR` to `assets_mock`.

3. Build (Windows example):

   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release --parallel

4. If you have access to private repos, add them as submodules:

   git submodule add git@github.com:YourOrg/nomad-premium.git NomadMuse
   git submodule update --init --recursive

Pre-commit checks
 - A PowerShell helper is available in `scripts/pre-commit-checks.ps1`. Install a Git hook to call it, or run it in CI.

If you are preparing a public PR that must not include private data, run a local secret scan (gitleaks recommended):

  gitleaks detect --source . --report-path gitleaks-report.json

If you are unsure about what belongs in the public core, ask a project maintainer before pushing.
