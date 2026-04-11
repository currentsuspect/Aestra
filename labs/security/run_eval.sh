#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="${AESTRA_REPO:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
BUILD_DIR="${BUILD_DIR:-${REPO_ROOT}/build-autoresearch}"
RESULTS_DIR="${SCRIPT_DIR}/results"
REBUILD=false
SECURITY_TESTS_DIR="${REPO_ROOT}/tests/security"
REDTEAM_DIR="${REPO_ROOT}/tests/redteam"

for arg in "$@"; do
  case "$arg" in
    --rebuild) REBUILD=true ;;
    --help|-h) echo "Usage: $0 [--rebuild]"; exit 0 ;;
  esac
done

# log prints messages to stdout prefixed with "[security-eval]".
log()  { echo "[security-eval] $*"; }
# fail prints a fatal message prefixed with [security-eval] to stderr and exits the script with status 1.
fail() { echo "[security-eval] FATAL: $*" >&2; exit 1; }

# timestamp outputs the current UTC timestamp in ISO 8601 format (YYYY-MM-DDTHH:MM:SSZ).
timestamp() { date -u +"%Y-%m-%dT%H:%M:%SZ"; }
# run_id echoes a UTC run identifier in the form run_YYYYmmdd_HHMMSS.
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

# do_build Configures CMake with security-test-specific flags and builds the project into BUILD_DIR.
# On configuration or build failure it calls fail and exits the script.
do_build() {
  log "Configuring..."
  cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
    -DAestra_CORE_MODE=ON -DAESTRA_HEADLESS_ONLY=ON \
    -DAESTRA_ENABLE_UI=OFF -DAESTRA_ENABLE_TESTS=ON \
    -DCMAKE_BUILD_TYPE=Release || fail "CMake config failed"
  log "Building security test targets..."
  cmake --build "$BUILD_DIR" --parallel 2 || fail "Build failed"
}

