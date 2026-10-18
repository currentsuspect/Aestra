// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Owner report (2026-10-11): "knobs like pan and trim don't show live changes when dragged (should show the
// tooltip but instead it disappears)."
//
// A hover tooltip is deliberately suppressed while a button is held or the cursor is captured. The knob's own
// value readout during a drag is exempt, but only when it is requested with force=true, and UIMixerKnob asked
// for a plain tooltip: the readout was shown on hover, then suppressed the moment the drag began.
//
// Drives the real knob through NUIComponent::dispatchMouseEvent (the entry that tracks held buttons, as the
// runtime does) and reads the global tooltip state.

#include "NUIComponent.h"
#include "UIMixerKnob.h"

#include <iostream>
#include <memory>
#include <string>

using namespace AestraUI;

namespace {

int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok) ++g_failures;
}

NUIMouseEvent mouse(NUIMouseEventType type, float x, float y, bool pressed, bool released, NUIMouseButton button) {
    NUIMouseEvent e;
    e.type = type;
    e.position = {x, y};
    e.button = button;
    e.pressed = pressed;
    e.released = released;
    return e;
}

} // namespace

int main() {
    for (UIMixerKnobType type : {UIMixerKnobType::Trim, UIMixerKnobType::Pan}) {
        const std::string name = type == UIMixerKnobType::Trim ? "trim" : "pan";
        NUIComponent::hideRemoteTooltip();
        auto knob = std::make_shared<UIMixerKnob>(type);
        knob->setBounds({100, 100, 40, 52});

        NUIComponent::dispatchMouseEvent(knob.get(), mouse(NUIMouseEventType::Down, 120, 120, true, false, NUIMouseButton::Left));
        check(knob->isDragging(), name + " knob: a press starts the drag");
        NUIComponent::updateGlobalTooltip(0.016);
        const auto& atPress = NUIComponent::getGlobalTooltipState();
        check(atPress.active && atPress.owner == knob.get() && !atPress.text.empty() && atPress.forced,
              name + " knob: the value readout is shown, as a forced readout, while the button is held");

        const std::string before = atPress.text;
        NUIComponent::dispatchMouseEvent(knob.get(), mouse(NUIMouseEventType::Move, 120, 90, false, false, NUIMouseButton::None));
        NUIComponent::updateGlobalTooltip(0.016);
        const auto& dragged = NUIComponent::getGlobalTooltipState();
        check(dragged.active && dragged.owner == knob.get(), name + " knob: it is still showing after the pointer moved");
        check(dragged.text != before, name + " knob: the readout follows the value as it changes (\"" + before + "\" -> \"" + dragged.text + "\")");

        NUIComponent::dispatchMouseEvent(knob.get(), mouse(NUIMouseEventType::Up, 120, 90, false, true, NUIMouseButton::Left));
        check(!knob->isDragging(), name + " knob: release ends the drag");
    }
    std::cout << (g_failures == 0 ? "[PASS] UIMixerKnobDragReadoutTest\n" : "[FAIL] UIMixerKnobDragReadoutTest\n");
    return g_failures == 0 ? 0 : 1;
}
