#!/usr/bin/env bash
# =============================================================================
# Command History Eval Runner
# =============================================================================
# Builds and runs all command history tests, capturing results as JSON.
#
# Usage:
#   ./labs/command-history/run_eval.sh [--rebuild]
#
# Environment:
#   AESTRA_REPO   — path to Aestra repo root
#   BUILD_DIR     — override build directory (default: build-autoresearch)
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
    --rebuild)      REBUILD=true ;;
    --help|-h)
      echo "Usage: $0 [--rebuild]"
      exit 0
      ;;
    *)
      echo "Unknown flag: $arg" >&2
      exit 1
      ;;
  esac
  shift
done

log()  { echo "[eval] $*"; }
fail() { echo "[eval] FATAL: $*" >&2; exit 1; }

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
  CommandHistoryTest
  MacroCommandTest
  MoveClipCommandTest
  ClipCommandsTest
  MixerCommandsTest
  CommandTransactionTest
)

do_build() {
  log "Configuring build..."
  cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
    -DAestra_CORE_MODE=ON \
    -DAESTRA_HEADLESS_ONLY=ON \
    -DAESTRA_ENABLE_UI=OFF \
    -DAESTRA_ENABLE_TESTS=ON \
    -DCMAKE_BUILD_TYPE=Release \
    || fail "CMake configuration failed"

  log "Building targets..."
  for target in "${BUILD_TARGETS[@]}"; do
    log "  Building $target..."
    cmake --build "$BUILD_DIR" --target "$target" --parallel 2 \
      || fail "Failed to build $target"
  done
  log "Build complete."
}

if $REBUILD || [ ! -d "$BUILD_DIR" ] || [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
  do_build
else
  NEED_REBUILD=false
  for src in \
    "$REPO_ROOT/AestraAudio/include/Commands/CommandHistory.h" \
    "$REPO_ROOT/AestraAudio/src/Commands/CommandHistory.cpp" \
    "$REPO_ROOT/AestraAudio/include/Commands/ICommand.h" \
    "$REPO_ROOT/AestraAudio/include/Commands/MacroCommand.h" \
    "$REPO_ROOT/AestraAudio/include/Commands/CommandTransaction.h" \
    "$REPO_ROOT/AestraAudio/src/Commands/CommandTransaction.cpp" \
    "$REPO_ROOT/Tests/Commands/CommandHistoryTest.cpp" \
    "$REPO_ROOT/Tests/Commands/MacroCommandTest.cpp" \
    "$REPO_ROOT/Tests/Commands/MoveClipCommandTest.cpp" \
    "$REPO_ROOT/Tests/Commands/ClipCommandsTest.cpp" \
    "$REPO_ROOT/Tests/Commands/MixerCommandsTest.cpp" \
    "$REPO_ROOT/Tests/Commands/CommandTransactionTest.cpp" \
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
    "$BUILD_DIR/Tests/Commands/$name" \
    "$BUILD_DIR/bin/Tests/Commands/$name" \
    "$BUILD_DIR/Tests/$name" \
    "$BUILD_DIR/bin/Tests/$name" \
    "$BUILD_DIR/$name"; do
    if [ -x "$candidate" ]; then
      echo "$candidate"
      return 0
    fi
  done
  return 1
}

declare -A BINS
for target in "${BUILD_TARGETS[@]}"; do
  bin="$(find_bin "$target")" || fail "Cannot find $target binary"
  BINS[$target]="$bin"
done

log "Binaries:"
for target in "${BUILD_TARGETS[@]}"; do
  log "  $target: ${BINS[$target]}"
done

ADVISORY_FLAGS=()
if $GIT_DIRTY; then
  ADVISORY_FLAGS+=("dirty_worktree")
fi

# ---------------------------------------------------------------------------
# Run eval lanes
# ---------------------------------------------------------------------------
declare -A EXITS
declare -A OUT_FILES

for target in "${BUILD_TARGETS[@]}"; do
  out_file="${RESULTS_DIR}/${RID}_${target}.txt"
  OUT_FILES[$target]="$out_file"
  log ""
  log "=== Lane: $target ==="
  set +e
  "${BINS[$target]}" > "$out_file" 2>&1
  EXITS[$target]=$?
  set -e
  log "Exit code: ${EXITS[$target]}"
done

# ---------------------------------------------------------------------------
# Build summary JSON
# ---------------------------------------------------------------------------
HARD_FAILURES=()
DECISION="accept"

for target in "${BUILD_TARGETS[@]}"; do
  if [ "${EXITS[$target]}" -ne 0 ]; then
    HARD_FAILURES+=("$target exited with code ${EXITS[$target]}")
    DECISION="reject"
  fi
done

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
fi

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
    "command_history": {
      "binary": "${BINS[CommandHistoryTest]}",
      "exit_code": ${EXITS[CommandHistoryTest]},
      "output_file": "${OUT_FILES[CommandHistoryTest]}"
    },
    "macro_command": {
      "binary": "${BINS[MacroCommandTest]}",
      "exit_code": ${EXITS[MacroCommandTest]},
      "output_file": "${OUT_FILES[MacroCommandTest]}"
    },
    "move_clip_command": {
      "binary": "${BINS[MoveClipCommandTest]}",
      "exit_code": ${EXITS[MoveClipCommandTest]},
      "output_file": "${OUT_FILES[MoveClipCommandTest]}"
    },
    "clip_commands": {
      "binary": "${BINS[ClipCommandsTest]}",
      "exit_code": ${EXITS[ClipCommandsTest]},
      "output_file": "${OUT_FILES[ClipCommandsTest]}"
    },
    "mixer_commands": {
      "binary": "${BINS[MixerCommandsTest]}",
      "exit_code": ${EXITS[MixerCommandsTest]},
      "output_file": "${OUT_FILES[MixerCommandsTest]}"
    },
    "command_transaction": {
      "binary": "${BINS[CommandTransactionTest]}",
      "exit_code": ${EXITS[CommandTransactionTest]},
      "output_file": "${OUT_FILES[CommandTransactionTest]}"
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

[ "$DECISION" = "reject" ] && exit 1
exit 0
log "Summary:  $SUMMARY_FILE"

if [ "$DECISION" = "reject" ]; then
  exit 1
fi

exit 0
