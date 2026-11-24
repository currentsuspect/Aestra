#!/usr/bin/env bash
# © 2026 Aestra Studios — All Rights Reserved.
# Run the local counterparts of the size, contract-coverage, and app-budget CI gates.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR/.." rev-parse --show-toplevel)"
cd "$REPO_ROOT"

BUILD_ROOT="${AESTRA_PREFLIGHT_BUILD_ROOT:-$REPO_ROOT/build-preflight}"
APP_BUILD="$BUILD_ROOT/app"
CENSUS_ROOT="$BUILD_ROOT/contracts"
VST3_DIR="$REPO_ROOT/AestraAudio/External/vst3sdk"

step() {
    printf '\n==> %s\n' "$1"
}

configure_census_tree() {
    local label="$1"
    local build_dir="$2"
    shift 2
    local log="$CENSUS_ROOT/configure-${label}.log"
    if cmake -S "$REPO_ROOT" -B "$build_dir" "$@" >"$log" 2>&1; then
        printf 'Configured %s\n' "$label"
    else
        cat "$log" >&2
        return 1
    fi
}

step "File-size ratchet"
cmake -DREPO_ROOT="$REPO_ROOT" -P "$REPO_ROOT/Tests/Guards/file_size_ratchet.cmake"

step "Budget gate self-test"
python3 "$REPO_ROOT/scripts/ci/test-check-budgets.py"
python3 "$REPO_ROOT/tools/update/check_package_size_selftest.py"

step "Test-contract verifier self-test"
python3 "$REPO_ROOT/scripts/ci/test-verify-test-contracts.py"

step "Fetch the SDK needed for the maximal test census"
if [[ ! -f "$VST3_DIR/CMakeLists.txt" ]]; then
    git submodule update --init --depth 1 AestraAudio/External/vst3sdk
fi
git -C "$VST3_DIR" submodule update --init --depth 1 base pluginterfaces public.sdk

step "Configure the Release UI app and maximal test census"
cmake -S "$REPO_ROOT" -B "$APP_BUILD" \
    -DAestra_CORE_MODE=ON \
    -DAESTRA_CI=OFF \
    -DAESTRA_HEADLESS_ONLY=OFF \
    -DAESTRA_ENABLE_UI=ON \
    -DAESTRA_ENABLE_TESTS=ON \
    -DAESTRA_ENABLE_RUNTIME_TESTS=ON \
    -DAESTRA_ENABLE_EXPERIMENTAL_TESTS=ON \
    -DAESTRA_ALLOW_LICENSE_GATE_OFF=OFF \
    -DCMAKE_BUILD_TYPE=Release

step "Build the app and headless budget target with -j2"
cmake --build "$APP_BUILD" --config Release --target Aestra AestraHeadless -j2

step "Run the file-size guard and its mutation checks"
ctest --test-dir "$APP_BUILD" -C Release -R '^FileSizeRatchetGuard(SelfTest)?$' \
    --no-tests=error --output-on-failure

mkdir -p "$CENSUS_ROOT"
for runtime in OFF ON; do
    for experimental in OFF ON; do
        name="build-hlON-rt${runtime}-ex${experimental}"
        configure_census_tree "${name}" "$CENSUS_ROOT/$name" \
            -DAestra_CORE_MODE=ON \
            -DAESTRA_HEADLESS_ONLY=ON \
            -DAESTRA_ENABLE_TESTS=ON \
            -DAESTRA_ENABLE_RUNTIME_TESTS="$runtime" \
            -DAESTRA_ENABLE_EXPERIMENTAL_TESTS="$experimental" \
            -DCMAKE_BUILD_TYPE=Release
        ctest --test-dir "$CENSUS_ROOT/$name" --show-only=json-v1 >"$CENSUS_ROOT/census-${name}.json"
    done
done

step "Verify test-contract coverage across all five configurations"
ctest --test-dir "$APP_BUILD" --show-only=json-v1 >"$CENSUS_ROOT/census-build-maximal.json"
python3 "$REPO_ROOT/scripts/ci/verify-test-contracts.py" \
    --allowlist "$REPO_ROOT/Tests/policy/benchmark-allowlist.txt" \
    --maximal maximal \
    --census "hl-on-rtOFF-exOFF=$CENSUS_ROOT/census-build-hlON-rtOFF-exOFF.json" \
    --census "hl-on-rtOFF-exON=$CENSUS_ROOT/census-build-hlON-rtOFF-exON.json" \
    --census "hl-on-rtON-exOFF=$CENSUS_ROOT/census-build-hlON-rtON-exOFF.json" \
    --census "hl-on-rtON-exON=$CENSUS_ROOT/census-build-hlON-rtON-exON.json" \
    --census "maximal=$CENSUS_ROOT/census-build-maximal.json"

step "Run the app-size and headless-memory budget gate"
python3 "$REPO_ROOT/scripts/ci/check-budgets.py" --bin-dir "$APP_BUILD/bin"

printf '\nPreflight passed: file-size ratchet, test-contract coverage, and build budgets.\n'
