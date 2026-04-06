#!/usr/bin/env bash
# =============================================================================
# Memory Lab Eval Runner
# =============================================================================
# Builds and runs the memory lab eval suite, capturing results as machine-
# readable JSON under labs/memory/results/.
#
# Usage:
#   ./labs/memory/run_eval.sh [--iterations N] [--rebuild]
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
log()  { echo "[memory-eval] $*"; }
fail() { echo "[memory-eval] FATAL: $*" >&2; exit 1; }

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

  log "Building..."
  cmake --build "$BUILD_DIR" --parallel \
    || fail "Build failed"
  log "Build complete."
}

if $REBUILD || [ ! -d "$BUILD_DIR" ] || [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
  do_build
else
  # Check if source files changed since last build
  NEED_REBUILD=false
  for src in \
    "$REPO_ROOT/AestraCore/src/AestraUnifiedProfiler.cpp" \
    "$REPO_ROOT/AestraCore/include/AestraUnifiedProfiler.h" \
    "$REPO_ROOT/AestraCore/include/AestraMemory.h" \
    "$REPO_ROOT/AestraCore/src/AestraMemory.cpp" \
    "$REPO_ROOT/AestraCore/CMakeLists.txt" \
    "$REPO_ROOT/AestraAudio/src/DSP/AudioProcessor.cpp" \
    "$REPO_ROOT/AestraAudio/src/DSP/Filter.cpp" \
    "$REPO_ROOT/AestraAudio/include/GarbageCollector.h" \
    "$REPO_ROOT/AestraAudio/src/DSP/SampleRateConverter.cpp" \
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
find_bin() {
  local name="$1"
  for candidate in \
    "$BUILD_DIR/$name" \
    "$BUILD_DIR/AestraCore/$name" \
    "$BUILD_DIR/Tests/$name" \
    "$BUILD_DIR/bin/$name" \
    "$BUILD_DIR/bin/Tests/$name" \
    "$BUILD_DIR/bin/Tests/Headless/$name" \
    "$BUILD_DIR/Tests/Headless/$name"; do
    if [ -x "$candidate" ]; then
      echo "$candidate"
      return 0
    fi
  done
  return 1
}

BIN_MATH="$(find_bin MathTests)" || fail "Cannot find MathTests binary"
BIN_SRC_TEST="$(find_bin AestraSampleRateConverterTest)" || fail "Cannot find AestraSampleRateConverterTest binary"
BIN_OFFLINE="$(find_bin OfflineRenderRegressionTest)" || fail "Cannot find OfflineRenderRegressionTest binary"

# These may not exist yet (created during allocator work)
BIN_ALLOC=""
BIN_PROFILER=""
BIN_MEMBENCH=""
find_bin_optional() {
  local name="$1"
  for candidate in \
    "$BUILD_DIR/$name" \
    "$BUILD_DIR/AestraCore/$name" \
    "$BUILD_DIR/Tests/$name" \
    "$BUILD_DIR/bin/$name" \
    "$BUILD_DIR/bin/Tests/$name" \
    "$BUILD_DIR/Tests/Headless/$name" \
    "$BUILD_DIR/bin/Tests/Headless/$name"; do
    if [ -x "$candidate" ]; then
      echo "$candidate"
      return 0
    fi
  done
  return 1
}
BIN_ALLOC="$(find_bin_optional MemoryAllocatorTest 2>/dev/null)" || true
BIN_PROFILER="$(find_bin_optional MemoryProfilingTest 2>/dev/null)" || true
BIN_MEMBENCH="$(find_bin_optional MemoryBenchmark 2>/dev/null)" || true

log "Binaries:"
log "  Math tests:     ${BIN_MATH:-NOT FOUND}"
log "  SRC test:       ${BIN_SRC_TEST:-NOT FOUND}"
log "  Allocator test: ${BIN_ALLOC:-NOT FOUND (not yet implemented)}"
log "  Profiler test:  ${BIN_PROFILER:-NOT FOUND (not yet implemented)}"
log "  Mem benchmark:  ${BIN_MEMBENCH:-NOT FOUND (not yet implemented)}"
log "  Offline:        ${BIN_OFFLINE:-NOT FOUND}"

# ---------------------------------------------------------------------------
# Run eval lanes
# ---------------------------------------------------------------------------
MATH_EXIT=0
SRC_EXIT=0
ALLOC_EXIT=0
PROF_EXIT=0
BENCH_EXIT=0
OFFLINE_EXIT=0

MATH_OUT="${RESULTS_DIR}/${RID}_math.txt"
SRC_OUT="${RESULTS_DIR}/${RID}_srctest.txt"
ALLOC_OUT="${RESULTS_DIR}/${RID}_allocator.txt"
PROF_OUT="${RESULTS_DIR}/${RID}_profiler.txt"
BENCH_OUT="${RESULTS_DIR}/${RID}_membench.txt"
OFFLINE_OUT="${RESULTS_DIR}/${RID}_offline.txt"

# Lane 1: AestraCore correctness
log ""
log "=== Lane 1: MathTests ==="
set +e
"$BIN_MATH" > "$MATH_OUT" 2>&1
MATH_EXIT=$?
set -e
log "Exit code: $MATH_EXIT"

# Lane 2: Hot-path correctness
log ""
log "=== Lane 2: AestraSampleRateConverterTest ==="
set +e
"$BIN_SRC_TEST" > "$SRC_OUT" 2>&1
SRC_EXIT=$?
set -e
log "Exit code: $SRC_EXIT"

# Lane 3: Allocator correctness (optional)
log ""
log "=== Lane 3: MemoryAllocatorTest ==="
if [ -n "$BIN_ALLOC" ] && [ -x "$BIN_ALLOC" ]; then
  set +e
  "$BIN_ALLOC" > "$ALLOC_OUT" 2>&1
  ALLOC_EXIT=$?
  set -e
  log "Exit code: $ALLOC_EXIT"
else
  log "  Binary not found, skipping."
  ALLOC_EXIT=0
  ALLOC_OUT=""
fi

# Lane 4: Profiler accuracy (optional)
log ""
log "=== Lane 4: MemoryProfilingTest ==="
if [ -n "$BIN_PROFILER" ] && [ -x "$BIN_PROFILER" ]; then
  set +e
  "$BIN_PROFILER" > "$PROF_OUT" 2>&1
  PROF_EXIT=$?
  set -e
  log "Exit code: $PROF_EXIT"
else
  log "  Binary not found, skipping."
  PROF_EXIT=0
  PROF_OUT=""
fi

# Lane 5: Memory benchmark (optional)
log ""
log "=== Lane 5: MemoryBenchmark ==="
if [ -n "$BIN_MEMBENCH" ] && [ -x "$BIN_MEMBENCH" ]; then
  set +e
  # INFO logs go to stdout mixed with JSON. Filter to only valid JSON.
  "$BIN_MEMBENCH" --json --iterations "$ITERATIONS" 2>&1 | python3 -c "
import sys, json
lines = sys.stdin.read()
# Find the JSON object (starts with { and ends with })
start = lines.find('{')
end = lines.rfind('}')
if start >= 0 and end > start:
    print(lines[start:end+1])
else:
    # Fallback: print everything (will fail parse but at least we get raw output)
    print(lines)
" > "$BENCH_OUT"
  BENCH_EXIT=${PIPESTATUS[0]}
  set -e
  log "Exit code: $BENCH_EXIT"

  # Baseline comparison
  BASELINE_FILE="${RESULTS_DIR}/baseline.json"
  if [ -f "$BASELINE_FILE" ] && [ "$BENCH_EXIT" -eq 0 ]; then
    log "=== Baseline Comparison ==="
    python3 -c "
import json, sys

with open('$BENCH_OUT') as f:
    cur = json.load(f)
with open('$BASELINE_FILE') as f:
    base = json.load(f)

cur_cases = {c['case_id']: c for c in cur.get('cases', [])}
base_cases = {c['case_id']: c for c in base.get('cases', [])}

TIME_REGRESS_THRESHOLD = 10  # percent
ALLOC_INCREASE_THRESHOLD = 0  # allocations should not increase

regressions = []
alloc_regressions = []
for case_id, cur_data in cur_cases.items():
    if case_id not in base_cases:
        continue
    base_data = base_cases[case_id]
    base_ms = base_data['median_ms']
    cur_ms = cur_data['median_ms']
    if base_ms > 0:
        time_pct = ((cur_ms - base_ms) / base_ms) * 100.0
        if time_pct > TIME_REGRESS_THRESHOLD:
            regressions.append(f'{case_id}: time {base_ms:.2f}ms -> {cur_ms:.2f}ms ({time_pct:+.1f}%)')

    # Allocation count should never increase
    base_allocs = base_data.get('arena_allocs', 0) + base_data.get('profiler_allocs', 0)
    cur_allocs = cur_data.get('arena_allocs', 0) + cur_data.get('profiler_allocs', 0)
    if cur_allocs > base_allocs + ALLOC_INCREASE_THRESHOLD:
        alloc_regressions.append(f'{case_id}: allocs {base_allocs} -> {cur_allocs}')

if regressions:
    print('TIME REGRESSIONS')
    for r in regressions:
        print(f'  - {r}')
if alloc_regressions:
    print('ALLOCATION REGRESSIONS')
    for r in alloc_regressions:
        print(f'  - {r}')
if not regressions and not alloc_regressions:
    print('OK — no regressions detected')
" 2>&1 | while IFS= read -r line; do
      log "  $line"
    done
  else
    if [ ! -f "$BASELINE_FILE" ]; then
      log "  No baseline found — skipping comparison."
    fi
  fi
else
  log "  Binary not found, skipping."
  BENCH_EXIT=0
  BENCH_OUT=""
fi

# Lane 6: Offline render regression
log ""
log "=== Lane 6: OfflineRenderRegressionTest ==="
set +e
"$BIN_OFFLINE" > "$OFFLINE_OUT" 2>&1
OFFLINE_EXIT=$?
set -e
log "Exit code: $OFFLINE_EXIT"

# ---------------------------------------------------------------------------
# Determine decision status
# ---------------------------------------------------------------------------
log ""
log "=== Building summary ==="

HARD_FAILURES=()
ADVISORY_FLAGS=()
DECISION="accept"

if [ "$MATH_EXIT" -ne 0 ]; then
  HARD_FAILURES+=("MathTests exited with code $MATH_EXIT")
  DECISION="reject"
fi

if [ "$SRC_EXIT" -ne 0 ]; then
  HARD_FAILURES+=("AestraSampleRateConverterTest exited with code $SRC_EXIT")
  DECISION="reject"
fi

if [ -n "$BIN_ALLOC" ] && [ -x "$BIN_ALLOC" ] && [ "$ALLOC_EXIT" -ne 0 ]; then
  HARD_FAILURES+=("MemoryAllocatorTest exited with code $ALLOC_EXIT")
  DECISION="reject"
fi

if [ -n "$BIN_PROFILER" ] && [ -x "$BIN_PROFILER" ] && [ "$PROF_EXIT" -ne 0 ]; then
  HARD_FAILURES+=("MemoryProfilingTest exited with code $PROF_EXIT")
  DECISION="reject"
fi

if [ "$OFFLINE_EXIT" -ne 0 ]; then
  HARD_FAILURES+=("OfflineRenderRegressionTest exited with code $OFFLINE_EXIT")
  DECISION="reject"
fi

if [ -n "$BIN_MEMBENCH" ] && [ -x "$BIN_MEMBENCH" ] && [ "$BENCH_EXIT" -ne 0 ]; then
  ADVISORY_FLAGS+=("MemoryBenchmark exited with code $BENCH_EXIT")
  if [ "$DECISION" = "accept" ]; then
    DECISION="inconclusive"
  fi
fi

# Build JSON arrays
HF_JSON="[]"
if [ ${#HARD_FAILURES[@]} -gt 0 ]; then
  HF_JSON="["
  for i in "${!HARD_FAILURES[@]}"; do
    [ "$i" -gt 0 ] && HF_JSON+=","
    HF_JSON+="\"${HARD_FAILURES[$i]}\""
  done
  HF_JSON+="]"
fi

AF_JSON="[]"
if [ ${#ADVISORY_FLAGS[@]} -gt 0 ]; then
  AF_JSON="["
  for i in "${!ADVISORY_FLAGS[@]}"; do
    [ "$i" -gt 0 ] && AF_JSON+=","
    AF_JSON+="\"${ADVISORY_FLAGS[$i]}\""
  done
  AF_JSON+="]"
fi

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
    "core_tests": {
      "binary": "$BIN_MATH",
      "exit_code": $MATH_EXIT,
      "output_file": "$MATH_OUT"
    },
    "resampler_tests": {
      "binary": "$BIN_SRC_TEST",
      "exit_code": $SRC_EXIT,
      "output_file": "$SRC_OUT"
    },
    "allocator_tests": {
      "binary": "${BIN_ALLOC:-N/A}",
      "exit_code": $ALLOC_EXIT,
      "output_file": "${ALLOC_OUT:-N/A}"
    },
    "profiler_tests": {
      "binary": "${BIN_PROFILER:-N/A}",
      "exit_code": $PROF_EXIT,
      "output_file": "${PROF_OUT:-N/A}"
    },
    "memory_benchmark": {
      "binary": "${BIN_MEMBENCH:-N/A}",
      "exit_code": $BENCH_EXIT,
      "output_file": "${BENCH_OUT:-N/A}"
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
log ""
log "Raw outputs:"
log "  $MATH_OUT"
log "  $SRC_OUT"
[ -n "$ALLOC_OUT" ] && log "  $ALLOC_OUT"
[ -n "$PROF_OUT" ] && log "  $PROF_OUT"
[ -n "$BENCH_OUT" ] && log "  $BENCH_OUT"
log "  $OFFLINE_OUT"

# Exit with non-zero if hard gates failed
if [ "$DECISION" = "reject" ]; then
  exit 1
fi

exit 0
