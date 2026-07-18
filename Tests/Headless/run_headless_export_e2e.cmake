# End-to-end driver for AestraHeadlessExport.
#
# Runs the real export binary through the full path — argument parsing, template
# project generation, and the export attempt — and enforces the current contract:
#
#   exit 0  full export success -> the output WAV must exist and contain audio
#   exit 1  graceful, diagnosed failure -> allowed FOR NOW: the
#           HeadlessMusicGenerator commit layer is stubbed (logs "Committed N"
#           but populates nothing), so the exporter correctly reports an empty
#           timeline. Tracked in issue #556; when that feature lands, tighten
#           this driver to require exit 0 + WAV validation.
#   anything else (exit 2 = arg error on valid args, or a signal such as
#           "Subprocess aborted") -> regression. On develop this run died with
#           SIGABRT from AudioEngine::getInstance() before any engine existed.
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

if(exit_code STREQUAL "0")
    # Full success claimed: hold the binary to it.
    if(NOT EXISTS "${OUT_FILE}")
        message(FATAL_ERROR "Export reported success but produced no output at '${OUT_FILE}'.")
    endif()
    file(SIZE "${OUT_FILE}" out_size)
    # A bare WAV header is 44 bytes; require substantially more so we know real
    # audio frames were written, not just an empty container.
    if(out_size LESS 1024)
        message(FATAL_ERROR "Export output '${OUT_FILE}' is ${out_size} bytes — too small to contain audio.")
    endif()
    message(STATUS "Export E2E OK: ${OUT_FILE} (${out_size} bytes)")
    file(REMOVE "${OUT_FILE}")
elseif(exit_code STREQUAL "1")
    # Graceful diagnosed failure. Acceptable only while the generator commit
    # layer is unimplemented — but it must be a *diagnosed* failure, never a
    # crash, and never a silent success with no file.
    if(stderr_log MATCHES "getInstance\\(\\) called before engine was created")
        message(FATAL_ERROR "Export path still depends on the AudioEngine singleton.")
    endif()
    message(STATUS "Export failed gracefully (generator commit layer not yet implemented) — no crash.")
else()
    message(FATAL_ERROR
        "AestraHeadlessExport terminated abnormally (result: '${exit_code}'). "
        "A signal here (e.g. 'Subprocess aborted') means the export path crashed "
        "instead of failing diagnostically.")
endif()
