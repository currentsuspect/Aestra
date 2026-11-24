## Contributing to Aestra Core (public)

This document explains how to build and contribute to the `Aestra-core` public repository.

Key points:

- `Aestra-core` should contain all non-sensitive code: AestraCore, AestraPlat, AestraUI, build scripts for the core, tests, and small mock assets.
- Premium assets, trained models, and private signing pipelines must live in private repositories (e.g., `Aestra-premium`, `Aestra-build`).

Local setup to build core when private repos are not present:

1. Clone the core repo:

   git clone <public-core-url>
   cd Aestra

2. The public tree builds without any private repository. The core-mode helper (`aestra-core/cmake/AestraCoreMode.cmake`) points `Aestra_ASSETS_DIR` at `<repo>/assets_mock/` and creates that directory empty during configure if it is missing; the four card placeholders the tree ships live in `aestra-core/assets_mock/cards/`. Follow [BUILD.md](https://github.com/currentsuspect/Aestra/blob/develop/BUILD.md) for the exact configure flags.

3. Build (Windows example):

   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release --parallel

4. If you have access to private repos, add them as submodules:

   git submodule add git@github.com:YourOrg/Aestra-premium.git AestraMuse
   git submodule update --init --recursive

Pre-commit checks
 - Run `python3 scripts/install_hooks.py` to install the Git hook. The scanner is `scripts/pre_commit_checks.py`; the `.ps1` entry points (`scripts/pre-commit-checks.ps1`, `scripts/pre-commit.ps1`, `scripts/install-hooks.ps1`, `scripts/install-pre-commit.ps1`) remain as wrappers for PowerShell users — each delegating to the Python above and holding no logic of its own. `scripts/ci/check-script-layer.sh` enforces both halves of that: no CI step or git hook may invoke a `.ps1` directly, and a `.ps1` with a Python twin must still delegate to it.

If you are preparing a public PR that must not include private data, run a local secret scan (gitleaks recommended):

  gitleaks detect --source . --report-path gitleaks-report.json

If you are unsure about what belongs in the public core, ask a project maintainer before pushing.