if $REBUILD || [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
  do_build
else
  NEED=false
  for src in \
    "$REPO_ROOT/tests/security/"* \
    "$REPO_ROOT/tests/security/CMakeLists.txt" \
    "$REPO_ROOT/AestraCore/include/AestraJSON.h" \
    "$REPO_ROOT/AestraAudio/src/IO/MiniAudioDecoder.cpp" \
    "$REPO_ROOT/AestraAudio/src/Models/UnitManager.cpp" \
    "$REPO_ROOT/AestraAudio/src/Plugin/SamplerPlugin.cpp" \
    "$REPO_ROOT/AestraAudio/src/IO/MetadataParser.cpp" \
    "$REPO_ROOT/AestraPlat/src/Linux/PlatformUtilsLinux.cpp" \
    "$REPO_ROOT/Source/Core/ProjectSerializer.cpp"; do
    if [ -f "$src" ] && [ "$src" -nt "$BUILD_DIR/CMakeCache.txt" ]; then
      NEED=true; break
    fi
  done
  if $NEED; then
    do_build
  else
    log "Build up to date."
  fi
fi

# ============================================================
# Lane 1: Security Tests
# ============================================================
log "=== Lane 1: Security Tests ==="

# The security CMakeLists.txt creates individual binaries, not a unified target.
# Build and run each test individually.
SEC_TEST_NAMES=("SecStoulCrash" "SecJsonDos" "SecDivZero" "SecSamplerPath" "SecId3Overflow" "SecShellEscape" "SecClipColorStoul" "SecPluginCacheBounds" "SecFreadTruncatedWav" "SecJsonParserHardening" "SecEnvVarValidation" "SecAutosaveRecoveryGuard" "SecFlacVendorlenBounds" "SecCacheMtimeIntegrity" "SecPluginTrustedPath")
TEST_DISPLAY_NAMES=("stoul_crash" "json_dos" "div_zero_channels" "sampler_path_traversal" "id3_overflow" "shell_escape_test" "clip_color_stoul" "plugin_cache_bounds" "fread_truncated_wav" "json_parser_hardening" "env_var_validation" "autosave_recovery_guard" "flac_vendorlen_bounds" "cache_mtime_integrity" "plugin_trusted_path")

TEST_EXIT_CODES=()
TEST_OUTPUT_FILES=()
TEST_PASS=0
TEST_FAIL=0

for i in "${!SEC_TEST_NAMES[@]}"; do
  target="${SEC_TEST_NAMES[$i]}"
  display="${TEST_DISPLAY_NAMES[$i]}"

  # Locate or build the binary
  SEC_BIN=""
  for candidate in \
    "$BUILD_DIR/$target" \
    "$BUILD_DIR/tests/$target" \
    "$BUILD_DIR/bin/$target" \
    "$BUILD_DIR/bin/tests/$target"; do
    [ -x "$candidate" ] && SEC_BIN="$candidate" && break
  done

  if [ -z "$SEC_BIN" ]; then
    log "  Building $target..."
    set +e
    cmake --build "$BUILD_DIR" --target "$target" --parallel 2 2>/dev/null
    set -e
    for candidate in \
      "$BUILD_DIR/$target" \
      "$BUILD_DIR/tests/$target" \
      "$BUILD_DIR/bin/$target" \
      "$BUILD_DIR/bin/tests/$target"; do
      [ -x "$candidate" ] && SEC_BIN="$candidate" && break
    done
  fi

  if [ -z "$SEC_BIN" ]; then
    log "  [FAIL] $display (binary not found/built)"
    TEST_FAIL=$((TEST_FAIL + 1))
    TEST_EXIT_CODES+=(1)
    TEST_OUTPUT_FILES+=("")
    continue
  fi

  OUT_FILE="${RESULTS_DIR}/${RID}_${display}.txt"
  set +e
  "$SEC_BIN" > "$OUT_FILE" 2>&1
  EXIT_CODE=$?
  set -e
  TEST_EXIT_CODES+=($EXIT_CODE)
  TEST_OUTPUT_FILES+=("$OUT_FILE")
  if [ "$EXIT_CODE" -eq 0 ]; then
    log "  [PASS] $display"
    TEST_PASS=$((TEST_PASS + 1))
  else
    log "  [FAIL] $display (exit $EXIT_CODE)"
    TEST_FAIL=$((TEST_FAIL + 1))
  fi
done

# ============================================================
# Lane 2: Red Team PoC Mitigation
# ============================================================
log "=== Lane 2: Red Team PoC Mitigation ==="

if ! command -v python3 &>/dev/null; then
  log "WARNING: python3 not found, skipping Red Team PoCs"
  REDTEAM_SKIPPED=true
else
  REDTEAM_SKIPPED=false
fi

POC_NAMES=("poc_wav_divzero" "poc_wav_heap_exhaust" "poc_unitmanager_stoul" "poc_json_stack")
POC_TARGETS=("RTM-001: WAV div-by-zero" "RTM-002: WAV heap exhaustion" "RTM-003: UnitManager stoul" "RTM-004: JSON stack exhaustion")
POC_STATUSES=()
POC_OUTPUT_FILES=()
POC_MITIGATED=0
POC_OPEN=0

if ! $REDTEAM_SKIPPED; then
  POC_TMPDIR=$(mktemp -d)
  trap "rm -rf $POC_TMPDIR" EXIT

  for i in "${!POC_NAMES[@]}"; do
    poc="${POC_NAMES[$i]}"
    target="${POC_TARGETS[$i]}"
    POC_SCRIPT="${REDTEAM_DIR}/${poc}.py"

    if [ ! -f "$POC_SCRIPT" ]; then
      log "  [SKIP] $poc (script not found)"
      POC_STATUSES+=("skipped")
      POC_OUTPUT_FILES+=("")
      continue
    fi

    OUT_FILE="${RESULTS_DIR}/${RID}_${poc}.txt"
    set +e
    # Run PoC — it generates a test file and reports whether it would crash
    python3 "$POC_SCRIPT" "${POC_TMPDIR}/${poc}.test" > "$OUT_FILE" 2>&1
    EXIT_CODE=$?
    set -e

    # PoC scripts exit 0 when they successfully generate the PoC file.
    # We need to check whether the current codebase handles the PoC.
    # For now, we capture the output and mark based on known fix status.
    # A more thorough check would attempt to load the PoC file into Aestra.
    POC_OUTPUT_FILES+=("$OUT_FILE")

    # Check mitigation status by examining source code guards
    mitigated=false
    case "$poc" in
      poc_wav_divzero)
        # Check MiniAudioDecoder.cpp for bitsPerSample guard
        if grep -q 'bitsPerSample' "${REPO_ROOT}/AestraAudio/src/IO/MiniAudioDecoder.cpp" 2>/dev/null; then
          # Check for explicit zero guard or validation
          if grep -qE '(bitsPerSample\s*==\s*0|bitsPerSample\s*!=\s*(16|24|32))' "${REPO_ROOT}/AestraAudio/src/IO/MiniAudioDecoder.cpp" 2>/dev/null; then
            mitigated=true
          fi
        fi
        # Also check MetronomeEngine WAV parser
        if grep -qE '(bitsPerSample\s*==\s*0|bitsPerSample\s*!=\s*(16|24|32))' "${REPO_ROOT}/AestraAudio/src/Playback/MetronomeEngine.cpp" 2>/dev/null; then
          mitigated=true
        fi
        ;;
      poc_wav_heap_exhaust)
        # Check for dataSize cap or max samples guard
        if grep -qE '(kMaxSamples|maxSamples|dataSize.*fileSize|samplesCount.*>)' "${REPO_ROOT}/AestraAudio/src/IO/MiniAudioDecoder.cpp" 2>/dev/null; then
          mitigated=true
        fi
        ;;
      poc_unitmanager_stoul)
        # Check for try/catch around stoul in UnitManager.cpp
        if grep -qE 'try\s*\{' "${REPO_ROOT}/AestraAudio/src/Models/UnitManager.cpp" 2>/dev/null && \
           grep -qE 'catch.*exception|catch\s*\(' "${REPO_ROOT}/AestraAudio/src/Models/UnitManager.cpp" 2>/dev/null; then
          mitigated=true
        fi
        ;;
      poc_json_stack)
        # Check for kMaxJsonDepth in AestraJSON.h
        if grep -qE 'kMaxJsonDepth|MAX_JSON_DEPTH|max_depth' "${REPO_ROOT}/AestraCore/include/AestraJSON.h" 2>/dev/null; then
          mitigated=true
        fi
        ;;
    esac

    if $mitigated; then
      log "  [MITIGATED] $poc ($target)"
      POC_STATUSES+=("mitigated")
      POC_MITIGATED=$((POC_MITIGATED + 1))
    else
      log "  [OPEN] $poc ($target)"
      POC_STATUSES+=("open")
      POC_OPEN=$((POC_OPEN + 1))
    fi
  done
