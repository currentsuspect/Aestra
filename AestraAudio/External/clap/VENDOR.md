# CLAP SDK (vendored)

Header-only CLAP plugin API, vendored rather than added as a submodule.

| | |
| --- | --- |
| Upstream | https://github.com/free-audio/clap |
| Version | `1.2.9` |
| Commit | `cd94482ba5941ae410809b6fbaed3bc851044270` |
| License | MIT (see `LICENSE`) |
| Vendored | 2026-08-02 |
| Contents | `include/` only — no build files, sources, or artwork |

## Why vendored and not a submodule

Every `actions/checkout` in `.github/workflows/ci.yml` uses `submodules: false`.
A submodule would therefore be absent in CI, `AESTRA_HAS_CLAP` would evaluate
OFF, and `CLAPHost.cpp` would never be compiled by any CI lane — the same gap
that currently leaves the VST3 host path uncompiled upstream of this change.
Vendoring the headers is what makes CI actually build the CLAP host.

It is a cheap tree to carry: header-only, 67 files, ~350 KB, no transitive deps.

## Updating

Replace `include/` from the new tag, refresh the version and commit above, and
rebuild. Do not edit these headers locally — any local change silently forks the
plugin ABI, which is the one thing a hosted plugin cannot tolerate.
