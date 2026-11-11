// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// PluginPresetBarTest — V83-S2 (FD-72, board A to C): the preset bar in a plugin window, driven by pointer and keys.
//   1. ‹ › step and are one undo each; the name shows "N changes" after a knob moves;
//   2. clicking the name browses: arrows are heard live, Esc puts back, typing filters, Enter keeps (one undo);
//      a hovered row is heard, a click keeps it, a click outside puts back;
//   3. Save opens with "Lead Vox Plate" filled in; Enter saves it to Yours; typing replaces the suggestion;
//   4. A|B compares; keys reach the bar through its panel only while a popover is open, and every key is
//      swallowed then (Space must not start the transport under a browser).

#include "AestraPanelWindow.h"
#include "PluginPresetBar.h"

#include "Plugin/BuiltInPlugins.h"
#include "Plugin/InternalPluginRegistry.h"
#include "Presets/PresetLibrary.h"
#include "Presets/PresetSession.h"

#include "../Support/NullRenderer.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace AestraUI;
using namespace Aestra::Audio;
using namespace Aestra::Audio::Presets;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}

bool near(float a, float b) { return std::fabs(a - b) < 1.0e-4f; }

NUIPoint centre(const NUIRect& r) { return {r.x + r.width * 0.5f, r.y + r.height * 0.5f}; }

NUIMouseEvent press(NUIPoint at) {
    NUIMouseEvent e;
    e.position = at;
    e.button = NUIMouseButton::Left;
    e.pressed = true;
    return e;
}
NUIMouseEvent release(NUIPoint at) {
    NUIMouseEvent e;
    e.position = at;
    e.button = NUIMouseButton::Left;
    e.released = true;
    return e;
}
NUIMouseEvent move(NUIPoint at) {
    NUIMouseEvent e;
    e.position = at;
    return e;
}
NUIKeyEvent key(NUIKeyCode code, char ch = 0) {
    NUIKeyEvent e;
    e.keyCode = code;
    e.character = ch;
    e.pressed = true;
    return e;
}

struct Rig {
    Aestra::Testing::NullRenderer renderer;
    PluginInstancePtr verb;
    uint32_t decay = 0;
    fs::path root;
    std::unique_ptr<PresetLibrary> library;
    std::vector<std::shared_ptr<ICommand>> history;
    std::shared_ptr<PluginPresetBar> bar;
    NUIRect title{100, 100, 720, 32};
    NUIRect window{100, 100, 720, 420};

