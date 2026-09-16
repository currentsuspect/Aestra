# UISurfaceResolutionGuard — ratchet source check for the V8-C14 surface-resolution rules.
#
# Founder's invariant (quoted once, enforced below):
#   "A persistent surface must not unconditionally establish user-owned geometry during
#    construction/open. Initial geometry must flow through the layout/state-resolution path."
#
# Context: FD-23 orders V8-C14 in six binding steps. Steps 1-5 merged (store schema,
# resolution contract in Source/Core/UISurfaceResolution.h, mixer-inspector reference
# migration, this CI guard, the floating-panel migration, and the plugin-editor
# migration). This guard exists so the migrated surfaces cannot regress and so no
# new violation can land while step 5c (docked widths) and step 6 (ranked gaps)
# proceed.
#
# Why a ratchet instead of zero-tolerance: R1 and R3 cannot be enforced positively
# today. Source/Core/AestraContent.cpp is the first production consumer of
# Source/Core/UISurfaceResolution.h (step 5a), but R1 ("every setBounds comes
# from a resolve result") still has no positive pattern — the ratchet pins the
# violation shapes at zero instead, so any reintroduction fails the build.
# There is deliberately NO R3 capture* pattern yet (see scoping notes below);
# R3 enforcement waits for step 5b, when capture*Placement call sites exist
# beyond AestraContent.
#
# The ratchet: today's known violations are pinned as the baseline table below. The build
# fails if any count RISES, if a pattern appears in a file NOT in the table (spread), or if
# a count FALLS without the table being updated (stale baseline — an unrecorded improvement
# lets a later regression hide). Steps 5a+5b drove every entry to its floor (four to
# zero-tolerance, the region function to exactly one); step 5c (docked widths) and
# step 6 (ranked gaps) proceed under it.
#
# Load-bearing scoping decisions (do not "fix" without a V8-C14 step behind you):
#   - R6 is scoped to Source/Core/AestraContent.cpp ONLY. Source/Core/AestraWindowManager.cpp
#     reads isMaximized() for the OS window in captureWindowState(), which is not a
#     persistent panel surface, and AestraUI owns several legitimate isMaximized()
#     definitions and reads (NUICustomWindow, NUIPlatformBridge, ...). The
#     panel-maximized-read entry therefore does not spread-scan the tree.
#   - There is deliberately NO R3 capture* pattern yet. capture* in this tree today means
#     captureUnitPluginState, m_captureKnobCenter, captureWindowState — none of them the
#     contract's capture*Placement — so any R3 regex would either false-positive on those
#     or match nothing. R3 enforcement is step 5's, when real capture*Placement call sites
#     exist. The self-test pins that the current capture* names match nothing.
#   - Vendored trees (*/External/*) are not ours to police and are skipped everywhere.
#   - Spread detection covers Source/ and AestraUI/ (the production surface trees). Tests/
#     holds the resolution contract's own tests and is out of scope.
#
# Baseline table (re-measured after the step-5b plugin-editor migration —
# re-verify every count before editing):
#   key                    rule   file                                       expected
#   viewstate-rect-assign  R2/R3  Source/Core/AestraContent.cpp               0
#   viewstate-rect-literal R4     Source/Core/AestraContent.cpp               0
#   editor-hand-centring   R4     AestraUI/Widgets/PluginUIController.cpp     0
#   placement-region-fn    R5     Source/Core/AestraContent.cpp               1
#   panel-maximized-read   R6     Source/Core/AestraContent.cpp               0
#
# Required -D args:
#   REPO_ROOT  absolute path to the repository root

if(NOT REPO_ROOT)
    message(FATAL_ERROR "REPO_ROOT must be provided")
endif()

# ── Baseline table (data; one entry per key, index-aligned lists) ──────────────
set(entry_keys
    viewstate-rect-assign
    viewstate-rect-literal
    editor-hand-centring
    placement-region-fn
    panel-maximized-read)
set(entry_rules
    "R2/R3"
    "R4"
    "R4"
    "R5"
    "R6")
set(entry_files
    "Source/Core/AestraContent.cpp"
    "Source/Core/AestraContent.cpp"
    "AestraUI/Widgets/PluginUIController.cpp"
    "Source/Core/AestraContent.cpp"
    "Source/Core/AestraContent.cpp")
set(entry_expected 0 0 0 1 0)
# Spread-scan the tree for this key? OFF only for R6 (see scoping note above).
set(entry_scan_tree ON ON ON ON OFF)
# What each entry's floor is and which step owns it (one line each).
# Documentation-as-data: no runtime consumer reads this list — it binds each
# pinned count to the exact change that owns it, so the table cannot be edited
# without a plan.
set(entry_step5
    "Step 5a routed every assignment through resolveSurfacePlacement(...).resolved or capture*Placement: zero-tolerance."
    "Step 5a deleted the NUIRect literal constructions at open: zero-tolerance."
    "Step 5b opens editors from the stored anchor through the anchored-placement resolve path: zero-tolerance."
    "Step 5a replaced both helpers with the single placement-region function: exactly one."
    "Step 5a derives panel maximized flags from pref.maximized: zero-tolerance.")

