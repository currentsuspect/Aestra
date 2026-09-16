# Self-test for ui_surface_resolution.cmake — pins the ratchet's contract.
#
# Materializes fixture trees in WORK_DIR/tree and runs the real guard against each,
# asserting PASS/FAIL per fixture. Every fixture starts from the exact pinned baseline
# (25 rect assignments including 3 NUIRect literals in Source/Core/AestraContent.cpp, plus
# 2 placement-region definitions and 6 isMaximized reads in the same file, plus 1
# editor->setBounds in AestraUI/Widgets/PluginUIController.cpp) and then applies one
# mutation, so each result isolates exactly one guard behavior.
#
# Required -D args:
#   GUARD_SCRIPT  absolute path to ui_surface_resolution.cmake
#   WORK_DIR      scratch directory (recreated per fixture)

if(NOT GUARD_SCRIPT OR NOT WORK_DIR)
    message(FATAL_ERROR "GUARD_SCRIPT and WORK_DIR must be provided")
endif()

set(failures 0)

# Rebuild WORK_DIR/tree at the exact pinned baseline (25 + 3-subset + 2 + 6, and 1).
function(write_baseline_tree)
    file(REMOVE_RECURSE "${WORK_DIR}/tree")
    file(MAKE_DIRECTORY "${WORK_DIR}/tree/Source/Core")
    file(MAKE_DIRECTORY "${WORK_DIR}/tree/AestraUI/Widgets")
    file(WRITE "${WORK_DIR}/tree/Source/Core/AestraContent.cpp" [=[
void AestraContent::restoreViewState() {
    m_viewState.sequencerRect = rect;
    m_viewState.mixerRect = rect;
    m_viewState.pianoRollRect = rect;
    m_viewState.historyRect = rect;
    m_viewState.takesRect = rect;
    m_viewState.mixerRect = clampRectToAllowed(m_viewState.mixerRect, allowed);
    m_viewState.pianoRollRect = clampRectToAllowed(m_viewState.pianoRollRect, allowed);
    m_viewState.sequencerRect = clampRectToAllowed(m_viewState.sequencerRect, allowed);
    m_viewState.historyRect = clampRectToAllowed(m_viewState.historyRect, allowed);
    m_viewState.takesRect = clampRectToAllowed(m_viewState.takesRect, allowed);
    m_viewState.dragStartRect = m_viewState.mixerRect;
    m_viewState.dragStartRect = m_viewState.pianoRollRect;
    m_viewState.dragStartRect = m_viewState.sequencerRect;
    m_viewState.dragStartRect = m_viewState.historyRect;
    m_viewState.dragStartRect = m_viewState.takesRect;
    m_viewState.mixerRect = finalRect;
    m_viewState.pianoRollRect = finalRect;
    m_viewState.sequencerRect = finalRect;
    m_viewState.historyRect = finalRect;
    m_viewState.takesRect = finalRect;
    m_viewState.*stateRect = clampRectToAllowed(proposed, allowed);
    m_viewState.browserRect = rect;
    m_viewState.pianoRollRect = AestraUI::NUIRect(editorX, editorY, editorWidth, editorHeight);
    m_viewState.historyRect = AestraUI::NUIRect(x, y, w, h);
    m_viewState.takesRect = AestraUI::NUIRect(x, y, w, h);
    if (!m_sequencerPanel || m_sequencerPanel->isMaximized()) {
    }
    if (m_mixerPanel->isMaximized()) {
    }
    if (m_pianoRollPanel->isMaximized()) {
    }
    if (m_sequencerPanel->isMaximized()) {
    }
    if (m_historyPanel->isMaximized()) {
    }
    if (m_takesPanel->isMaximized()) {
    }
}

AestraUI::NUIRect AestraContent::computeAllowedRectForPanels() const {
    return safe;
}

AestraUI::NUIRect AestraContent::computeMaximizedRect() const {
    return maxRect;
}
]=])
    file(WRITE "${WORK_DIR}/tree/AestraUI/Widgets/PluginUIController.cpp" [=[
void PluginUIController::centerEditor(Editor* editor) {
    float x = (layerBounds.width - editorWidth) * 0.5f;
    float y = (layerBounds.height - editorHeight) * 0.5f;
    editor->setBounds(x, y, editorWidth, editorHeight);
    relayoutEditor(editor, editorWidth, editorHeight);
}
]=])
endfunction()

