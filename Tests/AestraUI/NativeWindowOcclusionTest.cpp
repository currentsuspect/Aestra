// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// NativeWindowOcclusionTest — a plugin editor's own window is hidden while another panel sits on top of it.
//
// A plugin's editor is a real X window, so it always draws over anything Aestra paints. Raise the mixer or another panel over an
// editor and Aestra's frame goes behind it while the plugin's window stays in front: its controls, and the close button, are
// unreachable. The owner chose: hide a covered plugin window until it is raised. coveredByPanelAbove() answers "is an opaque
// panel above this one and over this rectangle", by the real z-order: later siblings are above, layers are transparent.

#include "Widgets/NativeWindowOcclusion.h"

#include <iostream>
#include <memory>
#include <string>

using namespace AestraUI;

namespace {
int g_failures = 0;

void check(bool condition, const std::string& label) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << label << std::endl;
    if (!condition) {
        ++g_failures;
    }
}

// An opaque panel, as the mixer, the piano roll and the plugin windows are.
class Panel : public NUIComponent {
public:
    Panel(NUIRect r) { setBounds(r); }
    bool occludesNativeWindows() const override { return true; }
};
// A transparent layer or a control: not something a native window must hide behind.
class Layer : public NUIComponent {
public:
    Layer(NUIRect r) { setBounds(r); }
};

const NUIRect kWindow{0, 0, 1200, 800};
const NUIRect kView{100, 100, 400, 300}; // the plugin's own view
} // namespace

int main() {
    auto root = std::make_shared<Layer>(kWindow);
    auto popups = std::make_shared<Layer>(kWindow); // the full-window layer editors live in
    root->addChild(popups);
    auto editor = std::make_shared<Panel>(NUIRect{90, 70, 420, 340});
    popups->addChild(editor);

    check(!coveredByPanelAbove(*editor, kView), "alone, an editor is not covered");

    auto other = std::make_shared<Panel>(NUIRect{300, 200, 400, 300}); // overlaps the view
    popups->addChild(other);
    check(coveredByPanelAbove(*editor, kView), "a panel added after it, overlapping its view, covers it");
    check(!coveredByPanelAbove(*other, NUIRect{300, 200, 400, 300}), "and the panel on top is not covered by the one below");

    popups->removeChild(other);
    other = std::make_shared<Panel>(NUIRect{700, 500, 200, 200}); // elsewhere
    popups->addChild(other);
    check(!coveredByPanelAbove(*editor, kView), "a panel above it that does not overlap its view does not cover it");

    other->setBounds(NUIRect{300, 200, 400, 300});
    other->setVisible(false);
    check(!coveredByPanelAbove(*editor, kView), "a hidden panel covers nothing");
    other->setVisible(true);

    // Raising the editor puts it above the other one: now the other is the covered one.
    editor->bringToFront();
    check(!coveredByPanelAbove(*editor, kView), "after the editor is raised it is no longer covered");
    check(coveredByPanelAbove(*other, NUIRect{300, 200, 400, 300}), "and the other panel now is");

    // The mixer: a panel in another layer, raised above the editors' layer.
    auto mixer = std::make_shared<Panel>(NUIRect{50, 50, 900, 600});
    root->addChild(mixer);
    check(coveredByPanelAbove(*editor, kView), "a panel in a layer above the editors' layer covers the editor");
    popups->bringToFront(); // a deliberate open raises the editors' layer
    check(!coveredByPanelAbove(*editor, kView), "raising the editors' layer above it uncovers the editor");

    // Layers are transparent: a full-window layer above does not cover anything by itself, but an opaque panel in it does.
    auto overlay = std::make_shared<Layer>(kWindow);
    root->addChild(overlay);
    check(!coveredByPanelAbove(*editor, kView), "an empty full-window layer above does not cover the editor");
    auto menuLike = std::make_shared<Panel>(NUIRect{120, 120, 100, 100});
    overlay->addChild(menuLike);
    check(coveredByPanelAbove(*editor, kView), "but an opaque panel inside that layer does");

    // Controls that are not panels (a button, a label) never cover a native window.
    overlay->removeChild(menuLike);
    auto control = std::make_shared<Layer>(NUIRect{120, 120, 100, 100});
    overlay->addChild(control);
    check(!coveredByPanelAbove(*editor, kView), "a control that is not a panel does not cover it");

    // The hide is delayed a moment, so a toast or a quick drag across the editor does not make it flicker.
    check(!shouldHideNativeWindow(0.0), "not covered yet: shown");
    check(!shouldHideNativeWindow(kHideAfterCoveredSeconds * 0.5), "covered for a moment: still shown");
    check(shouldHideNativeWindow(kHideAfterCoveredSeconds), "covered for the delay: hidden");
    check(awayCoveredSeconds(0.2, 0.05, false) == 0.0, "uncovered, the count starts again at once");
    check(awayCoveredSeconds(0.2, 0.05, true) > 0.24, "covered, it keeps counting");

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "native window occlusion passed\n";
    return 0;
}
