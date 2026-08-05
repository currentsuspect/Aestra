# End-to-end driver for AestraHeadlessExport.
#
# Runs the real export binary through the full path — argument parsing, template
# project generation, the generator commit layer, and the render — and enforces
# the contract that headless export actually produces audible audio (#556):
#
#   exit 0  -> the output WAV must exist, be substantially larger than a bare
#              header, and the render's reported peak must be above the silence
#              floor (the stubbed commit layer used to render an empty timeline
#              at the -96 dB noise floor; a working render lands far louder).
#   exit 1  -> failure. No longer tolerated: the generator commit layer is
#              implemented, so an empty/silent timeline is a regression.
#   anything else (exit 2 = arg error on valid args, or a signal such as
#              "Subprocess aborted") -> regression. On develop the pre-#557 path
#              died with SIGABRT from AudioEngine::getInstance().
#
# Required -D args:
#   EXPORT_BIN  path to the AestraHeadlessExport executable
#   OUT_FILE    path to write the exported .wav to (removed on success)

if(NOT EXPORT_BIN OR NOT OUT_FILE)
    message(FATAL_ERROR "EXPORT_BIN and OUT_FILE must be provided")
endif()

file(REMOVE "${OUT_FILE}")

# Smallest valid session this binary can express: one bar of the sparse
# "Minimal" template.
execute_process(
    COMMAND "${EXPORT_BIN}"
            --template Minimal
            --duration 1
            --sample-rate 48000
            --output "${OUT_FILE}"
    RESULT_VARIABLE exit_code
    OUTPUT_VARIABLE stdout_log
    ERROR_VARIABLE  stderr_log
)

message(STATUS "AestraHeadlessExport exit: ${exit_code}")
message(STATUS "AestraHeadlessExport stdout:\n${stdout_log}")
message(STATUS "AestraHeadlessExport stderr:\n${stderr_log}")

set(combined_log "${stdout_log}${stderr_log}")

# Singleton regression check first, independent of exit code: the abort path
# dies on a signal (exit_code becomes e.g. "Subprocess aborted", not "1"), so
# this must run before the exit-code dispatch to produce the specific diagnosis.
if(combined_log MATCHES "getInstance\\(\\) called before engine was created")
    message(FATAL_ERROR "Export path still depends on the AudioEngine singleton "
                        "(getInstance() called before any engine was constructed).")
endif()

if(NOT exit_code STREQUAL "0")
    message(FATAL_ERROR
        "AestraHeadlessExport did not succeed (result: '${exit_code}'). "
        "Exit 1 means the generator commit layer produced an empty/failed render; "
        "a signal means the export path crashed. Headless export must exit 0 with "
        "audible output (#556).")
endif()

# --- exit 0: hold the binary to a real, audible render ----------------------
if(NOT EXISTS "${OUT_FILE}")
    message(FATAL_ERROR "Export reported success but produced no output at '${OUT_FILE}'.")
endif()
file(SIZE "${OUT_FILE}" out_size)
# A bare WAV header is 44 bytes; require substantially more so we know real
# audio frames were written, not just an empty container.
if(out_size LESS 1024)
    message(FATAL_ERROR "Export output '${OUT_FILE}' is ${out_size} bytes — too small to contain audio.")
endif()

# Non-silence: parse the render's reported peak (dBFS) and require it above the
# silence floor. The stubbed commit layer rendered an empty timeline at the
# -96 dB noise floor; a real render is far louder (the Minimal template lands
# near -22 dBFS). Capture the integer dB part for a robust integer compare.
if(NOT combined_log MATCHES "peak: (-?[0-9]+)")
    message(FATAL_ERROR "Export produced no peak-level report to validate against silence.")
endif()
set(peak_db "${CMAKE_MATCH_1}")
if(NOT peak_db GREATER -80)
    message(FATAL_ERROR
        "Export output peaked at ${peak_db} dBFS — at or below the silence floor. "
        "The render produced no audible signal (empty/unrouted timeline).")
endif()

message(STATUS "Export E2E OK: ${OUT_FILE} (${out_size} bytes, peak ${peak_db} dBFS)")
file(REMOVE "${OUT_FILE}")