# run_guard(<name> <expect>)  expect: PASS|FAIL — runs the guard on the current tree.
function(run_guard name expect)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -DREPO_ROOT=${WORK_DIR}/tree -P "${GUARD_SCRIPT}"
        RESULT_VARIABLE rc
        OUTPUT_QUIET ERROR_QUIET)
    if(expect STREQUAL "FAIL" AND rc EQUAL 0)
        message(SEND_ERROR "fixture '${name}': guard PASSED but must FAIL")
        set(failures 1 PARENT_SCOPE)
    elseif(expect STREQUAL "PASS" AND NOT rc EQUAL 0)
        message(SEND_ERROR "fixture '${name}': guard FAILED but must PASS")
        set(failures 1 PARENT_SCOPE)
    else()
        message(STATUS "fixture '${name}': ok (${expect})")
    endif()
endfunction()

# ── Baseline exactly met → PASS ────────────────────────────────────────────────
write_baseline_tree()
run_guard(baseline-exact PASS)

# ── One extra rect assignment (25 → 26) → FAIL (count rose) ────────────────────
write_baseline_tree()
file(APPEND "${WORK_DIR}/tree/Source/Core/AestraContent.cpp" "    m_viewState.extraRect = rect;\n")
run_guard(count-rose FAIL)

# ── Same pattern in a file outside the table → FAIL (spread) ──────────────────
write_baseline_tree()
file(WRITE "${WORK_DIR}/tree/Source/Core/PanelStore.cpp" [=[
void PanelStore::restore() {
    m_viewState.otherRect = rect;
}
]=])
run_guard(spread-to-new-file FAIL)

# ── One fewer than baseline (25 → 24) → FAIL (stale baseline) ─────────────────
write_baseline_tree()
file(READ "${WORK_DIR}/tree/Source/Core/AestraContent.cpp" stale_contents)
string(REPLACE "    m_viewState.browserRect = rect;\n" "" stale_contents "${stale_contents}")
file(WRITE "${WORK_DIR}/tree/Source/Core/AestraContent.cpp" "${stale_contents}")
run_guard(stale-baseline FAIL)

# ── dragStartRect IS a rect assignment, not an exemption → FAIL ────────────────
# Pins that the pattern covers any *Rect member: one more dragStartRect is a rise.
write_baseline_tree()
file(APPEND "${WORK_DIR}/tree/Source/Core/AestraContent.cpp" "    m_viewState.dragStartRect = m_viewState.mixerRect;\n")
run_guard(dragstartrect-counts FAIL)

# ── Today's capture* names match nothing (no R3 pattern yet) → PASS ───────────
write_baseline_tree()
file(APPEND "${WORK_DIR}/tree/AestraUI/Widgets/PluginUIController.cpp" [=[
void PluginUIController::noteCapture(Unit* unit) {
    captureUnitPluginState(unit);
    m_captureKnobCenter = knobCenter;
}
]=])
run_guard(benign-capture-names PASS)

# ── isMaximized() outside AestraContent.cpp is out of scope → PASS ─────────────
# Pins the R6 scoping note: the OS-window read in captureWindowState() style code
# must not trip the panel-maximized-read entry.
write_baseline_tree()
file(WRITE "${WORK_DIR}/tree/Source/Core/AestraWindowManager.cpp" [=[
void AestraWindowManager::save() {
    state.maximized = m_window->isMaximized();
}
]=])
run_guard(r6-scoped-to-content PASS)

# ── */External/* copies are vendored, not ours → PASS ─────────────────────────
write_baseline_tree()
file(MAKE_DIRECTORY "${WORK_DIR}/tree/Source/External")
file(WRITE "${WORK_DIR}/tree/Source/External/VendoredPanels.cpp" [=[
void vendored() {
    m_viewState.vendoredRect = rect;
    m_viewState.vendoredRect = AestraUI::NUIRect(x, y, w, h);
    editor->setBounds(x, y, w, h);
    if (panel->isMaximized()) {
    }
}
]=])
run_guard(external-not-policed PASS)

file(REMOVE_RECURSE "${WORK_DIR}/tree")

if(failures)
    message(FATAL_ERROR "UISurfaceResolutionGuard self-test: FAILED")
endif()
message(STATUS "UISurfaceResolutionGuard self-test: all fixtures ok")
