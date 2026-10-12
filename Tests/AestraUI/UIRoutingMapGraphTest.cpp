// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// FD-28 point 8 removed the routing minimap, so UIRoutingMap is the full graph only. This builds the graph from
// a real routing (two feeders into a bus, a send, the master) and checks it holds every channel and route and
// lays out without a minimap path, and that the node and edge sets follow a routing change.

#include "MixerViewModel.h"
#include "UIRoutingMap.h"

#include "Core/AudioGraph.h"
#include "Core/ChannelSlotMap.h"
#include "Models/TrackManager.h"

#include <iostream>
#include <string>

using namespace Aestra;

namespace {

int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok)
        ++g_failures;
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
    snare->addSend(send);
    tm.buildAndShareSlotMap();
    MixerViewModel vm;
    vm.syncFromEngine(tm, *tm.getChannelSlotMapShared());

    AestraUI::UIRoutingMap map;
    map.setViewModel(&vm);
    map.setBounds({0, 0, 1200, 800});
    map.onResize(1200, 800);
    map.onUpdate(0.016);

    std::cout << "[1] the graph holds every channel and the master\n";
    check(map.nodeCount() == 5, "four channels and the master are nodes (" + std::to_string(map.nodeCount()) + ")");
    check(map.edgeCount() >= 5, "every channel's route and the send are edges (" + std::to_string(map.edgeCount()) + ")");

    std::cout << "[2] a routing change shows up\n";
    {
        const size_t before = map.edgeCount();
        snare->setMainOutputId(0);
        vm.syncFromEngine(tm, *tm.getChannelSlotMapShared());
        map.setViewModel(&vm);
        map.onResize(1200, 800);
        map.onUpdate(0.016);
        check(map.nodeCount() == 5, "the same channels are still there");
        check(map.edgeCount() != before, "moving Snare off the bus changes the edges (" + std::to_string(before) + " -> " + std::to_string(map.edgeCount()) + ")");
    }

    std::cout << "[3] a pointer move over the graph is harmless\n";
    {
        AestraUI::NUIMouseEvent move;
        move.type = AestraUI::NUIMouseEventType::Move;
        move.position = {600, 400};
        map.onMouseEvent(move);
        check(map.nodeCount() == 5, "nothing changed");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
