# Contributing to Aestra

This is the current contributor workflow for the public repository.

## Before You Start

- Read [../getting-started/building.md](../getting-started/building.md)
- Check the current priorities in [../technical/roadmap.md](../technical/roadmap.md)
- Review testing posture in [../technical/testing_ci.md](../technical/testing_ci.md)

## Repository Workflow

- Default integration branch: `develop`
- Stable branch: `main`
- Feature work should usually branch from `develop`

Example:

```bash
git checkout develop
git pull upstream develop
git checkout -b docs/update-build-guidance
```

## Local Setup

### Windows

```powershell
git clone https://github.com/YOUR_USERNAME/Aestra.git
cd Aestra
git remote add upstream https://github.com/currentsuspect/Aestra.git
pwsh -File scripts/install-hooks.ps1
cmake -S . -B build -DAestra_CORE_MODE=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo --parallel
ctest --test-dir build --config RelWithDebInfo --output-on-failure
```

### Linux

```bash
git clone https://github.com/YOUR_USERNAME/Aestra.git
cd Aestra
git remote add upstream https://github.com/currentsuspect/Aestra.git
cmake -S . -B build -DAestra_CORE_MODE=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

There is currently no `scripts/install-hooks.sh` equivalent in the repo. On Linux, install hooks manually if needed by configuring Git to use `.githooks/` or run the PowerShell helper if your environment supports it.

## Pull Request Expectations

- Keep each PR focused on one change area
- Update docs when behavior, build steps, or contributor workflow changes
- Add or update tests when behavior changes
- Add a short note under `Unreleased` in [../../CHANGELOG.md](../../CHANGELOG.md) for notable changes

## Documentation Maintenance Rules

- Prefer canonical docs in `docs/` over historical notes in `meta/`
- Check relative links after editing Markdown
- Run [../../scripts/docs-check.sh](../../scripts/docs-check.sh) when you change docs structure or links
- Keep build instructions aligned with the root [../../CMakeLists.txt](../../CMakeLists.txt)

If you touch public headers or API docs, regenerate documentation with:

```powershell
.\scripts\generate-api-docs.ps1 generate
```

or:

```powershell
.\scripts\generate-api-docs.bat
```

## Commit and Branch Naming

Use concise, reviewable commits. Conventional prefixes are preferred:

- `feat:`
- `fix:`
- `docs:`
- `refactor:`
- `test:`
- `chore:`

Branch examples:

- `feature/arsenal-pattern-routing`
- `fix/project-roundtrip-regression`
- `docs/build-and-contributing-refresh`

## Security and Licensing

- Security issues should be reported privately per [../../SECURITY.md](../../SECURITY.md)
- Contributions are accepted under the repository's ASSAL v1.1 terms in [../../LICENSE](../../LICENSE) and [../../LICENSING.md](../../LICENSING.md)
