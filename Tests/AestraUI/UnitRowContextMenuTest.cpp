// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Owner report (2026-10-11): "when I have loaded a one-shot into the Arsenal unit, right-clicking it
// should have the option Open in Piano Roll." The item was offered only for Instrument units, and
// the row menu opened only from the name label. A loaded one-shot is a Sampler unit.
//
// Pins: Sampler and 808 units offer "Open in Piano Roll" and it calls the open-pattern callback with
// the unit's pattern; an Audio unit does not; a right-press on the step grid is still the erase gesture
// and opens no menu; the menu opens from the row outside the grid, not only from the name label.

#include "NUIContextMenu.h"
#include "PatternSource.h"
#include "TrackManager.h"
#include "UnitRow.h"

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

NUIMouseEvent rightPress(float x, float y) {
    NUIMouseEvent e;
    e.type = NUIMouseEventType::Down;
    e.position = {x, y};
    e.button = NUIMouseButton::Right;
    e.pressed = true;
    return e;
}

struct Rig {
    std::shared_ptr<Aestra::Audio::TrackManager> tm{std::make_shared<Aestra::Audio::TrackManager>()};
    std::shared_ptr<NUIComponent> root{std::make_shared<NUIComponent>()};
    std::shared_ptr<UnitRow> row;
    Aestra::Audio::PatternID patternId{};
    Aestra::Audio::PatternID opened{};
    bool openedCalled = false;

    explicit Rig(Aestra::Audio::UnitType type) {
        auto& units = tm->getUnitManager();
        const auto id = units.createUnit("One-shot", type);
        patternId = tm->getPatternManager().createPattern();
        tm->getPatternManager().applyPatch(patternId, [](Aestra::Audio::PatternSource& p) {
            p.type = Aestra::Audio::PatternSource::Type::Midi;
            p.lengthBeats = 4.0;
            p.payload = Aestra::Audio::MidiPayload{};
        });
        if (auto* u = units.getUnit(id)) u->defaultPatternId = patternId;
        row = std::make_shared<UnitRow>(tm, units, id, patternId);
        row->setStepCount(16);
        row->setOnOpenPatternEditor([this](Aestra::Audio::PatternID pid) { opened = pid; openedCalled = true; });
        row->updateState();
        root->setBounds({0, 0, 700, 400});
        root->addChild(row);
        row->setBounds({0, 0, 700, 56});
    }

    std::shared_ptr<NUIContextMenu> visibleMenu() const {
        for (const auto& c : root->getChildren())
            if (auto m = std::dynamic_pointer_cast<NUIContextMenu>(c))
                if (m->isVisible()) return m;
        return nullptr;
    }
    static int indexOf(const std::shared_ptr<NUIContextMenu>& m, const std::string& label) {
        for (int i = 0; i < m->getItemCount(); ++i)
            if (auto it = m->getItem(i); it && it->getText() == label) return i;
        return -1;
    }
};

// Geometry of the 700x56 row: control width 266, separator 266, step grid from x ~280 onwards.
constexpr float kRowY = 28.0f;
constexpr float kNameX = 60.0f;   // inside the control area, left of the pills
constexpr float kGridX = 300.0f;  // on the step grid

} // namespace

int main() {
    {
        Rig r(Aestra::Audio::UnitType::Sampler);
        r.row->onMouseEvent(rightPress(kNameX, kRowY));
        auto menu = r.visibleMenu();
        check(menu != nullptr, "a right-click on a loaded one-shot's row opens the row menu");
        const int idx = menu ? Rig::indexOf(menu, "Open in Piano Roll") : -1;
        check(idx >= 0, "a Sampler unit offers 'Open in Piano Roll'");
        if (menu && idx >= 0) {
            if (auto cb = menu->getItem(idx)->getOnClick()) cb();
            check(r.openedCalled && r.opened == r.patternId, "choosing it opens the unit's own pattern in the piano roll");
        }
    }
    {
        Rig r(Aestra::Audio::UnitType::PitchedSampler);
        r.row->onMouseEvent(rightPress(kNameX, kRowY));
        auto menu = r.visibleMenu();
        check(menu && Rig::indexOf(menu, "Open in Piano Roll") >= 0, "an 808 (pitched sampler) unit offers it too");
    }
    {
        Rig r(Aestra::Audio::UnitType::Audio);
        r.row->onMouseEvent(rightPress(kNameX, kRowY));
        auto menu = r.visibleMenu();
        check(!menu || Rig::indexOf(menu, "Open in Piano Roll") < 0, "an Audio unit has no pattern to open, so no item");
    }
    {
        Rig r(Aestra::Audio::UnitType::Sampler);
        r.row->onMouseEvent(rightPress(kGridX, kRowY));
        check(r.visibleMenu() == nullptr, "a right-press on the step grid is still the erase gesture and opens no menu");
    }
    std::cout << (g_failures == 0 ? "[PASS] UnitRowContextMenuTest\n" : "[FAIL] UnitRowContextMenuTest\n");
    return g_failures == 0 ? 0 : 1;
}
