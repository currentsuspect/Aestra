# Vendored: Signalsmith Audio (MIT)

Header-only. Used by the sampler's keep-length pitch mode (time-stretch only;
pitch is applied by Aestra's own resampler).

| Library | Version | Upstream commit | Source |
|---|---|---|---|
| Signalsmith Stretch | 1.3.2 | a670068 (2026-09-25) | https://github.com/Signalsmith-Audio/signalsmith-stretch |
| Signalsmith Linear | 0.6.4 | de55e6a (2026-09-25) | https://github.com/Signalsmith-Audio/linear |

Copied files only: the headers, `include/` forwarding headers, Linear's
`platform/` backends (all opt-in by define; none enabled), LICENSE and README.
Upstream CMake files are **not** used: Linear's optional xsimd dispatch adds
per-architecture flags (-mavx*), which Aestra does not enable. Aestra exposes
these headers through an include-only target with no compile flags.

Unmodified. To update: replace the files from a new upstream tag and update
this table.
