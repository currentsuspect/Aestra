#!/usr/bin/env bash
# © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#
# Regenerate the Folio baseline fixture tree from scratch, then validate it.
#
# The tree is hashed in full — there is no exclusion list. Anything the
# generation pipeline writes must be deterministic, so that a mismatch is always
# a real change and never a known-noisy file being ignored. (The editor history
# directory ProjectSerializer::save writes alongside the project carries a
# wall-clock timestamp in its filename; the builder deletes it rather than
# letting the manifest learn to skip it.)
#
# Usage: generate-fixture.sh <fixture-root> [--skip-validate]

set -euo pipefail

ROOT="${1:?usage: generate-fixture.sh <fixture-root> [--skip-validate]}"
SKIP_VALIDATE="${2:-}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
BUILD="$REPO/build-linux/labs/perf"

for tool in "$BUILD/FolioBuildFixture" "$BUILD/FolioValidateFixture"; do
    if [[ ! -x "$tool" ]]; then
        echo "missing apparatus binary: $tool" >&2
        echo "build with: cmake -S . -B build-linux -DAESTRA_BUILD_PERF_APPARATUS=ON && \\" >&2
        echo "            cmake --build build-linux --target FolioBuildFixture FolioValidateFixture -j2" >&2
        exit 1
    fi
done

# A clean tree every time: a regeneration that silently inherited a stale file
# would still hash consistently and still be wrong.
rm -rf "$ROOT"
mkdir -p "$ROOT/assets"

python3 "$HERE/generate-assets.py" --out "$ROOT/assets"
"$BUILD/FolioBuildFixture" --out "$ROOT"
# The declaration is committed source, not a generated artifact — it lives
# outside the tree precisely so that wiping the tree cannot destroy the
# authority the tree is checked against.
cp "$HERE/../CAPABILITIES.json" "$ROOT/CAPABILITIES.json"

# SHA256SUMS covers every file in the tree except itself, with paths relative to
# the fixture root so the manifest is location-independent.
( cd "$ROOT" && find . -type f ! -name SHA256SUMS | LC_ALL=C sort | xargs sha256sum > SHA256SUMS )

echo
echo "fixture tree:"
( cd "$ROOT" && cat SHA256SUMS | sed 's/^/  /' )

if [[ "$SKIP_VALIDATE" != "--skip-validate" ]]; then
    echo
    "$BUILD/FolioValidateFixture" --fixture "$ROOT" | tail -5
fi
