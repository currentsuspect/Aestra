// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Spec 1 item 4b: the mixer insert dropdown answers the keyboard without
// leaving its search field — arrows move selection (skipping category
// headers), Enter loads the highlighted row (same as click).

#include "../../AestraUI/Widgets/UIMixerPluginDropdown.h"
#include "../../AestraUI/Base/NUITextInput.h"
#include "../../AestraUI/Helpers/MixerPluginListPolicy.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace AestraUI;

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        ++g_failures;
    }
}

Aestra::Components::MixerPluginEntry entry(const char* id, const char* name, const char* category) {
    Aestra::Components::MixerPluginEntry e;
    e.id = id;
    e.name = name;
    e.category = category;
    e.typeName = "Effect";
    return e;
}

NUIKeyEvent keyDown(NUIKeyCode code) {
    NUIKeyEvent e;
    e.keyCode = code;
    e.pressed = true;
    return e;
}

NUITextInput* findSearchInput(UIMixerPluginDropdown& menu) {
    for (const auto& child : menu.getChildren()) {
        if (auto* input = dynamic_cast<NUITextInput*>(child.get())) {
            return input;
        }
    }
    return nullptr;
}

void showMenu(UIMixerPluginDropdown& menu) {
    menu.setPluginEntries({entry("comp", "Comp", "Compressor"), entry("delay", "Delay", "Delay"),
                           entry("eq", "EQ", "Equalizer"), entry("verb", "Verb", "Reverb")});
    menu.showAt(NUIRect(0.0f, 0.0f, 100.0f, 30.0f), 800.0f, 0.0f);
}

} // namespace

int main() {
    // Flat order after grouping: [DYNAMICS Comp] [TIME Delay] [SPECTRAL EQ Verb].
    {
        UIMixerPluginDropdown menu;
        std::vector<std::pair<std::string, std::string>> loaded;
        menu.onPluginSelected = [&loaded](const std::string& id, const std::string& name) {
            loaded.emplace_back(id, name);
        };
        showMenu(menu);
        NUITextInput* search = findSearchInput(menu);
        expect(search != nullptr, "dropdown exposes its search field");

        search->onKeyEvent(keyDown(NUIKeyCode::Down));
        search->onKeyEvent(keyDown(NUIKeyCode::Down));
        search->onKeyEvent(keyDown(NUIKeyCode::Enter));
        expect(loaded.size() == 1 && loaded.back().first == "delay",
               "Down,Down,Enter loads Delay (category headers skipped)");
    }

    {
        UIMixerPluginDropdown menu;
        std::vector<std::string> loaded;
        menu.onPluginSelected = [&loaded](const std::string& id, const std::string&) { loaded.push_back(id); };
        showMenu(menu);
        NUITextInput* search = findSearchInput(menu);
        search->onKeyEvent(keyDown(NUIKeyCode::Up));
        search->onKeyEvent(keyDown(NUIKeyCode::Enter));
        expect(loaded.size() == 1 && loaded.back() == "verb", "Up from empty starts at the last item");
    }

    {
        UIMixerPluginDropdown menu;
        std::vector<std::string> loaded;
        menu.onPluginSelected = [&loaded](const std::string& id, const std::string&) { loaded.push_back(id); };
        showMenu(menu);
        NUITextInput* search = findSearchInput(menu);
        search->onKeyEvent(keyDown(NUIKeyCode::Enter));
        expect(loaded.empty(), "Enter with no selection loads nothing");

        for (int i = 0; i < 10; ++i) {
            search->onKeyEvent(keyDown(NUIKeyCode::Down));
        }
        search->onKeyEvent(keyDown(NUIKeyCode::Enter));
        expect(loaded.size() == 1 && loaded.back() == "verb", "Down clamps at the last item");
    }

    {
        // Host-faithful teardown: the host's onDismissed removes the menu and
        // drops its last reference while Enter is still on the stack. Before
        // the fix, activateSelected read item.id/item.name after dismiss()
        // destroyed them (live SIGSEGV hashing the id in
        // PluginManager::createInstanceById).
        auto root = std::make_shared<NUIComponent>();
        auto menu = std::make_shared<UIMixerPluginDropdown>();
        std::vector<std::pair<std::string, std::string>> loaded;
        menu->onPluginSelected = [&loaded](const std::string& id, const std::string& name) {
            loaded.emplace_back(id, name);
        };
        menu->onDismissed = [&root, &menu]() {
            if (auto* parent = menu->getParent()) {
                parent->removeChild(menu);
            }
            menu.reset();
        };
        menu->setPluginEntries({entry("comp", "Comp", "Compressor"), entry("delay", "Delay", "Delay"),
                                entry("eq", "EQ", "Equalizer"), entry("verb", "Verb", "Reverb")});
        root->addChild(menu);
        menu->showAt(NUIRect(0.0f, 0.0f, 100.0f, 30.0f), 800.0f, 0.0f);
        NUITextInput* search = findSearchInput(*menu);
        expect(search != nullptr, "host-owned dropdown exposes its search field");

        search->onKeyEvent(keyDown(NUIKeyCode::Down));
        search->onKeyEvent(keyDown(NUIKeyCode::Down));
        search->onKeyEvent(keyDown(NUIKeyCode::Enter));
        expect(loaded.size() == 1 && loaded.back().first == "delay" && loaded.back().second == "Delay",
               "Enter delivers the highlighted row even though dismiss destroyed the menu first");
        expect(menu == nullptr, "host teardown released the menu");
        expect(root->getChildren().empty(), "host teardown detached the menu");
    }

    {
        // Clearing the search restores a different row set: the selection
        // resets so Enter cannot activate a stale index in the restored rows.
        UIMixerPluginDropdown menu;
        std::vector<std::string> loaded;
        menu.onPluginSelected = [&loaded](const std::string& id, const std::string&) { loaded.push_back(id); };
        showMenu(menu);
        NUITextInput* search = findSearchInput(menu);
        expect(search != nullptr, "search field present for the clear-selection case");

        search->setText("e");
        search->onKeyEvent(keyDown(NUIKeyCode::Down));
        search->onKeyEvent(keyDown(NUIKeyCode::Enter));
        expect(loaded.size() == 1, "filtered Down+Enter loads the highlighted row");
        search->setText("");
        search->onKeyEvent(keyDown(NUIKeyCode::Enter));
        expect(loaded.size() == 1, "Enter after clearing search loads nothing (selection reset)");
    }

    if (g_failures == 0) {
        std::cout << "All MixerDropdownKeyboard tests passed.\n";
        return 0;
    }
    std::cerr << g_failures << " MixerDropdownKeyboard test(s) failed.\n";
    return 1;
}
