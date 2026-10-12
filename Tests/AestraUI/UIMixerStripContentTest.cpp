// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// FD-28 point 3, "the strip says what it does": a mixer strip names its inserts (on / bypassed) and its output
// (by name and colour), instead of an FX count and a channel number.
//
// Real TrackManager, real MixerViewModel, real UIMixerStrip: the strip is built the way the panel builds it,
// updated once, and what it would draw is read back from its parts.

#include "MixerViewModel.h"
#include "UIMixerInsertList.h"
#include "UIMixerStrip.h"
#include "UIOutputChip.h"

#include "Core/ChannelSlotMap.h"
#include "Models/TrackManager.h"
#include "Plugin/EffectChain.h"
#include "Plugin/PluginManager.h"
#include "Plugin/SamplerPlugin.h"

#include <iostream>
#include <memory>
#include <string>

using namespace Aestra;

namespace {

int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok)
        ++g_failures;
}

Audio::PluginInstancePtr makeSampler() {
    auto plugin = std::make_shared<Audio::Plugins::SamplerPlugin>();
    plugin->initialize(48000.0, 512);
    return plugin;
}

} // namespace

int main() {
    Audio::TrackManager tm;
    Audio::MixerChannel* drums = tm.addChannel("Drums");
    Audio::MixerChannel* bus = tm.addChannel("Drum Bus");
    Audio::MixerChannel* lone = tm.addChannel("Lone");
    if (!drums || !bus || !lone) {
        std::cerr << "[FAIL] setup\n";
        return 1;
    }
    drums->setMainOutputId(bus->getChannelId());
    drums->getEffectChain().insertPlugin(0, makeSampler());
    drums->getEffectChain().insertPlugin(1, makeSampler());
    drums->getEffectChain().setSlotBypassed(1, true);

    tm.buildAndShareSlotMap();
    auto slotMap = tm.getChannelSlotMapShared();
    MixerViewModel vm;
    vm.syncFromEngine(tm, *slotMap);

    auto makeStrip = [&](uint32_t id) {
        auto strip = std::make_shared<AestraUI::UIMixerStrip>(id, 1, &vm, tm.getMeterSnapshots(), tm.getContinuousParams());
        strip->setBounds({0, 0, 110, 560});
        strip->onUpdate(0.016);
        return strip;
    };

    std::cout << "[1] a routed channel names its output\n";
    {
        auto strip = makeStrip(drums->getChannelId());
        const auto* chip = strip->outputChip();
        check(chip && chip->isVisible(), "the strip has an output chip");
        check(chip && chip->label() == "Drum Bus", "the chip names the bus it goes to, not a number: '" +
                                                          (chip ? chip->label() : std::string("<none>")) + "'");
        check(chip && !chip->isDimmed(), "a live route is not dimmed");
    }

    std::cout << "[2] a channel on the master says so\n";
    {
        auto strip = makeStrip(lone->getChannelId());
        const auto* chip = strip->outputChip();
        check(chip && chip->label() == "Master", "an unrouted channel reads 'Master'");
    }

    std::cout << "[3] the insert chain is listed by name, bypassed ones kept\n";
    {
        auto strip = makeStrip(drums->getChannelId());
        const auto* list = strip->insertList();
        check(list && list->rows().size() == 2, "two inserts are listed (empty slots are not)");
        check(list && list->rows().size() == 2 && !list->rows()[0].name.empty() && !list->rows()[0].bypassed,
              "the first is named and on");
        check(list && list->rows().size() == 2 && list->rows()[1].bypassed, "the second is listed as bypassed");
    }
    {
        auto strip = makeStrip(lone->getChannelId());
        const auto* list = strip->insertList();
        check(list && list->rows().empty(), "a channel with no inserts lists none (it shows the + insert invitation)");
    }

    std::cout << "[4] the master has no output chip\n";
    {
        auto strip = makeStrip(0);
        check(strip->outputChip() && !strip->outputChip()->isVisible(), "the master strip hides the chip");
    }

    std::cout << "[5] the list folds rows that do not fit\n";
    {
        AestraUI::UIMixerInsertList list;
        list.setBounds({0, 0, 100, AestraUI::UIMixerInsertList::heightForRows(3)});
        check(list.visibleRowCapacity() == 3, "three rows of height hold three rows");
        list.setBounds({0, 0, 100, AestraUI::UIMixerInsertList::heightForRows(1)});
        check(list.visibleRowCapacity() == 1, "one row of height holds one");
        bool clicked = false;
        list.onClicked = [&] { clicked = true; };
        AestraUI::NUIMouseEvent down;
        down.type = AestraUI::NUIMouseEventType::Down;
        down.position = {10, 10};
        down.button = AestraUI::NUIMouseButton::Left;
        down.pressed = true;
        AestraUI::NUIMouseEvent up = down;
        up.type = AestraUI::NUIMouseEventType::Up;
        up.pressed = false;
        up.released = true;
        list.onMouseEvent(down);
        list.onMouseEvent(up);
        check(clicked, "a click on the list reports to its owner");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
