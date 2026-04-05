#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="${AESTRA_REPO:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
BUILD_DIR="${BUILD_DIR:-${REPO_ROOT}/build-autoresearch}"
RESULTS_DIR="${SCRIPT_DIR}/results"
REBUILD=false

for arg in "$@"; do
  case "$arg" in
    --rebuild) REBUILD=true ;;
    --help|-h) echo "Usage: $0 [--rebuild]"; exit 0 ;;
  esac
done

log()  { echo "[eval] $*"; }
fail() { echo "[eval] FATAL: $*" >&2; exit 1; }

timestamp() { date -u +"%Y-%m-%dT%H:%M:%SZ"; }
run_id()    { date -u +"run_%Y%m%d_%H%M%S"; }

mkdir -p "$RESULTS_DIR"
RID="$(run_id)"
TS="$(timestamp)"
SUMMARY_FILE="${RESULTS_DIR}/summary.json"

log "Run ID: $RID | Timestamp: $TS | Repo: $REPO_ROOT"

# Git metadata
GIT_COMMIT=""; GIT_BRANCH=""; GIT_DIRTY=false
if command -v git &>/dev/null && git -C "$REPO_ROOT" rev-parse --git-dir &>/dev/null; then
  GIT_COMMIT="$(git -C "$REPO_ROOT" rev-parse HEAD 2>/dev/null || echo unknown)"
  GIT_BRANCH="$(git -C "$REPO_ROOT" rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)"
  [ -n "$(git -C "$REPO_ROOT" status --porcelain 2>/dev/null)" ] && GIT_DIRTY=true
fi

# Build
do_build() {
  log "Configuring..."
  cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
    -DAestra_CORE_MODE=ON -DAESTRA_HEADLESS_ONLY=ON \
    -DAESTRA_ENABLE_UI=OFF -DAESTRA_ENABLE_TESTS=ON \
    -DCMAKE_BUILD_TYPE=Release || fail "CMake config failed"
  log "Building ThreadingTests..."
  cmake --build "$BUILD_DIR" --target ThreadingTests --parallel 2 || fail "Build failed"
  log "Building ThreadingBenchmark..."
  cmake --build "$BUILD_DIR" --target ThreadingBenchmark --parallel 2 || fail "Build failed"
}

