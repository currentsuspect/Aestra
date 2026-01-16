## Contributing to Aestra Core (public)

This document explains how to build and contribute to the `Aestra-core` public repository.

Key points:

- `Aestra-core` should contain all non-sensitive code: AestraCore, AestraPlat, AestraUI, build scripts for the core, tests, and small mock assets.
- Premium assets, trained models, and private signing pipelines must live in private repositories (e.g., `Aestra-premium`, `Aestra-build`).

Local setup to build core when private repos are not present:

1. Clone the core repo:

   git clone <public-core-url>
   cd Aestra

2. Ensure mock assets exist (they are included in `assets_mock`):

   The CMake helper `cmake/AestraPremiumFallback.cmake` automatically defines `Aestra_CORE_ONLY` when `AestraMuse` is missing and points `Aestra_ASSETS_DIR` to `assets_mock`.

3. Build (Windows example):

   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release --parallel

4. If you have access to private repos, add them as submodules:

   git submodule add git@github.com:YourOrg/Aestra-premium.git AestraMuse
   git submodule update --init --recursive

Pre-commit checks
 - A PowerShell helper is available in `scripts/pre-commit-checks.ps1`. Install a Git hook to call it, or run it in CI.

If you are preparing a public PR that must not include private data, run a local secret scan (gitleaks recommended):

  gitleaks detect --source . --report-path gitleaks-report.json

If you are unsure about what belongs in the public core, ask a project maintainer before pushing.
