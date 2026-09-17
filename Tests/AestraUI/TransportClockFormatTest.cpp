// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Spec 1 items 2+5: the transport clock toggles between time and musical
// display with one tap, and the BPM value opens for direct editing on
// double-click. Pins both clock representations (time preserved verbatim,
// musical in the existing bar:beat.centibeats convention), the toggle
// round-trip, and the BPM inline-edit commit/cancel/clamp contract.

#include "../../Source/Components/TransportInfoContainer.h"
#include "../../AestraUI/Base/NUITextInput.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        ++g_failures;
    }
}

AestraUI::NUIMouseEvent mousePress(float x, float y) {
    AestraUI::NUIMouseEvent e;
    e.position = AestraUI::NUIPoint(x, y);
    e.button = AestraUI::NUIMouseButton::Left;
    e.pressed = true;
    return e;
}

AestraUI::NUIMouseEvent mouseRelease(float x, float y) {
    AestraUI::NUIMouseEvent e;
    e.position = AestraUI::NUIPoint(x, y);
    e.button = AestraUI::NUIMouseButton::Left;
    e.released = true;
    return e;
}

AestraUI::NUIKeyEvent keyPress(AestraUI::NUIKeyCode code) {
    AestraUI::NUIKeyEvent e;
    e.keyCode = code;
    e.pressed = true;
    return e;
}

AestraUI::NUITextInput* findEditor(Aestra::BPMDisplay& bpm) {
    for (const auto& child : bpm.getChildren()) {
        if (auto* input = dynamic_cast<AestraUI::NUITextInput*>(child.get())) {
            return input;
        }
    }
    return nullptr;
}

void doubleClickValue(Aestra::BPMDisplay& bpm, float x, float y) {
    bpm.onMouseEvent(mousePress(x, y));
    bpm.onMouseEvent(mouseRelease(x, y));
    bpm.onMouseEvent(mousePress(x, y));
    bpm.onMouseEvent(mouseRelease(x, y));
}

} // namespace

