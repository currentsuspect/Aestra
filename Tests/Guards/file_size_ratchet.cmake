# FileSizeRatchetGuard — no first-party source file grows into a hub.
#
# A file that everything gets wired into does not announce itself. It grows a
# few dozen lines per feature, every one of them reasonable in review, until no
# reviewer can say what a change to it might touch. The tree has one candidate
# already (Source/Core/AestraContent.cpp, the panel hub P5 started shrinking),
# and the failure mode is well documented elsewhere: a 100k-line main.cpp whose
# reviewers can no longer bound the effect of a 20-line diff.
#
# The rule, in two parts:
#
#   1. CAP. No first-party .cpp/.h/.hpp/.mm/.c file may exceed kCap lines.
#   2. RATCHET. Files already over the cap are pinned in the baseline table
#      (file_size_baseline.txt) at their exact line count. They may shrink; they
#      may not grow.
#
# Every way the table can drift is a failure, matching ui_surface_resolution:
#
#   - a pinned file GROWS past its baseline             -> split it, or raise its
#                                                          row in the same PR (the
#                                                          explicit exception, and
#                                                          a reviewable edit)
#   - a pinned file SHRINKS below its baseline (stale)  -> lower the row, so the
#                                                          improvement is kept
#   - a pinned file drops to the cap or below           -> delete its row
#   - a pinned file no longer exists                    -> delete its row
#   - an UNPINNED file exceeds the cap                  -> split it, or pin it
#
# Lines are newline characters, the same number `wc -l` prints, so the
# baseline can be regenerated with:
#
#   git ls-files Source AestraUI AestraAudio AestraCore AestraPlat AestraLicense \
#     | grep -E '\.(cpp|h|hpp|mm|c)$' | grep -v '/External/' \
#     | xargs wc -l | grep -v ' total$' | awk '$1 > 1500 {print $1, $2}' | sort -k2
#
# Contract limits (deliberate): this bounds size, not coupling. A 1,400-line
# file can still be a hub. It is the cheap half of the contract, and the half
# that cannot be argued with in review.
#
# Required -D args:
#   REPO_ROOT  absolute path to the repository root
# Optional -D args:
#   BASELINE   path to the baseline table (default: the one next to this script)

if(NOT REPO_ROOT)
    message(FATAL_ERROR "REPO_ROOT must be provided")
endif()
if(NOT BASELINE)
    set(BASELINE "${REPO_ROOT}/Tests/Guards/file_size_baseline.txt")
endif()

set(kCap 1500)

set(scan_dirs
    Source
    AestraUI
    AestraAudio
    AestraCore
    AestraPlat
    AestraLicense
)

function(count_lines path out)
    file(READ "${path}" contents)
    string(REGEX MATCHALL "\n" newlines "${contents}")
    list(LENGTH newlines n)
    set(${out} ${n} PARENT_SCOPE)
endfunction()

# ── Baseline table ────────────────────────────────────────────────────────────
# One row per pinned file: "<lines> <repo-relative path>". Blank lines and
# '#' comments are ignored.
set(pinned_paths "")
set(violations "")
if(EXISTS "${BASELINE}")
    file(STRINGS "${BASELINE}" rows)
else()
    set(rows "")
endif()
foreach(row ${rows})
    string(STRIP "${row}" row)
    if(row STREQUAL "" OR row MATCHES "^#")
        continue()
    endif()
    if(NOT row MATCHES "^([0-9]+)[ \t]+([^ \t]+)$")
        list(APPEND violations "baseline row is malformed (want '<lines> <path>'): ${row}")
        continue()
    endif()
    set(expected "${CMAKE_MATCH_1}")
    set(rel "${CMAKE_MATCH_2}")
    list(APPEND pinned_paths "${rel}")
    set(pinned_${rel} "${expected}")

    if(NOT EXISTS "${REPO_ROOT}/${rel}")
        list(APPEND violations "${rel}: pinned in the baseline but no longer exists. Delete its row.")
        continue()
    endif()
    count_lines("${REPO_ROOT}/${rel}" actual)
    if(actual GREATER expected)
        list(APPEND violations
            "${rel}: GREW to ${actual} lines (pinned at ${expected}). Split it, or raise its row in this PR and say why in the description.")
    elseif(actual LESS_EQUAL kCap)
        list(APPEND violations
            "${rel}: now ${actual} lines, at or under the ${kCap}-line cap. Delete its row from the baseline.")
    elseif(actual LESS expected)
        list(APPEND violations
            "${rel}: baseline is stale, lower its row to ${actual} (pinned at ${expected}). Record the improvement so a later regression cannot hide inside it.")
    endif()
endforeach()

# ── Cap on every unpinned file ────────────────────────────────────────────────
foreach(dir ${scan_dirs})
    if(NOT EXISTS "${REPO_ROOT}/${dir}")
        continue()
    endif()
    file(GLOB_RECURSE files
        "${REPO_ROOT}/${dir}/*.cpp"
        "${REPO_ROOT}/${dir}/*.h"
        "${REPO_ROOT}/${dir}/*.hpp"
        "${REPO_ROOT}/${dir}/*.mm"
        "${REPO_ROOT}/${dir}/*.c")
    foreach(f ${files})
        if(f MATCHES "/External/")
            continue()
        endif()
        string(REPLACE "${REPO_ROOT}/" "" rel "${f}")
        if(DEFINED pinned_${rel})
            continue()
        endif()
        count_lines("${f}" actual)
        if(actual GREATER kCap)
            list(APPEND violations
                "${rel}: ${actual} lines, over the ${kCap}-line cap. Split it; or, if it genuinely must be one unit, pin it in Tests/Guards/file_size_baseline.txt in this PR and say why.")
        endif()
    endforeach()
endforeach()

list(LENGTH pinned_paths pinned_count)
if(violations)
    list(LENGTH violations n)
    message("FileSizeRatchetGuard: ${n} violation(s)")
    foreach(v ${violations})
        message("  ${v}")
    endforeach()
    message(FATAL_ERROR "FileSizeRatchetGuard failed")
endif()
message("FileSizeRatchetGuard: clean (cap ${kCap} lines, ${pinned_count} file(s) pinned)")
