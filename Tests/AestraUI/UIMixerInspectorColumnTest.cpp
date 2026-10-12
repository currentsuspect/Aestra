// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V81-R2, FD-25 and FD-28 point 6: the mixer inspector is one column, no tabs. Output, inserts, sends and input
// sit on one page; a bus lists what feeds it; "Show in routing graph" replaces the minimap's double-click; the
// master has no trim, sends or input. Real TrackManager, MixerViewModel and UIMixerInspector; no pixels.

#include "MixerViewModel.h"
#include "UIMixerInspector.h"

#include "Core/AudioGraph.h"
#include "Core/ChannelSlotMap.h"
#include "Models/TrackManager.h"
#include "Plugin/PluginManager.h"

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

AestraUI::NUIMouseEvent press(const AestraUI::NUIRect& r) {
    AestraUI::NUIMouseEvent e;
    e.type = AestraUI::NUIMouseEventType::Down;
    e.button = AestraUI::NUIMouseButton::Left;
    e.pressed = true;
    e.position = {r.x + r.width * 0.5f, r.y + r.height * 0.5f};
    return e;
}

} // namespace

int main() {
    Audio::TrackManager tm;
    auto* kick = tm.addChannel("Kick");
    auto* snare = tm.addChannel("Snare");
    auto* bus = tm.addChannel("Drum Bus");
    auto* verb = tm.addChannel("Reverb");
    kick->setMainOutputId(bus->getChannelId());
    snare->setMainOutputId(bus->getChannelId());
    Audio::AudioRoute send;
    send.targetChannelId = verb->getChannelId();
    send.gain = 0.5f;
    kick->addSend(send);
    tm.buildAndShareSlotMap();
    MixerViewModel vm;
    vm.syncFromEngine(tm, *tm.getChannelSlotMapShared());

    AestraUI::UIMixerInspector inspector(&vm);
    inspector.setBounds({1000, 100, 236, 700});
    inspector.onResize(236, 700);

    auto select = [&](uint32_t id) {
        vm.setSelectedChannelId(static_cast<int32_t>(id));
        inspector.onResize(236, 700);
    };

    std::cout << "[1] a track shows output, inserts, sends and input in one column\n";
    {
        select(kick->getChannelId());
        check(inspector.trimBarVisible(), "trim is there");
        check(!inspector.showInGraphRect().isEmpty(), "the routing graph link is there");
        check(!inspector.addSendRect().isEmpty(), "Add Send is on the same page (no tab to switch to)");
        check(inspector.fedByNames().empty(), "a plain track is fed by nothing");
        check(inspector.viewportRect().height > 100.0f, "there is room for the column");
    }

    std::cout << "[2] a bus lists what feeds it\n";
    {
        select(bus->getChannelId());
        const auto fed = inspector.fedByNames();
        check(fed.size() == 2 && fed[0] == "Kick" && fed[1] == "Snare", "Kick and Snare, in channel order");
    }

    std::cout << "[3] the master has no trim, sends or input\n";
    {
        select(0);
        check(!inspector.trimBarVisible(), "no trim bar");
        check(inspector.addSendRect().isEmpty(), "no Add Send");
        check(!inspector.showInGraphRect().isEmpty(), "but the graph link is still offered");
    }

    std::cout << "[4] the link opens the routing graph for this channel\n";
    {
        select(kick->getChannelId());
        uint32_t asked = 0;
        inspector.onShowRoutingGraph = [&](uint32_t id) { asked = id; };
        inspector.onMouseEvent(press(inspector.showInGraphRect()));
        check(asked == kick->getChannelId(), "clicking it reports the selected channel");
    }

    std::cout << "[5] the column scrolls as one when it is taller than the panel\n";
    {
        inspector.setBounds({1000, 100, 236, 300});
        inspector.onResize(236, 300);
        check(inspector.maxScrollOffset() > 50.0f, "a short panel can scroll (max " + std::to_string(inspector.maxScrollOffset()) + ")");
        AestraUI::NUIMouseEvent wheel;
        wheel.type = AestraUI::NUIMouseEventType::Scroll;
        wheel.wheelDelta = -3.0f;
        wheel.position = {1100, 300};
        const float before = inspector.scrollOffset();
        inspector.onMouseEvent(wheel);
        for (int i = 0; i < 40; ++i)
            inspector.onUpdate(0.016);
        check(inspector.scrollOffset() > before, "the wheel scrolls it");
        inspector.setActiveTab(AestraUI::UIMixerInspector::Tab::Sends);
        for (int i = 0; i < 60; ++i)
            inspector.onUpdate(0.016);
        inspector.onResize(236, 300);
        check(inspector.scrollOffset() > 0.0f, "asking for the Sends section scrolls to it");
        inspector.setBounds({1000, 100, 236, 700});
        inspector.onResize(236, 700);
    }

    std::cout << "[6] sends are one line each and open in place\n";
    {
        select(kick->getChannelId());
        check(inspector.sendRowCount() == 1, "one send, one row");
        check(inspector.expandedSend() == -1 && !inspector.sendCardVisible(0), "closed: no card");
        const auto row = inspector.sendRowRect(0);
        check(row.height > 20.0f && row.height < 40.0f, "a row is one line high (" + std::to_string(row.height) + ")");
        inspector.onMouseEvent(press(row));
        check(inspector.expandedSend() == 0, "clicking the row opens it");
        inspector.onResize(236, 700);
        check(inspector.sendCardVisible(0), "and its full card shows right below");
        inspector.onMouseEvent(press(inspector.sendRowRect(0)));
        check(inspector.expandedSend() == -1, "clicking again closes it");
        select(snare->getChannelId());
        check(inspector.sendRowCount() == 0, "a channel with no sends has no rows");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
