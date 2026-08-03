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
| `include/` digest | `e336a73822b7c2c1abf49a85ee0c430773a3b8b1d3dbe8f79929487d66d37279` |

## Provenance — verified, not assumed

`include/` and `LICENSE` were diffed against upstream at the commit above and are
byte-for-byte identical (67 files each side). "Do not edit these headers" below is
therefore an instruction backed by a check anyone can repeat, rather than a request
to trust whoever imported them.

Reproduce it:

```bash
curl -sSL -o clap.tar.gz \
  https://codeload.github.com/free-audio/clap/tar.gz/cd94482ba5941ae410809b6fbaed3bc851044270
tar xzf clap.tar.gz
diff -r clap-cd94482*/include AestraAudio/External/clap/include   # must be silent
```

Or check the digest in the table without needing the network:

```bash
cd AestraAudio/External/clap
find include -type f | LC_ALL=C sort | xargs sha256sum | sha256sum
```

`LC_ALL=C sort` is load-bearing — locale-dependent ordering changes the digest for
an unchanged tree, which would look exactly like tampering.

## Why vendored and not a submodule

No `actions/checkout` in `.github/workflows/ci.yml` fetches submodules — most set
`submodules: false` explicitly and the rest inherit the same default. A submodule
would therefore be absent in CI, `AESTRA_HAS_CLAP` would evaluate OFF, and
`CLAPHost.cpp` would never be compiled by any CI lane — the same gap that leaves
the VST3 host path uncompiled upstream of this change. Vendoring the headers is
what makes CI actually build the CLAP host.

It is a cheap tree to carry: header-only, 67 files, 380 KB, no transitive deps.

## Updating

Replace `include/` from the new tag, refresh the version, commit and digest above,
then rebuild. Do not edit these headers locally — any local change silently forks
the plugin ABI, which is the one thing a hosted plugin cannot tolerate. A digest
that no longer matches an untouched tree is the signal that someone did.
