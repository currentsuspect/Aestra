# Self-test for ui_surface_resolution.cmake — pins the ratchet's contract.
#
# Materializes fixture trees in WORK_DIR/tree and runs the real guard against each,
# asserting PASS/FAIL per fixture. Every fixture starts from the exact pinned baseline
# (no ViewState rect assignments and no NUIRect literals in Source/Core/AestraContent.cpp,
# no isMaximized reads there, the single computePlacementRegion definition, plus 1
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

# Rebuild WORK_DIR/tree at the exact pinned baseline (0 + 0-subset + 1 + 0, and 1).
function(write_baseline_tree)
    file(REMOVE_RECURSE "${WORK_DIR}/tree")
    file(MAKE_DIRECTORY "${WORK_DIR}/tree/Source/Core")
    file(MAKE_DIRECTORY "${WORK_DIR}/tree/AestraUI/Widgets")
    file(WRITE "${WORK_DIR}/tree/Source/Core/AestraContent.cpp" [=[
void AestraContent::applyPanelPreference(Audio::ViewType view) {
    const UISurfaceGeometry preference = panelPreference(view);
    const Layout::NUIWindowRect region = toWindowRect(computePlacementRegion());
}

AestraUI::NUIRect AestraContent::computePlacementRegion() const {
    return safe;
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

# ── One extra rect assignment (0 → 1) → FAIL (count rose) ────────────────────
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

# ── One fewer than baseline (editor 1 → 0) → FAIL (stale baseline) ───────────
# The zero-tolerance entries cannot fall further, so the stale pin lives on the
# one remaining counted entry: removing the editor hand-centring without
# updating the table must fail.
write_baseline_tree()
file(READ "${WORK_DIR}/tree/AestraUI/Widgets/PluginUIController.cpp" stale_contents)
string(REPLACE "    editor->setBounds(x, y, editorWidth, editorHeight);\n" "" stale_contents
               "${stale_contents}")
file(WRITE "${WORK_DIR}/tree/AestraUI/Widgets/PluginUIController.cpp" "${stale_contents}")
run_guard(stale-baseline FAIL)

# ── dragStartRect IS a rect assignment, not an exemption → FAIL ────────────────
# Pins that the pattern covers any *Rect member: one more dragStartRect is a rise.
write_baseline_tree()
file(APPEND "${WORK_DIR}/tree/Source/Core/AestraContent.cpp" "    m_viewState.dragStartRect = m_viewState.mixerRect;\n")
run_guard(dragstartrect-counts FAIL)

# ── Spaced `editor->setBounds (` still counts as editor hand-centring → FAIL ──
# Pins the R4 whitespace variant: valid C++ spacing must not bypass the ratchet.
write_baseline_tree()
file(APPEND "${WORK_DIR}/tree/AestraUI/Widgets/PluginUIController.cpp" "    editor->setBounds (x, y, editorWidth, editorHeight);\n")
run_guard(spaced-setbounds-counts FAIL)

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

# ── Spaced `isMaximized ()` still counts as a panel-maximized read → FAIL ─────
# Pins the R6 whitespace variant: valid C++ spacing must not bypass the ratchet.
write_baseline_tree()
file(APPEND "${WORK_DIR}/tree/Source/Core/AestraContent.cpp" "    if (m_mixerPanel->isMaximized ()) {\n    }\n")
run_guard(spaced-ismaximized-counts FAIL)

# ── A second placement-region definition (1 → 2) → FAIL (count rose) ─────────
# Pins the R5 exactly-one invariant: the region function must not reduplicate.
write_baseline_tree()
file(APPEND "${WORK_DIR}/tree/Source/Core/AestraContent.cpp" [=[
AestraUI::NUIRect AestraContent::computePlacementRegion() const {
    return other;
}
]=])
run_guard(duplicate-region-fn FAIL)

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
