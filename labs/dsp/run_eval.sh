#!/usr/bin/env bash
# =============================================================================
# DSP Lab Eval Runner
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="${AESTRA_REPO:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
BUILD_DIR="${BUILD_DIR:-${REPO_ROOT}/build-autoresearch}"
RESULTS_DIR="${SCRIPT_DIR}/results"
REBUILD=false

i=0
while [ $i -lt $# ]; do
  arg="$1"
  case "$arg" in
    --iterations=*) ITERATIONS="${arg#*=}" ;;
    --iterations)   shift; ITERATIONS="$1" ;;
    --rebuild)      REBUILD=true ;;
    --help|-h) echo "Usage: $0 [--iterations N] [--rebuild]"; exit 0 ;;
    *) echo "Unknown flag: $arg" >&2; exit 1 ;;
  esac
  shift
done

log()  { echo "[dsp-eval] $*"; }
fail() { echo "[dsp-eval] FATAL: $*" >&2; exit 1; }
timestamp() { date -u +"%Y-%m-%dT%H:%M:%SZ"; }
run_id()    { date -u +"run_%Y%m%d_%H%M%S"; }

mkdir -p "$RESULTS_DIR"
RID="$(run_id)"
TS="$(timestamp)"
SUMMARY_FILE="${RESULTS_DIR}/summary.json"

log "Run ID:    $RID"
log "Timestamp: $TS"
log "Repo:      $REPO_ROOT"
log "Build dir: $BUILD_DIR"

# Git metadata
GIT_COMMIT="" GIT_BRANCH="" GIT_DIRTY=false
if command -v git &>/dev/null && git -C "$REPO_ROOT" rev-parse --git-dir &>/dev/null; then
  GIT_COMMIT="$(git -C "$REPO_ROOT" rev-parse HEAD 2>/dev/null || echo "unknown")"
  GIT_BRANCH="$(git -C "$REPO_ROOT" rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")"
  [ -n "$(git -C "$REPO_ROOT" status --porcelain 2>/dev/null)" ] && GIT_DIRTY=true
fi

# Build
do_build() {
  log "Configuring build..."
  cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
    -DAestra_CORE_MODE=ON -DAESTRA_HEADLESS_ONLY=ON -DAESTRA_ENABLE_UI=OFF \
    -DAESTRA_ENABLE_TESTS=ON -DAESTRA_ENABLE_EXPERIMENTAL_TESTS=ON \
    -DCMAKE_BUILD_TYPE=Release || fail "CMake configuration failed"
  log "Building..."
  cmake --build "$BUILD_DIR" --parallel || fail "Build failed"
  log "Build complete."
}