if $REBUILD || [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
  do_build
else
  NEED=false
  for src in \
    "$REPO_ROOT/AestraCore/include/AestraThreading.h" \
    "$REPO_ROOT/AestraCore/src/ThreadingTests.cpp" \
    "$REPO_ROOT/AestraCore/src/ThreadingBenchmark.cpp" \
    "$REPO_ROOT/AestraCore/CMakeLists.txt"; do
    [ -f "$src" ] && [ "$src" -nt "$BUILD_DIR/CMakeCache.txt" ] && NEED=true && break
  done
  if $NEED; then
    do_build
  else
    log "Build up to date."
  fi
fi

# Locate binaries
TESTS_BIN=""
BENCH_BIN=""
for candidate in \
  "$BUILD_DIR/ThreadingTests" \
  "$BUILD_DIR/AestraCore/ThreadingTests" \
  "$BUILD_DIR/bin/ThreadingTests" \
  "$BUILD_DIR/bin/AestraCore/ThreadingTests"; do
  [ -x "$candidate" ] && TESTS_BIN="$candidate" && break
done
for candidate in \
  "$BUILD_DIR/ThreadingBenchmark" \
  "$BUILD_DIR/AestraCore/ThreadingBenchmark" \
  "$BUILD_DIR/bin/ThreadingBenchmark" \
  "$BUILD_DIR/bin/AestraCore/ThreadingBenchmark"; do
  [ -x "$candidate" ] && BENCH_BIN="$candidate" && break
done

if [ -z "$TESTS_BIN" ] || [ -z "$BENCH_BIN" ]; then
  log "Missing binaries, rebuilding targets..."
  cmake --build "$BUILD_DIR" --target ThreadingTests ThreadingBenchmark --parallel 2 || fail "Build failed"
  for candidate in \
    "$BUILD_DIR/ThreadingTests" \
    "$BUILD_DIR/AestraCore/ThreadingTests" \
    "$BUILD_DIR/bin/ThreadingTests" \
    "$BUILD_DIR/bin/AestraCore/ThreadingTests"; do
    [ -x "$candidate" ] && TESTS_BIN="$candidate" && break
  done
  for candidate in \
    "$BUILD_DIR/ThreadingBenchmark" \
    "$BUILD_DIR/AestraCore/ThreadingBenchmark" \
    "$BUILD_DIR/bin/ThreadingBenchmark" \
    "$BUILD_DIR/bin/AestraCore/ThreadingBenchmark"; do
    [ -x "$candidate" ] && BENCH_BIN="$candidate" && break
  done
fi
[ -z "$TESTS_BIN" ] && fail "Cannot find ThreadingTests binary"
[ -z "$BENCH_BIN" ] && fail "Cannot find ThreadingBenchmark binary"

log "Tests binary: $TESTS_BIN"
log "Benchmark binary: $BENCH_BIN"

# Lane 1: Correctness
log "=== Lane 1: Threading Tests (Correctness) ==="
OUT_FILE="${RESULTS_DIR}/${RID}_threading.txt"
set +e
"$TESTS_BIN" > "$OUT_FILE" 2>&1
TESTS_EXIT=$?
set -e
log "Tests exit code: $TESTS_EXIT"

# Lane 2: Benchmark
log "=== Lane 2: Threading Benchmark (Performance) ==="
BENCH_OUT_FILE="${RESULTS_DIR}/${RID}_benchmark.txt"
BENCH_JSON_FILE="${RESULTS_DIR}/${RID}_benchmark.json"
set +e
"$BENCH_BIN" > "$BENCH_OUT_FILE" 2>&1
BENCH_EXIT=$?
set -e
log "Benchmark exit code: $BENCH_EXIT"

# Extract JSON from benchmark output (between the { } delimiters)
awk '/^\{/{found=1} found{print} /^\}/{if(found) exit}' "$BENCH_OUT_FILE" > "$BENCH_JSON_FILE" 2>/dev/null || true

# Capture baseline on first clean pass
BASELINE_FILE="${RESULTS_DIR}/baseline_benchmark.json"
if [ "$BENCH_EXIT" -eq 0 ] && [ ! -f "$BASELINE_FILE" ]; then
  cp "$BENCH_JSON_FILE" "$BASELINE_FILE"
  log "Baseline captured: $BASELINE_FILE"
fi

# Decision
DECISION="accept"
HARD_FAILURES="[]"
ADVISORY_FLAGS="[]"

if [ "$TESTS_EXIT" -ne 0 ]; then
  DECISION="reject"
  HARD_FAILURES="[\"ThreadingTests exited with code $TESTS_EXIT\"]"
fi

if [ "$BENCH_EXIT" -ne 0 ]; then
  DECISION="reject"
  if [ "$BENCH_EXIT" -eq 2 ]; then
    HARD_FAILURES="[\"ThreadingBenchmark XRUN/miss rate exceeded thresholds\"]"
  else
    HARD_FAILURES="[\"ThreadingBenchmark exited with code $BENCH_EXIT\"]"
  fi
fi

if $GIT_DIRTY; then
  ADVISORY_FLAGS='["dirty_worktree"]'
fi

REASON="All gates passed."
[ "$DECISION" = "reject" ] && REASON="Hard gate failure(s): $(echo "$HARD_FAILURES" | tr -d '[]\"')"

cat > "$SUMMARY_FILE" <<EOJSON
{
  "run_id": "$RID",
  "timestamp": "$TS",
  "git": { "commit": "$GIT_COMMIT", "branch": "$GIT_BRANCH", "dirty": $GIT_DIRTY },
  "lanes": {
    "tests": { "binary": "$TESTS_BIN", "exit_code": $TESTS_EXIT, "output_file": "$OUT_FILE" },
    "benchmark": { "binary": "$BENCH_BIN", "exit_code": $BENCH_EXIT, "output_file": "$BENCH_OUT_FILE", "json_file": "$BENCH_JSON_FILE" }
  },
  "decision": { "status": "$DECISION", "reason": "$REASON", "hard_gate_failures": $HARD_FAILURES, "advisory_flags": $ADVISORY_FLAGS }
}
EOJSON

log "Decision: $DECISION | Summary: $SUMMARY_FILE"
[ "$DECISION" = "reject" ] && exit 1
exit 0
