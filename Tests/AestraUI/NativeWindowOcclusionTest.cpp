// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// NativeWindowOcclusionTest — a plugin editor's own window is hidden while another panel sits on top of it.
//
// A plugin's editor is a real X window, so it always draws over anything Aestra paints. Raise the mixer or another panel over an
// editor and Aestra's frame goes behind it while the plugin's window stays in front: its controls, and the close button, are
// unreachable. The owner chose: hide a covered plugin window until it is raised. coveredByPanelAbove() answers "is an opaque
// panel above this one and over this rectangle", by the real z-order: later siblings are above, layers are transparent.

#include "Widgets/AestraPanelWindow.h"
#include "Widgets/EditorSurfacePlacement.h"
#include "Widgets/NativeWindowOcclusion.h"

#include <cmath>
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

// A plugin editor panel as the real ones are: an AestraPanelWindow.
class Editor : public AestraPanelWindow {
public:
    Editor(NUIRect r) { setBounds(r); }
    void drawContent(NUIRenderer&, const NUIRect&) override {}
    // As every real editor does (#481): a click over its empty body is its own, so it never falls through to the one behind.
    bool onMouseEvent(const NUIMouseEvent& e) override {
        if (AestraPanelWindow::onMouseEvent(e)) return true;
        return consumeInsideBounds(e);
    }
};

NUIMouseEvent press(float x, float y) {
    NUIMouseEvent e;
    e.type = NUIMouseEventType::Down;
    e.position = {x, y};
    e.button = NUIMouseButton::Left;
    e.pressed = true;
    return e;
}

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

    // --- the editors' layer stays above the mixer, whatever is raised or added after it ------------------------------------
    {
        auto top = std::make_shared<Layer>(kWindow);
        auto editorsLayer = std::make_shared<Layer>(kWindow);
        editorsLayer->setAlwaysOnTop(true);
        auto mixerPanel = std::make_shared<Panel>(NUIRect{50, 50, 900, 600});
        top->addChild(editorsLayer);
        top->addChild(mixerPanel); // added after
        check(top->getChildren().back() == editorsLayer, "a layer marked always-on-top stays last when a sibling is added after it");
        mixerPanel->bringToFront();
        check(top->getChildren().back() == editorsLayer, "and when the mixer is raised");
        auto another = std::make_shared<Panel>(NUIRect{10, 10, 100, 100});
        top->addChild(another);
        another->bringToFront();
        check(top->getChildren().back() == editorsLayer, "and after several raises and adds");
        auto plain = std::make_shared<Layer>(kWindow);
        top->addChild(plain);
        plain->bringToFront();
        check(top->getChildren().back() != plain, "a layer that is not marked is raised as before");
        editorsLayer->setAlwaysOnTop(false);
        mixerPanel->bringToFront();
        check(top->getChildren().back() == mixerPanel, "and without the mark nothing is held on top");
    }

    // --- a press on an editor raises it (any editor panel), reaching the one behind where it sticks out ----------------------
    {
        auto top = std::make_shared<Layer>(kWindow);
        auto layer = std::make_shared<Layer>(kWindow);
        top->addChild(layer);
        auto a = std::make_shared<Editor>(NUIRect{100, 100, 300, 300});
        auto b = std::make_shared<Editor>(NUIRect{250, 250, 300, 300}); // opened later, on top of a
        layer->addChild(a);
        layer->addChild(b);
        check(layer->getChildren().back() == b, "premise: the newer editor is on top");
        NUIComponent::dispatchMouseEvent(top.get(), press(300, 300)); // inside both: the top one takes it
        check(layer->getChildren().back() == b, "a press in the overlap goes to the editor on top, which stays on top");
        NUIComponent::dispatchMouseEvent(top.get(), press(120, 120)); // only the one behind
        check(layer->getChildren().back() == a, "a press where the editor behind sticks out raises it");
    }

    // --- new editors cascade, so the older one's title bar stays visible ------------------------------------------------
    {
        const Layout::NUIAnchoredRect centred{0.5, 0.5, 400.0, 300.0};
        const auto none = EditorPlacement::cascadedOpenPreference(centred, 0, 800.0, 500.0);
        check(none.anchorX == 0.5 && none.anchorY == 0.5, "the first editor opens centred, as before");
        const auto second = EditorPlacement::cascadedOpenPreference(centred, 1, 800.0, 500.0);
        check(std::abs((second.anchorX - 0.5) * 800.0 - EditorPlacement::kCascadeStepPx) < 0.001, "the second opens one step right of the centre");
        check(std::abs((second.anchorY - 0.5) * 500.0 - EditorPlacement::kCascadeStepPx) < 0.001, "and one step down");
        const auto third = EditorPlacement::cascadedOpenPreference(centred, 2, 800.0, 500.0);
        check(third.anchorX > second.anchorX && third.anchorY > second.anchorY, "each further editor steps on");
        const auto wrapped = EditorPlacement::cascadedOpenPreference(centred, 1 + EditorPlacement::kCascadeSteps, 800.0, 500.0);
        check(wrapped.anchorX == second.anchorX && wrapped.anchorY == second.anchorY, "after a few steps it starts over instead of walking off the screen");
        const auto tight = EditorPlacement::cascadedOpenPreference(centred, 3, 0.0, 0.0);
        check(tight.anchorX == 0.5 && tight.anchorY == 0.5, "with no room to move, it stays where it was");
        check(tight.width == 400.0 && tight.height == 300.0, "and the size is never touched");
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "native window occlusion passed\n";
    return 0;
}
