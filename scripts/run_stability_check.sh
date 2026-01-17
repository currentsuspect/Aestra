#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-"$ROOT_DIR/build"}"
CONFIG="${CONFIG:-Release}"
TARGET="AestraHeadless"

ARGS=()
if [[ "${PROJECT:-}" != "" ]]; then
  ARGS+=("--project" "$PROJECT")
elif [[ "${Aestra_PROJECT:-}" != "" ]]; then
  ARGS+=("--project" "$Aestra_PROJECT")
fi

SCENARIO_ARGS=()
if [[ "${SCENARIO_FILE:-}" != "" ]]; then
  SCENARIO_ARGS+=("--scenario-file" "$SCENARIO_FILE")
fi
if [[ "${SCENARIO_NAME:-}" != "" ]]; then
  SCENARIO_ARGS+=("--scenario" "$SCENARIO_NAME")
fi
if [[ "${REPORT_PATH:-}" != "" ]]; then
  SCENARIO_ARGS+=("--report" "$REPORT_PATH")
fi

if [[ "${MIN_PEAK:-}" != "" ]]; then
  ARGS+=("--min-peak" "$MIN_PEAK")
fi

RUNS=1
MODE="${1:-}" 
if [[ "$MODE" == "--stress" ]]; then
  RUNS=50
elif [[ "$MODE" != "" ]]; then
  echo "Usage: $0 [--stress]" >&2
  exit 2
fi

# Configure if needed (important for containers/CI).
if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
  cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DAestra_HEADLESS_ONLY=ON
fi

cmake --build "$BUILD_DIR" --config "$CONFIG" --target "$TARGET"

BIN_CANDIDATES=(
  "$BUILD_DIR/bin/$CONFIG/$TARGET.exe"
  "$BUILD_DIR/bin/$CONFIG/$TARGET"
  "$BUILD_DIR/bin/$TARGET.exe"
  "$BUILD_DIR/bin/$TARGET"
)

BIN=""
for c in "${BIN_CANDIDATES[@]}"; do
  if [[ -f "$c" ]]; then
    BIN="$c"
    break
  fi
done

if [[ "$BIN" == "" ]]; then
  echo "Could not find built binary for $TARGET." >&2
  echo "Tried:" >&2
  printf '  %s\n' "${BIN_CANDIDATES[@]}" >&2
  exit 2
fi

for i in $(seq 1 "$RUNS"); do
  LOG="$BUILD_DIR/${TARGET}_run_${i}.log"
  set +e
  "$BIN" "${SCENARIO_ARGS[@]}" "${ARGS[@]}" >"$LOG" 2>&1
  # If you need extra args, use PROJECT=/path/to/autosave.aes ./run_stability_check.sh
  STATUS=$?
  set -e

  if [[ $STATUS -ne 0 ]]; then
    printf "\033[0;31mCRITICAL FAILURE\033[0m\n" >&2
    echo "Run $i/$RUNS failed (exit=$STATUS). Log saved to: $LOG" >&2
    exit $STATUS
  else
    rm -f "$LOG"
  fi

done

echo "$TARGET stability check OK ($RUNS run(s))"
