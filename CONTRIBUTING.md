# Contributing to Nomad Core

Thanks for your interest in contributing to Nomad Core!

## Scope

- Public contributions target `nomad-core/`.
- Premium modules and private build/signing live in private repos and are not part of public PRs.

## Development Setup (Windows)

1. Install Git, CMake, MSVC, and PowerShell 7.
2. Install the pre-commit hook:
   - `pwsh -File scripts/install-hooks.ps1`
3. Configure/build core-only:
   - `cmake -S . -B build -DNOMAD_CORE_MODE=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo`
   - `cmake --build build --config RelWithDebInfo --parallel`

## Coding Guidelines

- Avoid committing binaries, models, or large assets.
- Do not commit secrets (.env, keys, certs). The hook and CI will block them.
- Prefer small, focused PRs with clear descriptions.

## Tests

- Include unit tests where applicable. Public CI runs on Windows.

## Licensing and Premium Features

- License verification internals are private. Use the public API declared in `nomad-core/include/Nomad/License.h`.
- Community builds run without premium features.