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
    *) echo "Unknown flag: $arg" >&2; exit 1 ;;
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

GIT_COMMIT=""
GIT_BRANCH=""
GIT_DIRTY=false
if command -v git &>/dev/null && git -C "$REPO_ROOT" rev-parse --git-dir &>/dev/null; then
  GIT_COMMIT="$(git -C "$REPO_ROOT" rev-parse HEAD 2>/dev/null || echo unknown)"
  GIT_BRANCH="$(git -C "$REPO_ROOT" rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)"
  [ -n "$(git -C "$REPO_ROOT" status --porcelain 2>/dev/null)" ] && GIT_DIRTY=true
fi

do_build() {
  log "Configuring..."
  cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
    -DAestra_CORE_MODE=ON \
    -DAESTRA_HEADLESS_ONLY=ON \
    -DAESTRA_ENABLE_UI=OFF \
    -DAESTRA_ENABLE_TESTS=ON \
    -DAESTRA_ENABLE_EXPERIMENTAL_TESTS=ON \
    -DCMAKE_BUILD_TYPE=Release || fail "CMake config failed"

  log "Building waveform-cache targets..."
  cmake --build "$BUILD_DIR" \
    --target AestraWaveformCacheTest AestraWaveformLockTest \
    --parallel 2 || fail "Build failed"
}

if $REBUILD || [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
  do_build
else
  NEED=false
  for src in \
    "$REPO_ROOT/AestraAudio/include/IO/WaveformCache.h" \
    "$REPO_ROOT/AestraAudio/src/IO/WaveformCache.cpp" \
    "$REPO_ROOT/AestraAudio/include/DSP/WaveformSIMD.h" \
    "$REPO_ROOT/Tests/AestraAudio/WaveformCacheTest.cpp" \
    "$REPO_ROOT/Tests/AestraAudio/WaveformLockTest.cpp" \
    "$REPO_ROOT/Tests/CMakeLists.txt"; do
    [ -f "$src" ] && [ "$src" -nt "$BUILD_DIR/CMakeCache.txt" ] && NEED=true && break
  done
  if $NEED; then
    do_build
  else
    log "Build up to date."
  fi
fi

find_bin() {
  local name="$1"
  for candidate in \
    "$BUILD_DIR/Tests/AestraAudio/$name" \
    "$BUILD_DIR/bin/Tests/AestraAudio/$name" \
    "$BUILD_DIR/Tests/$name" \
    "$BUILD_DIR/bin/Tests/$name" \
    "$BUILD_DIR/$name"; do
    [ -x "$candidate" ] && echo "$candidate" && return 0
  done
  return 1
}

BIN_CORRECTNESS="$(find_bin AestraWaveformCacheTest || true)"
BIN_LOCK="$(find_bin AestraWaveformLockTest || true)"
if [ -z "$BIN_CORRECTNESS" ] || [ -z "$BIN_LOCK" ]; then
  log "One or more binaries missing, rebuilding waveform-cache targets..."
  cmake --build "$BUILD_DIR" \
    --target AestraWaveformCacheTest AestraWaveformLockTest \
    --parallel 2 || fail "Build failed"
  BIN_CORRECTNESS="$(find_bin AestraWaveformCacheTest || true)"
  BIN_LOCK="$(find_bin AestraWaveformLockTest || true)"
fi
[ -z "$BIN_CORRECTNESS" ] && fail "Cannot find AestraWaveformCacheTest binary"
[ -z "$BIN_LOCK" ] && fail "Cannot find AestraWaveformLockTest binary"

CORRECTNESS_OUT="${RESULTS_DIR}/${RID}_correctness.txt"
LOCK_OUT="${RESULTS_DIR}/${RID}_lock.txt"

set +e
"$BIN_CORRECTNESS" > "$CORRECTNESS_OUT" 2>&1
CORRECTNESS_EXIT=$?
"$BIN_LOCK" > "$LOCK_OUT" 2>&1
LOCK_EXIT=$?
set -e

DECISION="accept"
REASON="All hard gates passed."
HARD_FAILURES=()
ADVISORY_FLAGS=()

if [ "$CORRECTNESS_EXIT" -ne 0 ]; then
  DECISION="reject"
  HARD_FAILURES+=("AestraWaveformCacheTest exited with code $CORRECTNESS_EXIT")
  REASON="AestraWaveformCacheTest exited with code $CORRECTNESS_EXIT"
fi

if [ "$LOCK_EXIT" -ne 0 ]; then
  ADVISORY_FLAGS+=("waveform_lock_latency_failed")
  if [ "$DECISION" = "accept" ]; then
    DECISION="inconclusive"
    REASON="Hard gates passed, advisory latency lane failed"
  fi
fi

if $GIT_DIRTY; then
  ADVISORY_FLAGS+=("dirty_worktree")
fi

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

cat > "$SUMMARY_FILE" <<EOJSON
{
  "run_id": "$RID",
  "timestamp": "$TS",
  "git": { "commit": "$GIT_COMMIT", "branch": "$GIT_BRANCH", "dirty": $GIT_DIRTY },
  "lanes": {
    "correctness": {
      "binary": "$BIN_CORRECTNESS",
      "exit_code": $CORRECTNESS_EXIT,
      "output_file": "$CORRECTNESS_OUT"
    },
    "lock_latency": {
      "binary": "$BIN_LOCK",
      "exit_code": $LOCK_EXIT,
      "output_file": "$LOCK_OUT"
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

log "Correctness exit: $CORRECTNESS_EXIT"
log "Lock-latency exit: $LOCK_EXIT"
log "Decision: $DECISION | Summary: $SUMMARY_FILE"

[ "$DECISION" = "reject" ] && exit 1
exit 0