# Regexes per entry index (_b only where the table row needs two patterns).
# Note: CMake folds \t inside "..." to a literal tab, so [ \t] below is a
# space/tab class both as written and as matched; the [ \t\r\n]* runs after a
# method name also match a newline, so `name (` / `name ()` cannot bypass the
# ratchet. None of these patterns can match a semicolon, so MATCHALL results
# stay well-formed CMake lists.
set(regex_0_a "m_viewState\\.[A-Za-z]*[Rr]ect[ \t]*=")
set(regex_0_b "m_viewState\\.\\*stateRect[ \t]*=")
set(regex_1_a "m_viewState\\.[A-Za-z]*[Rr]ect[ \t]*=[ \t]*AestraUI::NUIRect\\(")
set(regex_2_a "editor->setBounds[ \t\r\n]*\\(")
set(regex_3_a "AestraContent::computePlacementRegion")
set(regex_4_a "isMaximized[ \t\r\n]*\\([ \t\r\n]*\\)")

# count_for_entry(<text> <regex_a> <has_b> <regex_b> <out>): total MATCHALL hits.
function(count_for_entry text regex_a has_b regex_b out)
    string(REGEX MATCHALL "${regex_a}" hits_a "${text}")
    list(LENGTH hits_a n)
    if(has_b)
        string(REGEX MATCHALL "${regex_b}" hits_b "${text}")
        list(LENGTH hits_b m)
        math(EXPR n "${n} + ${m}")
    endif()
    set(${out} "${n}" PARENT_SCOPE)
endfunction()

list(LENGTH entry_keys num_entries)
math(EXPR last_idx "${num_entries} - 1")

set(violations "")

# ── 1. Baseline counts: fail on rise, fail on fall (stale) ─────────────────────
foreach(i RANGE 0 ${last_idx})
    list(GET entry_keys ${i} key)
    list(GET entry_rules ${i} rule)
    list(GET entry_files ${i} file)
    list(GET entry_expected ${i} expected)
    if(DEFINED "regex_${i}_b")
        set(has_b TRUE)
        set(regex_b "${regex_${i}_b}")
    else()
        set(has_b FALSE)
        set(regex_b "")
    endif()
    set(abs "${REPO_ROOT}/${file}")
    if(NOT EXISTS "${abs}")
        list(APPEND violations
            "key '${key}' (${rule}): baseline file ${file} is missing (expected ${expected}).")
    else()
        file(READ "${abs}" baseline_contents)
        count_for_entry("${baseline_contents}" "${regex_${i}_a}" "${has_b}" "${regex_b}" actual)
        if(actual GREATER expected)
            list(APPEND violations
                "key '${key}' (${rule}): count ROSE in ${file} (expected ${expected}, found ${actual}). Route the new geometry through the layout/state-resolution path.")
        elseif(actual LESS expected)
            list(APPEND violations
                "key '${key}' (${rule}): baseline is stale — lower '${key}' to ${actual} in this file (${file}: expected ${expected}, found ${actual}). Record the improvement so a later regression cannot hide.")
        endif()
    endif()
endforeach()

# ── 2. Spread: fail if a pattern matches in any file outside its table entry ──
set(scan_dirs
    Source
    AestraUI)
set(all_files "")
foreach(dir ${scan_dirs})
    if(NOT EXISTS "${REPO_ROOT}/${dir}")
        continue()
    endif()
    file(GLOB_RECURSE dir_files
        "${REPO_ROOT}/${dir}/*.h"
        "${REPO_ROOT}/${dir}/*.hpp"
        "${REPO_ROOT}/${dir}/*.cpp")
    list(APPEND all_files ${dir_files})
endforeach()

foreach(f ${all_files})
    # Vendored third-party trees are not ours to police.
    if(f MATCHES "/External/")
        continue()
    endif()
    string(REPLACE "${REPO_ROOT}/" "" rel "${f}")
    file(READ "${f}" contents)
    foreach(i RANGE 0 ${last_idx})
        list(GET entry_keys ${i} key)
        list(GET entry_rules ${i} rule)
        list(GET entry_files ${i} file)
        list(GET entry_scan_tree ${i} scan)
        if(NOT scan)
            continue()
        endif()
        if(rel STREQUAL file)
            continue()
        endif()
        if(DEFINED "regex_${i}_b")
            set(has_b TRUE)
            set(regex_b "${regex_${i}_b}")
        else()
            set(has_b FALSE)
            set(regex_b "")
        endif()
        count_for_entry("${contents}" "${regex_${i}_a}" "${has_b}" "${regex_b}" actual)
        if(actual GREATER 0)
            list(APPEND violations
                "key '${key}' (${rule}): pattern SPREAD to ${rel} (${actual} match(es)); only ${file} is in the baseline table. Put the geometry through the layout/state-resolution path instead of adding a new violation site.")
        endif()
    endforeach()
endforeach()

if(violations)
    list(JOIN violations "\n  " joined)
    message(FATAL_ERROR
        "UISurfaceResolutionGuard: surface-geometry ratchet violated:\n  ${joined}\n"
        "A persistent surface must not unconditionally establish user-owned geometry during "
        "construction/open. Initial geometry must flow through the layout/state-resolution path. "
        "See the baseline table and scoping notes in Tests/Guards/ui_surface_resolution.cmake.")
endif()

message(STATUS "UISurfaceResolutionGuard: clean (ratchet holds)")