fi

# ============================================================
# Lane 3: Regression Tests
# ============================================================
log "=== Lane 3: Regression Tests ==="

# Try to locate MathTests and AestraFilterTest
MATH_BIN=""
FILTER_BIN=""
for candidate in \
  "$BUILD_DIR/MathTests" "$BUILD_DIR/AestraCore/MathTests" "$BUILD_DIR/bin/MathTests"; do
  [ -x "$candidate" ] && MATH_BIN="$candidate" && break
done
for candidate in \
  "$BUILD_DIR/AestraFilterTest" "$BUILD_DIR/tests/AestraFilterTest" "$BUILD_DIR/bin/AestraFilterTest"; do
  [ -x "$candidate" ] && FILTER_BIN="$candidate" && break
done

MATH_EXIT=0; MATH_OUT="${RESULTS_DIR}/${RID}_regression_math.txt"
FILTER_EXIT=0; FILTER_OUT="${RESULTS_DIR}/${RID}_regression_filter.txt"

if [ -n "$MATH_BIN" ]; then
  set +e
  "$MATH_BIN" > "$MATH_OUT" 2>&1
  MATH_EXIT=$?
  set -e
  log "  MathTests exit code: $MATH_EXIT"
else
  log "  MathTests binary not found, skipping"
fi

if [ -n "$FILTER_BIN" ]; then
  set +e
  printf 'n\n' | "$FILTER_BIN" > "$FILTER_OUT" 2>&1
  FILTER_EXIT=$?
  set -e
  log "  AestraFilterTest exit code: $FILTER_EXIT"
else
  log "  AestraFilterTest binary not found, skipping"
fi

# ============================================================
# Decision
# ============================================================
DECISION="accept"
HARD_FAILURES="[]"
ADVISORY_FLAGS="[]"

# Lane 1 failures
if [ "$TEST_FAIL" -gt 0 ]; then
  DECISION="reject"
  FAILED_NAMES=""
  for i in "${!TEST_EXIT_CODES[@]}"; do
    if [ "${TEST_EXIT_CODES[$i]}" -ne 0 ]; then
      FAILED_NAMES="${FAILED_NAMES}${TEST_DISPLAY_NAMES[$i]} (exit ${TEST_EXIT_CODES[$i]}), "
    fi
  done
  HARD_FAILURES="[\"Security tests failed: ${FAILED_NAMES%, }\"]"