if $REBUILD || [ ! -d "$BUILD_DIR" ] || [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
  do_build
else
  NEED_REBUILD=false
  for src in \
    "$REPO_ROOT/AestraAudio/src/DSP/"*.cpp \
    "$REPO_ROOT/AestraAudio/include/DSP/"*.h \
    "$REPO_ROOT/AestraAudio/src/DSP/SincAVX512.cpp" \
    "$REPO_ROOT/Tests/AestraAudio/"*Test.cpp \
    "$REPO_ROOT/Tests/AestraAudio/SincBenchmark.cpp" \
    "$REPO_ROOT/Tests/Integration/ResamplerBenchmark.cpp" \
    "$REPO_ROOT/Tests/CMakeLists.txt"; do
    if [ -f "$src" ] && [ "$src" -nt "$BUILD_DIR/CMakeCache.txt" ]; then
      NEED_REBUILD=true; break
    fi
  done
  if $NEED_REBUILD; then log "Source files changed, rebuilding..."; do_build
  else log "Build is up to date, skipping."; fi
fi

# Locate binaries
find_bin() {
  local name="$1"
  for candidate in \
    "$BUILD_DIR/$name" "$BUILD_DIR/AestraCore/$name" "$BUILD_DIR/Tests/$name" \
    "$BUILD_DIR/bin/$name" "$BUILD_DIR/bin/Tests/$name" \
    "$BUILD_DIR/bin/Tests/Headless/$name" "$BUILD_DIR/Tests/Headless/$name"; do
    if [ -x "$candidate" ]; then echo "$candidate"; return 0; fi
  done
  return 1
}

BIN_MATH="$(find_bin MathTests)" || fail "Cannot find MathTests"
BIN_OSC="$(find_bin AestraOscillatorTest)" || fail "Cannot find AestraOscillatorTest"
BIN_MIX="$(find_bin AestraMixerBusTest)" || fail "Cannot find AestraMixerBusTest"
BIN_SRC="$(find_bin AestraSampleRateConverterTest)" || fail "Cannot find AestraSampleRateConverterTest"
BIN_OFF="$(find_bin OfflineRenderRegressionTest)" || fail "Cannot find OfflineRenderRegressionTest"
BIN_FILT=""
find_bin_optional() { local name="$1"; for c in "$BUILD_DIR/Tests/$name" "$BUILD_DIR/bin/Tests/$name" "$BUILD_DIR/Tests/Headless/$name" "$BUILD_DIR/bin/Tests/Headless/$name"; do if [ -x "$c" ]; then echo "$c"; return 0; fi; done; return 1; }
BIN_FILT="$(find_bin_optional AestraFilterTest 2>/dev/null)" || true
BIN_SINC="$(find_bin_optional AestraSincBenchmark 2>/dev/null)" || true
BIN_RBENCH="$(find_bin_optional ResamplerBenchmark 2>/dev/null)" || true

log "Binaries:"
log "  Math:          ${BIN_MATH}"
log "  Oscillator:    ${BIN_OSC}"
log "  MixerBus:      ${BIN_MIX}"
log "  Resampler:     ${BIN_SRC}"
log "  Filter:        ${BIN_FILT:-NOT FOUND}"
log "  Offline:       ${BIN_OFF}"
log "  Sinc BM:       ${BIN_SINC:-NOT FOUND}"
log "  Resampler BM:  ${BIN_RBENCH:-NOT FOUND}"

# Run eval lanes
MATH_EXIT=0; OSC_EXIT=0; MIX_EXIT=0; SRC_EXIT=0; FILT_EXIT=0; OFF_EXIT=0; SINC_EXIT=0; RBENCH_EXIT=0
MATH_OUT="${RESULTS_DIR}/${RID}_math.txt"
OSC_OUT="${RESULTS_DIR}/${RID}_osc.txt"
MIX_OUT="${RESULTS_DIR}/${RID}_mix.txt"
SRC_OUT="${RESULTS_DIR}/${RID}_src.txt"
FILT_OUT="${RESULTS_DIR}/${RID}_filt.txt"
OFF_OUT="${RESULTS_DIR}/${RID}_off.txt"
SINC_OUT="${RESULTS_DIR}/${RID}_sinc.txt"
RBENCH_OUT="${RESULTS_DIR}/${RID}_rbench.txt"

run_test() { local name="$1"; local bin="$2"; local outfile="$3"; local exitvar="$4";
  log ""
  log "=== Lane: $name ==="
  if [ -n "$bin" ] && [ -x "$bin" ]; then
    set +e; "$bin" > "$outfile" 2>&1; eval "$exitvar=\$?" ; set -e
    log "Exit code: ${!exitvar}"
  else
    log "  Binary not found, skipping."
    eval "$exitvar=0"
  fi
}

run_test "MathTests" "$BIN_MATH" "$MATH_OUT" "MATH_EXIT"
run_test "AestraOscillatorTest" "$BIN_OSC" "$OSC_OUT" "OSC_EXIT"
run_test "AestraMixerBusTest" "$BIN_MIX" "$MIX_OUT" "MIX_EXIT"
run_test "AestraSampleRateConverterTest" "$BIN_SRC" "$SRC_OUT" "SRC_EXIT"
run_test "AestraFilterTest" "$BIN_FILT" "$FILT_OUT" "FILT_EXIT"
run_test "OfflineRenderRegressionTest" "$BIN_OFF" "$OFF_OUT" "OFF_EXIT"
run_test "AestraSincBenchmark" "$BIN_SINC" "$SINC_OUT" "SINC_EXIT"
run_test "ResamplerBenchmark" "$BIN_RBENCH" "$RBENCH_OUT" "RBENCH_EXIT"

# Determine decision status
HARD_FAILURES=()
ADVISORY_FLAGS=()
DECISION="accept"

declare -A hard_lanes=( [MathTests]="$MATH_EXIT" [Oscillator]="$OSC_EXIT" [MixerBus]="$MIX_EXIT" [Resampler]="$SRC_EXIT" [Offline]="$OFF_EXIT" )
for lane_name in "MathTests" "Oscillator" "MixerBus" "Resampler" "Offline"; do
  exit_code="${hard_lanes[$lane_name]}"
  if [ "$exit_code" -ne 0 ]; then
    HARD_FAILURES+=("${lane_name} exited with code $exit_code")
    DECISION="reject"
  fi
done

# Filter test is hard if the binary exists
if [ -n "$BIN_FILT" ] && [ -x "$BIN_FILT" ] && [ "$FILT_EXIT" -ne 0 ]; then
  HARD_FAILURES+=("AestraFilterTest exited with code $FILT_EXIT")
  DECISION="reject"
fi

if [ -n "$BIN_SINC" ] && [ -x "$BIN_SINC" ] && [ "$SINC_EXIT" -ne 0 ]; then
  ADVISORY_FLAGS+=("AestraSincBenchmark exited with code $SINC_EXIT")
  [ "$DECISION" = "accept" ] && DECISION="inconclusive"
fi

if [ -n "$BIN_RBENCH" ] && [ -x "$BIN_RBENCH" ] && [ "$RBENCH_EXIT" -ne 0 ]; then
  ADVISORY_FLAGS+=("ResamplerBenchmark exited with code $RBENCH_EXIT")
  [ "$DECISION" = "accept" ] && DECISION="inconclusive"
fi

HF_JSON="[]"
if [ ${#HARD_FAILURES[@]} -gt 0 ]; then
  HF_JSON="["; for i in "${!HARD_FAILURES[@]}"; do [ "$i" -gt 0 ] && HF_JSON+=","; HF_JSON+="\"${HARD_FAILURES[$i]}\""; done; HF_JSON+="]"
fi
AF_JSON="[]"
if [ ${#ADVISORY_FLAGS[@]} -gt 0 ]; then
  AF_JSON="["; for i in "${!ADVISORY_FLAGS[@]}"; do [ "$i" -gt 0 ] && AF_JSON+=","; AF_JSON+="\"${ADVISORY_FLAGS[$i]}\""; done; AF_JSON+="]"
fi

REASON="All gates passed."
[ "$DECISION" = "reject" ] && REASON="Hard gate failure(s): ${HARD_FAILURES[*]}"
[ "$DECISION" = "inconclusive" ] && REASON="Some benchmarks could not complete cleanly: ${ADVISORY_FLAGS[*]}"

cat > "$SUMMARY_FILE" <<EOJSON
{
  "run_id": "$RID",
  "timestamp": "$TS",
  "git": { "commit": "$GIT_COMMIT", "branch": "$GIT_BRANCH", "dirty": $GIT_DIRTY },
  "lanes": {
    "core_tests":       { "binary": "$BIN_MATH", "exit_code": $MATH_EXIT, "output_file": "$MATH_OUT" },
    "oscillator":       { "binary": "$BIN_OSC", "exit_code": $OSC_EXIT, "output_file": "$OSC_OUT" },
    "mixer":            { "binary": "$BIN_MIX", "exit_code": $MIX_EXIT, "output_file": "$MIX_OUT" },
    "resampler":        { "binary": "$BIN_SRC", "exit_code": $SRC_EXIT, "output_file": "$SRC_OUT" },
    "filter":           { "binary": "${BIN_FILT:-N/A}", "exit_code": $FILT_EXIT, "output_file": "${FILT_OUT:-N/A}" },
    "offline":          { "binary": "$BIN_OFF", "exit_code": $OFF_EXIT, "output_file": "$OFF_OUT" },
    "sinc_benchmark":   { "binary": "${BIN_SINC:-N/A}", "exit_code": $SINC_EXIT, "output_file": "${SINC_OUT:-N/A}" },
    "resampler_benchmark": { "binary": "${BIN_RBENCH:-N/A}", "exit_code": $RBENCH_EXIT, "output_file": "${RBENCH_OUT:-N/A}" }
  },
  "decision": { "status": "$DECISION", "reason": "$REASON", "hard_gate_failures": $HF_JSON, "advisory_flags": $AF_JSON }
}
EOJSON

log ""
log "=== Eval Complete ==="
log "Decision: $DECISION"
log "Summary:  $SUMMARY_FILE"
log "Raw outputs:"
for f in "$MATH_OUT" "$OSC_OUT" "$MIX_OUT" "$SRC_OUT" "$FILT_OUT" "$OFF_OUT" "$SINC_OUT" "$RBENCH_OUT"; do
  [ -n "$f" ] && [ -f "$f" ] && log "  $f"
done

[ "$DECISION" = "reject" ] && exit 1
exit 0
