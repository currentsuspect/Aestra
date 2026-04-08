#!/usr/bin/env bash
# =============================================================================
# Realtime Lab Eval Runner
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="${AESTRA_REPO:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
BUILD_DIR="${BUILD_DIR:-${REPO_ROOT}/build-autoresearch}"
RESULTS_DIR="${SCRIPT_DIR}/results"
ITERATIONS="${ITERATIONS:-3}"
REBUILD=false

i=0
while [ $i -lt $# ]; do
  arg="$1"
  case "$arg" in
    --iterations=*) ITERATIONS="${arg#*=}" ;;
    --iterations)   shift; ITERATIONS="$1" ;;
    --rebuild)      REBUILD=true ;;
    --help|-h)
      echo "Usage: $0 [--iterations N] [--rebuild]"
      exit 0
      ;;
    *) echo "Unknown flag: $arg" >&2; exit 1 ;;
  esac
  shift
done

log()  { echo "[rt-eval] $*"; }
fail() { echo "[rt-eval] FATAL: $*" >&2; exit 1; }

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

# Build
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
  NEED_REBUILD=false
  for src in \
    "$REPO_ROOT/AestraAudio/src/Linux/RtAudioDriver.cpp" \
    "$REPO_ROOT/AestraAudio/include/Linux/RtAudioDriver.h" \
    "$REPO_ROOT/AestraAudio/src/Core/AudioEngine.cpp" \
    "$REPO_ROOT/Tests/CMakeLists.txt" \
    "$REPO_ROOT/Tests/AestraAudio/RealtimeSchedulingTest.cpp"; do
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

# Locate binaries
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
BIN_RT=""
find_bin_optional() {
  local name="$1"
  for candidate in \
    "$BUILD_DIR/Tests/$name" \
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
BIN_RT="$(find_bin_optional RealtimeSchedulingTest 2>/dev/null)" || true

log "Binaries:"
log "  Math tests:    ${BIN_MATH}"
log "  SRC test:      ${BIN_SRC_TEST}"
log "  RT scheduling: ${BIN_RT:-NOT FOUND (not yet implemented)}"
log "  Offline:       ${BIN_OFFLINE}"

# Run eval lanes
MATH_EXIT=0
SRC_EXIT=0
RT_EXIT=0
OFFLINE_EXIT=0

MATH_OUT="${RESULTS_DIR}/${RID}_math.txt"
SRC_OUT="${RESULTS_DIR}/${RID}_srctest.txt"
RT_OUT="${RESULTS_DIR}/${RID}_realtime.txt"
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

# Lane 3: Realtime scheduling (optional)
log ""
log "=== Lane 3: RealtimeSchedulingTest ==="
if [ -n "$BIN_RT" ] && [ -x "$BIN_RT" ]; then
  set +e
  "$BIN_RT" > "$RT_OUT" 2>&1
  RT_EXIT=$?
  set -e
  log "Exit code: $RT_EXIT"
else
  log "  Binary not found, skipping."
  RT_EXIT=0
  RT_OUT=""
fi

# Lane 4: Offline render regression
log ""
log "=== Lane 4: OfflineRenderRegressionTest ==="
set +e
"$BIN_OFFLINE" > "$OFFLINE_OUT" 2>&1
OFFLINE_EXIT=$?
set -e
log "Exit code: $OFFLINE_EXIT"

# Determine decision status
HARD_FAILURES=()
DECISION="accept"

if [ "$MATH_EXIT" -ne 0 ]; then
  HARD_FAILURES+=("MathTests exited with code $MATH_EXIT")
  DECISION="reject"
fi

if [ "$SRC_EXIT" -ne 0 ]; then
  HARD_FAILURES+=("AestraSampleRateConverterTest exited with code $SRC_EXIT")
  DECISION="reject"
fi

if [ -n "$BIN_RT" ] && [ -x "$BIN_RT" ] && [ "$RT_EXIT" -ne 0 ]; then
  HARD_FAILURES+=("RealtimeSchedulingTest exited with code $RT_EXIT")
  DECISION="reject"
fi

if [ "$OFFLINE_EXIT" -ne 0 ]; then
  HARD_FAILURES+=("OfflineRenderRegressionTest exited with code $OFFLINE_EXIT")
  DECISION="reject"
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

REASON="All gates passed."
if [ "$DECISION" = "reject" ]; then
  REASON="Hard gate failure(s): ${HARD_FAILURES[*]}"
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
    "realtime_scheduling": {
      "binary": "${BIN_RT:-N/A}",
      "exit_code": $RT_EXIT,
      "output_file": "${RT_OUT:-N/A}"
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
    "advisory_flags": []
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
[ -n "$RT_OUT" ] && log "  $RT_OUT"
log "  $OFFLINE_OUT"

if [ "$DECISION" = "reject" ]; then
  exit 1
fi

exit 0
