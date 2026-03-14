#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-dev}"
JOBS="${JOBS:-2}"
MODE="${1:-run}"

TARGETS=(
  RumbleStateTest
  RumblePluginFactoryTest
  RumbleUsagePathTest
  RumbleDiscoveryTest
  ArsenalInstrumentAttachmentTest
  InternalPluginProjectRoundTripTest
  RumbleRenderTest
  RumbleArsenalAudibleTest
  ProjectRoundTripTest
)

usage() {
  cat <<'EOF'
Usage: scripts/run-confidence-suite.sh [run|build-only]

Builds the current self-contained high-signal Aestra confidence suite in build-dev
and optionally executes the resulting test binaries.

Environment overrides:
  BUILD_DIR=/path/to/build-dir   Build directory (default: ./build-dev)
  JOBS=4                         Parallel build jobs (default: 2)
EOF
}

if [[ "$MODE" != "run" && "$MODE" != "build-only" ]]; then
  usage >&2
  exit 2
fi

mkdir -p "$BUILD_DIR"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" --target "${TARGETS[@]}" -j"$JOBS"

if [[ "$MODE" == "build-only" ]]; then
  echo "Confidence suite built successfully in $BUILD_DIR"
  exit 0
fi

run_test() {
  local label="$1"
  shift
  echo "==> $label"
  "$@"
}

TMP_DIR="${TMPDIR:-/tmp}/aestra-confidence-suite"
mkdir -p "$TMP_DIR"

run_test RumbleStateTest "$BUILD_DIR/Tests/RumbleStateTest"
run_test RumblePluginFactoryTest "$BUILD_DIR/Tests/RumblePluginFactoryTest"
run_test RumbleUsagePathTest "$BUILD_DIR/Tests/RumbleUsagePathTest"
run_test RumbleDiscoveryTest "$BUILD_DIR/Tests/RumbleDiscoveryTest"
run_test ArsenalInstrumentAttachmentTest "$BUILD_DIR/Tests/ArsenalInstrumentAttachmentTest"
run_test InternalPluginProjectRoundTripTest "$BUILD_DIR/Tests/InternalPluginProjectRoundTripTest"
run_test RumbleRenderTest "$BUILD_DIR/Tests/Headless/RumbleRenderTest" "$TMP_DIR/rumble_render"
run_test RumbleArsenalAudibleTest "$BUILD_DIR/Tests/Headless/RumbleArsenalAudibleTest"
run_test ProjectRoundTripTest "$BUILD_DIR/Tests/ProjectRoundTripTest"

echo
echo "Confidence suite passed."