    Rig() {
        BuiltInPlugins::registerCoreBuiltIns();
        verb = InternalPluginRegistry::instance().createInstance(BuiltInPlugins::verbInfo().id);
        verb->initialize(48000.0, 512);
        for (const auto& p : verb->getParameters())
            if (p.name == "Decay") decay = p.id;
        root = fs::temp_directory_path() /
               ("aestra-presetbar-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        library = std::make_unique<PresetLibrary>(root);
        auto make = [](const char* name, float d, std::vector<std::string> tags = {}) {
            Preset p;
            p.name = name;
            p.tags = std::move(tags);
            p.params = {{PresetParam::kNoId, "Decay", d}};
            return p;
        };
        registerFactoryPresets(verb->getInfo().id,
                               {make("Big Hall", 0.95f), make("Vocal Plate", 0.40f, {"vocal"}), make("Vocal Room", 0.20f)});
        auto session = std::make_unique<PresetSession>(verb, *library, "Lead Vox",
                                                       [this](std::shared_ptr<ICommand> c) { history.push_back(c); });
        bar = std::make_shared<PluginPresetBar>(std::move(session));
        frame();
    }
    ~Rig() {
        registerFactoryPresets(verb->getInfo().id, {});
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void frame() {
        bar->render(renderer, title);
        bar->renderOverlay(renderer, window);
    }
    void click(const NUIRect& r) {
        bar->onMouseEvent(press(centre(r)));
        bar->onMouseEvent(release(centre(r)));
        frame();
    }
    void type(const std::string& s) {
        for (char c : s) bar->onKeyEvent(key(NUIKeyCode::Unknown, c));
        frame();
    }
    float d() const { return verb->getParameter(decay); }
};

void testStepping() {
    Rig rig;
    auto& s = rig.bar->session();
    check(s.presets().size() == 3 && s.presets()[0].name == "Vocal Plate", "vocal presets first on Lead Vox");
    rig.click(rig.bar->controlRect(2));
    check(s.currentName() == "Vocal Plate" && near(rig.d(), 0.40f), "› loads the first preset");
    rig.click(rig.bar->controlRect(2));
    check(s.currentName() == "Vocal Room", "› again steps");
    rig.click(rig.bar->controlRect(0));
    check(s.currentName() == "Vocal Plate", "‹ steps back");
    check(rig.history.size() == 3, "each step is one undo");
    rig.verb->setParameter(rig.decay, 0.5f);
    check(s.changes() == 1, "a moved knob shows as 1 change");
}

void testBrowse() {
    Rig rig;
    auto& s = rig.bar->session();
    rig.verb->setParameter(rig.decay, 0.61f);

    rig.click(rig.bar->controlRect(1));
    check(rig.bar->popover() == PluginPresetBar::Popover::Browse && s.browsing(), "clicking the name browses");
    rig.bar->onKeyEvent(key(NUIKeyCode::Down));
    check(near(rig.d(), 0.40f), "↓ hears the first row live");
    rig.bar->onKeyEvent(key(NUIKeyCode::Down));
    check(near(rig.d(), 0.20f), "↓ again hears the next");
    rig.bar->onKeyEvent(key(NUIKeyCode::Escape));
    rig.frame();
    check(rig.bar->popover() == PluginPresetBar::Popover::None && near(rig.d(), 0.61f), "Esc puts back the sound");
    check(rig.history.empty(), "Esc records nothing");

    rig.click(rig.bar->controlRect(1));
    rig.type("hall");
    check(rig.bar->visibleRows().size() == 1, "typing filters the list");
    rig.bar->onKeyEvent(key(NUIKeyCode::Down));
    rig.bar->onKeyEvent(key(NUIKeyCode::Enter));
    rig.frame();
    check(s.currentName() == "Big Hall" && near(rig.d(), 0.95f), "Enter keeps the one heard");
    check(rig.history.size() == 1, "a kept browse is one undo");
    rig.history.back()->undo();
    check(near(rig.d(), 0.61f) && !s.current(), "undo goes back to before the browse");
    rig.history.back()->redo();

    // Pointer: a hovered row is heard, a click keeps it.
    rig.click(rig.bar->controlRect(1));
    rig.bar->onMouseEvent(move(centre(rig.bar->rowRect(1))));
    check(near(rig.d(), 0.20f), "hovering a row hears it");
    rig.bar->onMouseEvent(press(centre(rig.bar->rowRect(1))));
    rig.frame();
    check(rig.bar->popover() == PluginPresetBar::Popover::None && s.currentName() == "Vocal Room", "a click keeps it");

    // A click outside puts back.
    rig.click(rig.bar->controlRect(1));
    rig.bar->onMouseEvent(move(centre(rig.bar->rowRect(2))));
    check(near(rig.d(), 0.95f), "hovering another hears it");
    rig.bar->onMouseEvent(press({rig.window.right() + 40.0f, rig.window.bottom() + 40.0f}));
    rig.frame();
    check(near(rig.d(), 0.20f) && s.currentName() == "Vocal Room", "a click outside puts back what was there");

    // Filter chips: Yours is empty until something is saved.
    rig.click(rig.bar->controlRect(1));
    rig.bar->setFilter(2);
    check(rig.bar->visibleRows().empty(), "Yours is empty before a save");
    check(rig.bar->chipCount() == 3, "no This project chip until the project can keep presets");
    rig.bar->onKeyEvent(key(NUIKeyCode::Escape));
}

void testSave() {
    Rig rig;
    auto& s = rig.bar->session();
    rig.click(rig.bar->controlRect(2)); // Vocal Plate
    rig.verb->setParameter(rig.decay, 0.44f);
    rig.click(rig.bar->controlRect(3));
    check(rig.bar->popover() == PluginPresetBar::Popover::Save, "Save opens its form");
    check(rig.bar->saveName() == "Lead Vox Plate", "the name is filled in: " + rig.bar->saveName());
    rig.bar->onKeyEvent(key(NUIKeyCode::Enter));
    rig.frame();
    check(rig.bar->popover() == PluginPresetBar::Popover::None, "Enter saves and closes");
    check(s.currentName() == "Lead Vox Plate" && s.current()->source == PresetSource::User, "the bar shows yours");
    check(fs::exists(rig.root / "Aestra Verb" / "Lead Vox Plate.aepreset") || !s.current()->path.empty(),
          "the file is written");

    rig.click(rig.bar->controlRect(3));
    check(rig.bar->popoverRect().height < 160.0f, "without project presets, Save has no place switch");
    rig.bar->onKeyEvent(key(NUIKeyCode::Tab));
    rig.type("Mine");
    check(rig.bar->saveName() == "Mine", "typing replaces the suggestion");
    rig.bar->onKeyEvent(key(NUIKeyCode::Backspace));
    check(rig.bar->saveName() == "Min", "backspace edits");
    rig.bar->onKeyEvent(key(NUIKeyCode::Escape));
    check(rig.bar->popover() == PluginPresetBar::Popover::None && s.currentName() == "Lead Vox Plate",
          "Esc closes Save without saving");
}

void testCompareAndKeys() {
    Rig rig;
    auto& s = rig.bar->session();
    rig.verb->setParameter(rig.decay, 0.30f);
    rig.click(rig.bar->controlRect(5));
    check(s.comparing() && s.activeSlot() == 1, "pressing B starts comparing on B");
    rig.verb->setParameter(rig.decay, 0.80f);
    rig.click(rig.bar->controlRect(4));
    check(s.activeSlot() == 0 && near(rig.d(), 0.30f), "A is as it was");
    rig.click(rig.bar->controlRect(5));
    check(near(rig.d(), 0.80f), "B kept its edit");

    // Through the panel: keys only while a popover is open, and then every key.
    AestraPanelWindow panel;
    panel.setBounds(rig.window);
    panel.setTitleAccessory(rig.bar);
    check(!panel.routeKeyToAccessory(key(NUIKeyCode::Space, ' ')), "with nothing open, Space is the transport's");
    rig.click(rig.bar->controlRect(1));
    check(panel.routeKeyToAccessory(key(NUIKeyCode::Space, ' ')), "with the browser open, Space is swallowed");
    check(rig.bar->searchText() == " ", "and typed into the search");
    check(panel.routeKeyToAccessory(key(NUIKeyCode::Escape)), "Esc closes it");
    check(!panel.routeKeyToAccessory(key(NUIKeyCode::Space, ' ')), "and Space is the transport's again");

    // Typed characters come to the focused component: a click that opens a popover focuses the panel.
    panel.onMouseEvent(press(centre(rig.bar->controlRect(3))));
    rig.frame();
    check(panel.isFocused(), "opening Save focuses the panel");
    NUIKeyEvent ch;
    ch.character = 'Z';
    ch.pressed = true;
    check(panel.onKeyEvent(ch) && rig.bar->saveName() == "Z", "a typed character reaches the Save name");
}

} // namespace

int main() {
    testStepping();
    testBrowse();
    testSave();
    testCompareAndKeys();
    std::cout << "PluginPresetBarTest: " << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
