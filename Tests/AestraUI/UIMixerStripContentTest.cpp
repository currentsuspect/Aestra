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
#include "Models/MeterSnapshot.h"
#include "Core/AudioGraph.h"
#include "Models/TrackManager.h"
#include "Plugin/EffectChain.h"
#include "Plugin/PluginManager.h"
#include "Plugin/SamplerPlugin.h"

#include <cmath>
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

    std::cout << "[6] a channel's role follows the routing\n";
    {
        Audio::TrackManager rt;
        auto* a = rt.addChannel("A");
        auto* b = rt.addChannel("Bus");
        auto* c = rt.addChannel("Reverb");
        auto* d = rt.addChannel("Plain");
        a->setMainOutputId(b->getChannelId());
        Audio::AudioRoute send;
        send.targetChannelId = c->getChannelId();
        send.gain = 0.5f;
        d->addSend(send);
        rt.buildAndShareSlotMap();
        MixerViewModel rvm;
        rvm.syncFromEngine(rt, *rt.getChannelSlotMapShared());
        using Role = ChannelViewModel::Role;
        check(rvm.getChannelById(b->getChannelId())->role == Role::Bus, "a channel others route into is a Bus");
        check(rvm.getChannelById(c->getChannelId())->role == Role::Return, "a channel that receives sends is a Return");
        check(rvm.getChannelById(a->getChannelId())->role == Role::Track && rvm.getChannelById(d->getChannelId())->role == Role::Track,
              "the rest are plain tracks");
    }

    std::cout << "[7] peak since reset keeps the highest peak until it is reset\n";
    {
        Audio::MeterSnapshotBuffer snaps;
        auto* ch = vm.getChannelById(drums->getChannelId());
        snaps.writeLevels(static_cast<int>(ch->slotIndex), 0.5f, 0.25f, 0.2f, 0.2f, 0.0f, 0.0f, 0.0f);
        vm.updateMeters(snaps, 0.016);
        const float first = ch->peakSinceResetDb;
        check(std::fabs(first - (-6.02f)) < 0.1f, "reads the loudest side (-6 dB)");
        snaps.writeLevels(static_cast<int>(ch->slotIndex), 0.1f, 0.1f, 0.05f, 0.05f, 0.0f, 0.0f, 0.0f);
        for (int i = 0; i < 200; ++i)
            vm.updateMeters(snaps, 0.016);
        check(std::fabs(ch->peakSinceResetDb - first) < 1e-4f, "stays after the signal falls away");
        vm.resetPeaks();
        check(ch->peakSinceResetDb <= MixerMath::DB_MIN + 0.01f, "resetPeaks clears it");
    }

    std::cout << "[8] the strip lists its sends and shows pan as a bar\n";
    {
        Audio::TrackManager st;
        auto* src = st.addChannel("Src");
        auto* verb = st.addChannel("Verb");
        Audio::AudioRoute send;
        send.targetChannelId = verb->getChannelId();
        send.gain = 0.5f;
        src->addSend(send);
        st.buildAndShareSlotMap();
        MixerViewModel svm;
        svm.syncFromEngine(st, *st.getChannelSlotMapShared());
        st.getContinuousParams()->setPan(svm.getChannelById(src->getChannelId())->slotIndex, -0.4f);
        svm.syncFromEngine(st, *st.getChannelSlotMapShared());
        AestraUI::UIMixerStrip strip(src->getChannelId(), 1, &svm, st.getMeterSnapshots(), st.getContinuousParams());
        strip.setBounds({0, 0, 84, 640});
        strip.onUpdate(0.016);
        const auto* sends = strip.sendList();
        check(sends && sends->rows().size() == 1 && sends->rows()[0].name == "Verb" &&
                  std::fabs(sends->rows()[0].levelDb - (-6.02f)) < 0.1f && !sends->rows()[0].sidechain,
              "one send, to Verb, at -6 dB");
        check(strip.panBar() && std::fabs(strip.panBar()->value() - (-0.4f)) < 1e-4f, "the pan bar follows the channel's pan");
        check(strip.meterBarsRect().width > 20.0f && strip.meterBarsRect().height > 100.0f, "the meters have room (24 px wide, tall)");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