int main() {
    using Aestra::BPMDisplay;
    using Aestra::TimerDisplay;

    expect(TimerDisplay::formatTime(0.0) == "0:00.00", "time zero formats");
    expect(TimerDisplay::formatTime(4.33) == "0:04.33", "time seconds format");
    expect(TimerDisplay::formatTime(65.5) == "1:05.50", "time minute rollover");
    expect(TimerDisplay::formatTime(600.0) == "10:00.00", "time double digits");

    expect(TimerDisplay::formatMusical(0.0, 4) == "1:1.00", "musical downbeat");
    expect(TimerDisplay::formatMusical(3.5, 4) == "1:4.50", "musical fractional beat");
    expect(TimerDisplay::formatMusical(4.0, 4) == "2:1.00", "musical bar rollover");
    expect(TimerDisplay::formatMusical(7.25, 3) == "3:2.25", "musical triple meter");
    expect(TimerDisplay::formatMusical(-2.0, 4) == "1:1.00", "musical clamps negative");
    expect(TimerDisplay::formatMusical(5.0, 0) == "6:1.00", "musical guards zero meter");
    expect(TimerDisplay::formatMusical(3.995, 4) == "2:1.00", "centibeat rounding carries into the next bar");
    expect(TimerDisplay::formatMusical(7.999, 4) == "3:1.00", "carry works past the first bar boundary");
    expect(TimerDisplay::formatMusical(3.994, 4) == "1:4.99", "no carry below the rounding boundary");

    TimerDisplay clock;
    expect(clock.getDisplayMode() == TimerDisplay::DisplayMode::Time, "clock defaults to time mode");
    clock.setTime(4.33);
    clock.setMusicalPosition(8.66, 4);
    clock.toggleDisplayMode();
    expect(clock.getDisplayMode() == TimerDisplay::DisplayMode::Musical, "one toggle reaches musical mode");
    clock.toggleDisplayMode();
    expect(clock.getDisplayMode() == TimerDisplay::DisplayMode::Time, "two toggles return to time mode");
    expect(clock.getTime() == 4.33, "toggling never moves the position");

    BPMDisplay bpm;
    bpm.setBounds(AestraUI::NUIRect(0.0f, 0.0f, 90.0f, 28.0f));
    bpm.setBPM(120.0f);
    std::vector<float> committed;
    bpm.setOnBPMChange([&committed](float v) { committed.push_back(v); });

    // A single click opens nothing.
    bpm.onMouseEvent(mousePress(10.0f, 18.0f));
    bpm.onMouseEvent(mouseRelease(10.0f, 18.0f));
    expect(findEditor(bpm) == nullptr, "single click does not open the editor");

    // Double-click opens the inline editor prefilled with the current value.
    doubleClickValue(bpm, 10.0f, 18.0f);
    AestraUI::NUITextInput* editor = findEditor(bpm);
    expect(editor != nullptr, "double-click opens the BPM editor");
    if (editor != nullptr) {
        expect(editor->getText() == "120", "editor is prefilled with the trimmed value");
        editor->setText("140");
        editor->onKeyEvent(keyPress(AestraUI::NUIKeyCode::Enter));
        expect(bpm.getBPM() == 140.0f, "Return commits the typed value");
        expect(committed.size() == 1 && committed.back() == 140.0f, "commit fires the change callback");
        expect(findEditor(bpm) == nullptr, "editor closes after commit");
    }

    // Garbage is rejected: value and silence preserved.
    committed.clear();
    doubleClickValue(bpm, 10.0f, 18.0f);
    editor = findEditor(bpm);
    expect(editor != nullptr, "editor reopens");
    if (editor != nullptr) {
        editor->setText("abc");
        editor->onKeyEvent(keyPress(AestraUI::NUIKeyCode::Enter));
        expect(bpm.getBPM() == 140.0f, "unparseable input keeps the value");
        expect(committed.empty(), "rejected input fires no callback");
    }

    // Out-of-range input is constrained to the existing 20..999 limits.
    editor = findEditor(bpm);
    if (editor == nullptr) {
        doubleClickValue(bpm, 10.0f, 18.0f);
        editor = findEditor(bpm);
    }
    if (editor != nullptr) {
        editor->setText("5000");
        editor->onKeyEvent(keyPress(AestraUI::NUIKeyCode::Enter));
        expect(bpm.getBPM() == 999.0f, "overshoot clamps to 999");
        doubleClickValue(bpm, 10.0f, 18.0f);
        editor = findEditor(bpm);
        if (editor != nullptr) {
            editor->setText("5");
            editor->onKeyEvent(keyPress(AestraUI::NUIKeyCode::Enter));
            expect(bpm.getBPM() == 20.0f, "undershoot clamps to 20");
        }
    }

    // Escape abandons the edit.
    doubleClickValue(bpm, 10.0f, 18.0f);
    editor = findEditor(bpm);
    if (editor != nullptr) {
        editor->setText("200");
        editor->onKeyEvent(keyPress(AestraUI::NUIKeyCode::Escape));
        expect(bpm.getBPM() == 20.0f, "Escape keeps the value");
        expect(findEditor(bpm) == nullptr, "editor closes after Escape");
    }

    // Clicking away applies what's typed (mixer-fader convention).
    doubleClickValue(bpm, 10.0f, 18.0f);
    editor = findEditor(bpm);
    if (editor != nullptr) {
        editor->setText("128");
        editor->onFocusLost();
        expect(bpm.getBPM() == 128.0f, "focus loss commits the typed value");
        expect(findEditor(bpm) == nullptr, "editor closes after focus loss");
    }

    // Existing controls survive the feature.
    bpm.onMouseEvent(mousePress(80.0f, 6.0f));
    bpm.onMouseEvent(mouseRelease(80.0f, 6.0f));
    expect(bpm.getBPM() == 129.0f, "up arrow still increments");

    if (g_failures == 0) {
        std::cout << "All TransportClockFormat tests passed.\n";
        return 0;
    }
    std::cerr << g_failures << " TransportClockFormat test(s) failed.\n";
    return 1;
}