fi

# Lane 2 failures (open critical PoCs)
if [ "$POC_OPEN" -gt 0 ]; then
  DECISION="reject"
  OPEN_NAMES=""
  for i in "${!POC_STATUSES[@]}"; do
    if [ "${POC_STATUSES[$i]}" = "open" ]; then
      OPEN_NAMES="${OPEN_NAMES}${POC_NAMES[$i]}, "
    fi
  done
  HARD_FAILURES="[\"Red team PoCs not mitigated: ${OPEN_NAMES%, }\"]"
fi

# Lane 3 regressions
if [ "$MATH_EXIT" -ne 0 ]; then
  DECISION="reject"
  HARD_FAILURES="[\"MathTests regression (exit $MATH_EXIT)\"]"
fi

if [ "$FILTER_EXIT" -ne 0 ]; then
  DECISION="reject"
  HARD_FAILURES="[\"AestraFilterTest regression (exit $FILTER_EXIT)\"]"
fi

if $GIT_DIRTY; then
  ADVISORY_FLAGS='["dirty_worktree"]'
fi

# Advisory: monitor-status PoCs
if [ "$POC_MITIGATED" -lt "${#POC_NAMES[@]}" ] && ! $REDTEAM_SKIPPED; then
  MONITOR_POCS=""
  for i in "${!POC_STATUSES[@]}"; do
    if [ "${POC_STATUSES[$i]}" != "mitigated" ] && [ "${POC_STATUSES[$i]}" != "skipped" ]; then
      MONITOR_POCS="${MONITOR_POCS}${POC_NAMES[$i]}, "
    fi
  done
  if [ -n "$MONITOR_POCS" ]; then
    ADVISORY_FLAGS="[\"monitor_status_pocs: ${MONITOR_POCS%, }\"]"
  fi
fi

REASON="All gates passed."
[ "$DECISION" = "reject" ] && REASON="Hard gate failure(s)"

# Build security tests JSON array
SEC_TESTS_JSON="["
for i in "${!TEST_DISPLAY_NAMES[@]}"; do
  [ "$i" -gt 0 ] && SEC_TESTS_JSON+=","
  SEC_TESTS_JSON+="{\"name\":\"${TEST_DISPLAY_NAMES[$i]}\",\"exit_code\":${TEST_EXIT_CODES[$i]},\"output_file\":\"${TEST_OUTPUT_FILES[$i]}\"}"
done
SEC_TESTS_JSON+="]"

# Build red team PoCs JSON array
POC_JSON="["
for i in "${!POC_NAMES[@]}"; do
  [ "$i" -gt 0 ] && POC_JSON+=","
  OUT="${POC_OUTPUT_FILES[$i]:-}"
  POC_JSON+="{\"name\":\"${POC_NAMES[$i]}\",\"target_vuln\":\"${POC_TARGETS[$i]}\",\"status\":\"${POC_STATUSES[$i]:-skipped}\",\"output_file\":\"${OUT}\"}"
done
POC_JSON+="]"

cat > "$SUMMARY_FILE" <<EOJSON
{
  "run_id": "$RID",
  "timestamp": "$TS",
  "git": { "commit": "$GIT_COMMIT", "branch": "$GIT_BRANCH", "dirty": $GIT_DIRTY },
  "lanes": {
    "security_tests": {
      "tests": $SEC_TESTS_JSON,
      "pass_count": $TEST_PASS,
      "fail_count": $TEST_FAIL,
      "total_count": ${#SEC_TEST_NAMES[@]}
    },
    "redteam_pocs": {
      "pocs": $POC_JSON,
      "mitigated_count": $POC_MITIGATED,
      "open_count": $POC_OPEN,
      "total_count": ${#POC_NAMES[@]}
    },
    "regression": {
      "math_tests": { "exit_code": $MATH_EXIT, "output_file": "$MATH_OUT" },
      "filter_tests": { "exit_code": $FILTER_EXIT, "output_file": "$FILTER_OUT" }
    }
  },
  "decision": { "status": "$DECISION", "reason": "$REASON", "hard_gate_failures": $HARD_FAILURES, "advisory_flags": $ADVISORY_FLAGS }
}
EOJSON

log "Decision: $DECISION | Summary: $SUMMARY_FILE"
[ "$DECISION" = "reject" ] && exit 1
exit 0
