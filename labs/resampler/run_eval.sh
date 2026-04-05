#!/usr/bin/env bash
# =============================================================================
# Resampler Eval Runner
# =============================================================================
# Builds and runs the resampler eval suite, capturing results as machine-
# readable JSON under labs/resampler/results/.
#
# Usage:
#   ./labs/resampler/run_eval.sh [--iterations N] [--rebuild]
#
# Environment:
#   AESTRA_REPO   — path to Aestra repo root (default: script's repo root)
#   BUILD_DIR     — override build directory (default: build-autoresearch)
# =============================================================================
set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="${AESTRA_REPO:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
BUILD_DIR="${BUILD_DIR:-${REPO_ROOT}/build-autoresearch}"
RESULTS_DIR="${SCRIPT_DIR}/results"
ITERATIONS="${ITERATIONS:-3}"
REBUILD=false

# Parse flags
i=0
while [ $i -lt $# ]; do
  arg="$1"
  case "$arg" in
    --iterations=*) ITERATIONS="${arg#*=}" ;;
    --iterations)   shift; ITERATIONS="$1" ;;
    --rebuild)      REBUILD=true ;;
    --help|-h)
      echo "Usage: $0 [--iterations N] [--rebuild]"
      echo "  --iterations N   Number of benchmark iterations (default: 3)"
      echo "  --rebuild        Force a full rebuild"
      exit 0
      ;;
    *)
      echo "Unknown flag: $arg" >&2
      exit 1
      ;;
  esac
  shift
done

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
log()  { echo "[eval] $*"; }
fail() { echo "[eval] FATAL: $*" >&2; exit 1; }

timestamp() { date -u +"%Y-%m-%dT%H:%M:%SZ"; }
run_id()    { date -u +"run_%Y%m%d_%H%M%S"; }

mkdir -p "$RESULTS_DIR"

RID="$(run_id)"
TS="$(timestamp)"
SUMMARY_FILE="${RESULTS_DIR}/summary.json"
DETAIL_FILE="${RESULTS_DIR}/${RID}.json"

log "Run ID:    $RID"
log "Timestamp: $TS"
log "Repo:      $REPO_ROOT"
log "Build dir: $BUILD_DIR"
log "Results:   $RESULTS_DIR"
log "Iterations: $ITERATIONS"

# ---------------------------------------------------------------------------
# Git metadata
# ---------------------------------------------------------------------------
GIT_COMMIT=""
GIT_BRANCH=""
GIT_DIRTY=false
if command -v git &>/dev/null && git -C "$REPO_ROOT" rev-parse --git-dir &>/dev/null; then
  GIT_COMMIT="$(git -C "$REPO_ROOT" rev-parse HEAD 2>/dev/null || echo "unknown")"
  GIT_BRANCH="$(git -C "$REPO_ROOT" rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")"
  if [ -n "$(git -C "$REPO_ROOT" status --porcelain 2>/dev/null)" ]; then
    GIT_DIRTY=true
  fi
fi

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
BUILD_TARGETS=(
  AestraSampleRateConverterTest
  ResamplerBenchmark
  AestraSincBenchmark
  OfflineRenderRegressionTest
)

do_build() {
  log "Configuring build..."
  cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
    -DAestra_CORE_MODE=ON \
    -DAESTRA_HEADLESS_ONLY=ON \
    -DAESTRA_ENABLE_UI=OFF \
    -DAESTRA_ENABLE_TESTS=ON \
    -DAESTRA_ENABLE_EXPERIMENTAL_TESTS=ON \
    -DCMAKE_BUILD_TYPE=Release \
    || fail "CMake configuration failed"

  log "Building targets..."
  for target in "${BUILD_TARGETS[@]}"; do
    log "  Building $target..."
    cmake --build "$BUILD_DIR" --target "$target" --parallel \
      || fail "Failed to build $target"
  done
  log "Build complete."
}

