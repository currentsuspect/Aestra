// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file NUITextInputEditingTest.cpp
 * @brief Regression tests for NUITextInput editing semantics.
 *
 * Locks the contract that broke in practice: **typing over a selection must
 * replace it**, exactly as pasting over a selection already did.
 *
 * NUITextInput has two insertion entry points. insertText() (the paste path)
 * always called deleteSelectedText() first; insertCharacter() (the keyboard
 * path) did not — it inserted at the caret and returned. So selectAll() drew a
 * highlight the keyboard never honoured, and because the caret sits at 0 after
 * a select-all, typed characters were *prepended*.
 *
 * That failure is silent, which is why it survived: a numeric field seeded
 * "6.0" and select-alled became "06.0" when the user typed "0" over it, and
 * "06.0" parses straight back to 6.0 — the edit simply appeared to do nothing.
 * Every field using select-all-then-type shares this path, including inline
 * rename.
 *
 * Characters are driven through onKeyEvent() rather than the private
 * insertCharacter(), so these tests exercise the real production path:
 * AestraWindowManager's char callback dispatches to the focused component's
 * onKeyEvent, which routes printable characters to insertCharacter().
 */

#include "Base/NUITextInput.h"
#include <iostream>
#include <string>

using namespace AestraUI;

static int testsPassed = 0;
static int testsFailed = 0;

#define PASS(msg) do { std::cout << "  PASS: " << msg << "\n"; ++testsPassed; } while(0)
#define FAIL(msg) do { std::cout << "  FAIL: " << msg << "\n"; ++testsFailed; } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while(0)

namespace {

/// A focused, visible input — onKeyEvent() drops everything otherwise.
std::unique_ptr<NUITextInput> makeFocusedInput(const std::string& text)
{
    auto input = std::make_unique<NUITextInput>(text);
    input->setVisible(true);
    input->setFocused(true);
    return input;
}

/// Deliver one printable character the way the platform does.
void typeChar(NUITextInput& input, char c)
{
    NUIKeyEvent event;
    event.keyCode = NUIKeyCode::Unknown;  // not a navigation/edit key
    event.character = c;
    event.pressed = true;
    input.onKeyEvent(event);
}

void typeString(NUITextInput& input, const std::string& s)
{
    for (char c : s) {
        typeChar(input, c);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// The regression: keyboard insertion must replace a selection
// ---------------------------------------------------------------------------
static void test_typing_replaces_selection() {
    auto input = makeFocusedInput("0.0");
    input->selectAll();

    typeChar(*input, '5');

    ASSERT(input->getText() == "5",
           "typing over a full selection must replace it, not insert at the caret "
           "(got '" + input->getText() + "')");
    PASS("typing over a selection replaces it");
}

static void test_paste_replaces_selection() {
    auto input = makeFocusedInput("0.0");
    input->selectAll();

    input->insertText("5");

    ASSERT(input->getText() == "5",
           "pasting over a full selection must replace it (got '" + input->getText() + "')");
    PASS("pasting over a selection replaces it");
}

/// The two entry points must not disagree — that divergence *was* the bug.
static void test_keyboard_and_paste_parity() {
    auto typed = makeFocusedInput("0.0");
    typed->selectAll();
    typeChar(*typed, '5');

    auto pasted = makeFocusedInput("0.0");
    pasted->selectAll();
    pasted->insertText("5");

    ASSERT(typed->getText() == pasted->getText(),
           "keyboard and paste must produce identical text over a selection "
           "(keyboard '" + typed->getText() + "' vs paste '" + pasted->getText() + "')");
    ASSERT(typed->getCaretPosition() == pasted->getCaretPosition(),
           "keyboard and paste must leave the caret in the same place");
    PASS("keyboard/paste parity over a selection");
}

static void test_selection_cleared_and_caret_after_replacement() {
    auto input = makeFocusedInput("0.0");
    input->selectAll();

    typeChar(*input, '5');

    ASSERT(input->getSelectedText().empty(),
           "the selection must be consumed by the replacement");
    ASSERT(input->getCaretPosition() == 1,
           "caret must land after the replacement text, not at 0");
    PASS("selection cleared and caret lands after replacement");
}

// ---------------------------------------------------------------------------
// The unselected path must keep working — the fix must not turn every
// keystroke into a replacement.
// ---------------------------------------------------------------------------
static void test_no_selection_inserts_at_caret() {
    auto input = makeFocusedInput("abc");
    input->setCaretPosition(1);

    typeChar(*input, 'X');

    ASSERT(input->getText() == "aXbc",
           "with no selection a character must insert at the caret "
           "(got '" + input->getText() + "')");
    ASSERT(input->getCaretPosition() == 2, "caret advances past the inserted character");
    PASS("no selection inserts at the caret");
}

static void test_no_selection_append_at_end() {
    auto input = makeFocusedInput("ab");
    input->setCaretPosition(2);

    typeChar(*input, 'c');

    ASSERT(input->getText() == "abc",
           "typing at the end appends (got '" + input->getText() + "')");
    PASS("no selection appends at end of text");
}

// ---------------------------------------------------------------------------
// The case that exposed it: a numeric readout edited in place.
// ---------------------------------------------------------------------------
static void test_negative_value_entry_over_selection() {
    auto input = makeFocusedInput("0.0");
    input->selectAll();

    typeString(*input, "-4.5");

    ASSERT(input->getText() == "-4.5",
           "select-all then typing a negative value must yield exactly that value "
           "(got '" + input->getText() + "')");
    ASSERT(input->getSelectedText().empty(), "selection consumed by the first keystroke");
    ASSERT(input->getCaretPosition() == 4, "caret at end of the typed value");
    PASS("negative value typed over a selection yields exactly that value");
}

/// The precise silent-no-op that was shipping: "6.0" + typing "0" -> "06.0",
/// which parses back to 6.0, so the edit looked like it did nothing at all.
static void test_prepend_regression_is_gone() {
    auto input = makeFocusedInput("6.0");
    input->selectAll();

    typeChar(*input, '0');

    ASSERT(input->getText() != "06.0",
           "regression: the typed character was prepended to the selected text");
    ASSERT(input->getText() == "0",
           "typing '0' over a selected '6.0' must leave exactly '0' "
           "(got '" + input->getText() + "')");
    PASS("no prepend-instead-of-replace regression");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
    std::cout << "========================================\n";
    std::cout << "  NUITextInput Editing Regression Tests\n";
    std::cout << "========================================\n\n";

    test_typing_replaces_selection();
    test_paste_replaces_selection();
    test_keyboard_and_paste_parity();
    test_selection_cleared_and_caret_after_replacement();
    test_no_selection_inserts_at_caret();
    test_no_selection_append_at_end();
    test_negative_value_entry_over_selection();
    test_prepend_regression_is_gone();

    std::cout << "\n========================================\n";
    if (testsFailed == 0) {
        std::cout << "  All " << testsPassed << " tests passed.\n";
    } else {
        std::cout << "  " << testsPassed << " passed, " << testsFailed << " failed.\n";
    }
    std::cout << "========================================\n";
    return testsFailed > 0 ? 1 : 0;
}
