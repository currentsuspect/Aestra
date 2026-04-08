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
}

if $REBUILD || [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
  do_build
else
  NEED=false
  for src in \
    "$REPO_ROOT/AestraCore/include/AestraThreading.h" \
    "$REPO_ROOT/AestraCore/src/ThreadingTests.cpp" \
    "$REPO_ROOT/Tests/CMakeLists.txt"; do
    [ -f "$src" ] && [ "$src" -nt "$BUILD_DIR/CMakeCache.txt" ] && NEED=true && break
  done
  if $NEED; then
    do_build
  else
    log "Build up to date."
  fi
fi

# Locate binary
BIN=""
for candidate in \
  "$BUILD_DIR/ThreadingTests" \
  "$BUILD_DIR/AestraCore/ThreadingTests" \
  "$BUILD_DIR/bin/ThreadingTests" \
  "$BUILD_DIR/bin/AestraCore/ThreadingTests"; do
  [ -x "$candidate" ] && BIN="$candidate" && break
done
if [ -z "$BIN" ]; then
  log "ThreadingTests binary missing, building target..."
  cmake --build "$BUILD_DIR" --target ThreadingTests --parallel 2 || fail "Build failed"
  for candidate in \
    "$BUILD_DIR/ThreadingTests" \
    "$BUILD_DIR/AestraCore/ThreadingTests" \
    "$BUILD_DIR/bin/ThreadingTests" \
    "$BUILD_DIR/bin/AestraCore/ThreadingTests"; do
    [ -x "$candidate" ] && BIN="$candidate" && break
  done
fi
[ -z "$BIN" ] && fail "Cannot find ThreadingTests binary"

log "Binary: $BIN"

# Run
OUT_FILE="${RESULTS_DIR}/${RID}_threading.txt"
set +e
"$BIN" > "$OUT_FILE" 2>&1
EXIT=$?
set -e
log "Exit code: $EXIT"

# Summary
DECISION="accept"
[ "$EXIT" -ne 0 ] && DECISION="reject"
REASON="All gates passed."
ADVISORY_FLAGS="[]"
if $GIT_DIRTY; then
  ADVISORY_FLAGS='["dirty_worktree"]'
fi
[ "$DECISION" = "reject" ] && REASON="ThreadingTests exited with code $EXIT"

cat > "$SUMMARY_FILE" <<EOJSON
{
  "run_id": "$RID",
  "timestamp": "$TS",
  "git": { "commit": "$GIT_COMMIT", "branch": "$GIT_BRANCH", "dirty": $GIT_DIRTY },
  "lanes": { "threading": { "binary": "$BIN", "exit_code": $EXIT, "output_file": "$OUT_FILE" } },
  "decision": { "status": "$DECISION", "reason": "$REASON", "hard_gate_failures": $([ "$EXIT" -ne 0 ] && echo "[\"ThreadingTests exited with code $EXIT\"]" || echo "[]"), "advisory_flags": $ADVISORY_FLAGS }
}
EOJSON

log "Decision: $DECISION | Summary: $SUMMARY_FILE"
[ "$DECISION" = "reject" ] && exit 1
exit 0