if $REBUILD || [ ! -d "$BUILD_DIR" ] || [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
  do_build
else
  # Check if source files changed since last build
  NEED_REBUILD=false
  for src in \
    "$REPO_ROOT/AestraAudio/src/DSP/SampleRateConverter.cpp" \
    "$REPO_ROOT/AestraAudio/include/DSP/SampleRateConverter.h" \
    "$REPO_ROOT/Tests/AestraAudio/SampleRateConverterTest.cpp" \
    "$REPO_ROOT/Tests/Integration/ResamplerBenchmark.cpp" \
    "$REPO_ROOT/Tests/AestraAudio/SincBenchmark.cpp" \
    "$REPO_ROOT/Tests/Headless/OfflineRenderRegressionTest.cpp" \
    "$REPO_ROOT/Tests/CMakeLists.txt"; do
    if [ -f "$src" ] && [ "$src" -nt "$BUILD_DIR/CMakeCache.txt" ]; then
      NEED_REBUILD=true
      break
    fi
  done
  if $NEED_REBUILD; then
    log "Source files changed, rebuilding..."
    do_build
  else
    log "Build is up to date, skipping."
  fi
fi

# ---------------------------------------------------------------------------
# Locate binaries
# ---------------------------------------------------------------------------
# CMake typically places test binaries under build/Tests/ or build/Tests/Headless/
find_bin() {
  local name="$1"
  # Try multiple locations
  for candidate in \
    "$BUILD_DIR/Tests/$name" \
    "$BUILD_DIR/Tests/Headless/$name" \
    "$BUILD_DIR/bin/Tests/$name" \
    "$BUILD_DIR/bin/Tests/Headless/$name"; do
    if [ -x "$candidate" ]; then
      echo "$candidate"
      return 0
    fi
  done
  return 1
}

BIN_SRC_TEST="$(find_bin AestraSampleRateConverterTest)" || fail "Cannot find AestraSampleRateConverterTest binary"
BIN_RESAMPLER="$(find_bin ResamplerBenchmark)" || fail "Cannot find ResamplerBenchmark binary"
BIN_SINC="$(find_bin AestraSincBenchmark)" || fail "Cannot find AestraSincBenchmark binary"
BIN_OFFLINE="$(find_bin OfflineRenderRegressionTest)" || fail "Cannot find OfflineRenderRegressionTest binary"

log "Binaries:"
log "  SRC test:      $BIN_SRC_TEST"
log "  Resampler BM:  $BIN_RESAMPLER"
log "  Sinc BM:       $BIN_SINC"
log "  Offline:       $BIN_OFFLINE"

# ---------------------------------------------------------------------------
# Run eval lanes
# ---------------------------------------------------------------------------
SRC_TEST_OUT="${RESULTS_DIR}/${RID}_srctest.txt"
SRC_TEST_JSON="${RESULTS_DIR}/${RID}_srctest.json"
RESAMPLER_OUT="${RESULTS_DIR}/${RID}_resampler.txt"
RESAMPLER_JSON="${RESULTS_DIR}/${RID}_resampler.json"
SINC_OUT="${RESULTS_DIR}/${RID}_sinc.txt"
SINC_JSON="${RESULTS_DIR}/${RID}_sinc.json"
OFFLINE_OUT="${RESULTS_DIR}/${RID}_offline.txt"

SRC_EXIT=0
RESAMPLER_EXIT=0
SINC_EXIT=0
OFFLINE_EXIT=0

# Lane 1: Correctness tests
log ""
log "=== Lane 1: AestraSampleRateConverterTest ==="
set +e
"$BIN_SRC_TEST" --json --iterations "$ITERATIONS" > "$SRC_TEST_OUT" 2>&1
SRC_EXIT=$?
set -e
log "Exit code: $SRC_EXIT"

# Lane 2: Resampler benchmark
log ""
log "=== Lane 2: ResamplerBenchmark ==="
set +e
"$BIN_RESAMPLER" --json --iterations "$ITERATIONS" > "$RESAMPLER_OUT" 2>&1
RESAMPLER_EXIT=$?
set -e
log "Exit code: $RESAMPLER_EXIT"

# Lane 3: Sinc benchmark
log ""
log "=== Lane 3: AestraSincBenchmark ==="
set +e
"$BIN_SINC" --json --iterations "$ITERATIONS" > "$SINC_OUT" 2>&1
SINC_EXIT=$?
set -e
log "Exit code: $SINC_EXIT"

# Lane 4: Offline render regression
log ""
log "=== Lane 4: OfflineRenderRegressionTest ==="
set +e
"$BIN_OFFLINE" > "$OFFLINE_OUT" 2>&1
OFFLINE_EXIT=$?
set -e
log "Exit code: $OFFLINE_EXIT"

# ---------------------------------------------------------------------------
# Baseline comparison (if baselines exist)
# ---------------------------------------------------------------------------
BASELINE_RESAMPLER="${RESULTS_DIR}/baseline_resampler.json"
BASELINE_SINC="${RESULTS_DIR}/baseline_sinc.json"
REGRESSION_THRESHOLD="10"  # percent

compare_baselines() {
  local current_json="$1"
  local baseline_json="$2"
  local label="$3"

  if [ ! -f "$baseline_json" ]; then
    log "  No baseline found for $label — skipping comparison."
    return 0
  fi

  # Use python3 for reliable JSON parsing (available on most Linux systems)
  if ! command -v python3 &>/dev/null; then
    log "  python3 not found — skipping baseline comparison."
    return 0
  fi

  local comparison
  comparison=$(python3 -c "
import json, sys

with open('$current_json') as f:
    cur = json.load(f)
with open('$baseline_json') as f:
    base = json.load(f)

cur_cases = {c['case_id']: c for c in cur.get('cases', [])}
base_cases = {c['case_id']: c['median_ms'] for c in base.get('cases', [])}

regressions = []
improvements = []
noisy = []
CV_THRESHOLD = 0.05  # 5%

for case_id, cur_data in cur_cases.items():
    if case_id not in base_cases:
        continue
    base_ms = base_cases[case_id]
    cur_ms = cur_data['median_ms']
    cv = cur_data.get('cv', 0.0)
    if base_ms <= 0:
        continue
    pct = ((cur_ms - base_ms) / base_ms) * 100.0
    if cv > CV_THRESHOLD:
        noisy.append(f'{case_id}: cv={cv:.1%} (measurement unreliable)')
        continue
    if abs(pct) > $REGRESSION_THRESHOLD:
        entry = f'{case_id}: {base_ms:.2f}ms -> {cur_ms:.2f}ms ({pct:+.1f}%)'
        if pct > 0:
            regressions.append(entry)
        else:
            improvements.append(entry)

if noisy:
    print('NOISY')
    for n in noisy:
        print(f'  {n}')
if regressions:
    print('REGRESSIONS')
    for r in regressions:
        print(f'  {r}')
if improvements:
    print('IMPROVEMENTS')
    for i in improvements:
        print(f'  {i}')
if not noisy and not regressions and not improvements:
    print('OK')
" 2>&1)

  if echo "$comparison" | grep -q "^NOISY"; then
    log "  High variance detected in $label (CV > 5%):"
    while IFS= read -r line; do
      [ "$line" = "NOISY" ] && continue
      log "    ~ $line"
    done <<< "$comparison"
    # High variance makes the run inconclusive for this benchmark
    echo "NOISY"
    return 1
  fi

  if echo "$comparison" | grep -q "^REGRESSIONS"; then
    log "  REGRESSIONS detected in $label:"
    while IFS= read -r line; do
      [ "$line" = "REGRESSIONS" ] && continue
      log "    - $line"
      ADVISORY_FLAGS+=("Benchmark regression ($label):${line#  }")
    done <<< "$comparison"
  fi

  if echo "$comparison" | grep -q "^IMPROVEMENTS"; then
    log "  Improvements in $label:"
    while IFS= read -r line; do
      [ "$line" = "IMPROVEMENTS" ] && continue
      log "    + $line"
    done <<< "$comparison"
  fi

  if echo "$comparison" | grep -q "^OK"; then
    log "  No significant changes in $label (within ${REGRESSION_THRESHOLD}%)."
  fi

  return 0
}

if [ "$RESAMPLER_EXIT" -eq 0 ] && [ "$SINC_EXIT" -eq 0 ]; then
  log ""
  log "=== Baseline Comparison ==="
  compare_baselines "$RESAMPLER_OUT" "$BASELINE_RESAMPLER" "ResamplerBenchmark"
  compare_baselines "$SINC_OUT" "$BASELINE_SINC" "AestraSincBenchmark"
fi

# ---------------------------------------------------------------------------
# Parse results and build summary JSON
# ---------------------------------------------------------------------------
log ""
log "=== Building summary ==="

# Determine decision status
HARD_FAILURES=()
ADVISORY_FLAGS=()
DECISION="accept"

if [ "$SRC_EXIT" -ne 0 ]; then
  HARD_FAILURES+=("AestraSampleRateConverterTest exited with code $SRC_EXIT")
  DECISION="reject"
fi

if [ "$OFFLINE_EXIT" -ne 0 ]; then
  HARD_FAILURES+=("OfflineRenderRegressionTest exited with code $OFFLINE_EXIT")
  DECISION="reject"
fi

if [ "$RESAMPLER_EXIT" -ne 0 ]; then
  ADVISORY_FLAGS+=("ResamplerBenchmark exited with code $RESAMPLER_EXIT")
  if [ "$DECISION" = "accept" ]; then
    DECISION="inconclusive"
  fi
fi

if [ "$SINC_EXIT" -ne 0 ]; then
  ADVISORY_FLAGS+=("AestraSincBenchmark exited with code $SINC_EXIT")
  if [ "$DECISION" = "accept" ]; then
    DECISION="inconclusive"
  fi
fi

# Build hard failures JSON array
HF_JSON="[]"
if [ ${#HARD_FAILURES[@]} -gt 0 ]; then
  HF_JSON="["
  for i in "${!HARD_FAILURES[@]}"; do
    [ "$i" -gt 0 ] && HF_JSON+=","
    HF_JSON+="\"${HARD_FAILURES[$i]}\""
  done
  HF_JSON+="]"
fi

# Build advisory flags JSON array
AF_JSON="[]"
if [ ${#ADVISORY_FLAGS[@]} -gt 0 ]; then
  AF_JSON="["
  for i in "${!ADVISORY_FLAGS[@]}"; do
    [ "$i" -gt 0 ] && AF_JSON+=","
    AF_JSON+="\"${ADVISORY_FLAGS[$i]}\""
  done
  AF_JSON+="]"
fi

# Build reason
REASON="All gates passed."
if [ "$DECISION" = "reject" ]; then
  REASON="Hard gate failure(s): ${HARD_FAILURES[*]}"
elif [ "$DECISION" = "inconclusive" ]; then
  REASON="Some benchmarks could not complete cleanly: ${ADVISORY_FLAGS[*]}"
fi

# Write summary JSON
cat > "$SUMMARY_FILE" <<EOJSON
{
  "run_id": "$RID",
  "timestamp": "$TS",
  "git": {
    "commit": "$GIT_COMMIT",
    "branch": "$GIT_BRANCH",
    "dirty": $GIT_DIRTY
  },
  "lanes": {
    "correctness": {
      "binary": "$BIN_SRC_TEST",
      "exit_code": $SRC_EXIT,
      "output_file": "$SRC_TEST_OUT"
    },
    "resampler_benchmark": {
      "binary": "$BIN_RESAMPLER",
      "exit_code": $RESAMPLER_EXIT,
      "output_file": "$RESAMPLER_OUT"
    },
    "sinc_benchmark": {
      "binary": "$BIN_SINC",
      "exit_code": $SINC_EXIT,
      "output_file": "$SINC_OUT"
    },
    "offline_parity": {
      "binary": "$BIN_OFFLINE",
      "exit_code": $OFFLINE_EXIT,
      "output_file": "$OFFLINE_OUT"
    }
  },
  "decision": {
    "status": "$DECISION",
    "reason": "$REASON",
    "hard_gate_failures": $HF_JSON,
    "advisory_flags": $AF_JSON
  }
}
EOJSON

log ""
log "=== Eval Complete ==="
log "Decision: $DECISION"
log "Summary:  $SUMMARY_FILE"
log "Detail:   $DETAIL_FILE"
log ""
log "Raw outputs:"
log "  $SRC_TEST_OUT"
log "  $RESAMPLER_OUT"
log "  $SINC_OUT"
log "  $OFFLINE_OUT"

# Exit with non-zero if hard gates failed
if [ "$DECISION" = "reject" ]; then
  exit 1
fi

exit 0
