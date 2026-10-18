// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The routing graph draws exceptions only (V81-R1, FD-25). This checks the layout rules on their own (plain
// channels are listed, not drawn; columns follow signal depth; a bus sits level with its feeders; loops are
// found) and then the widget built from a real routing: what it draws, what it lists, and that it follows a
// routing change.

#include "MixerViewModel.h"
#include "RoutingGraphLayout.h"
#include "UIRoutingMap.h"

#include "Core/AudioGraph.h"
#include "Core/ChannelSlotMap.h"
#include "Models/TrackManager.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace Aestra;
namespace RG = AestraUI::RoutingGraph;

namespace {

int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok)
        ++g_failures;
}

RG::Channel C(uint32_t id, uint32_t out = RG::kMaster, RG::Role role = RG::Role::Track, std::vector<RG::Send> sends = {}) {
    RG::Channel c;
    c.id = id;
    c.output = out;
    c.role = role;
    c.sends = std::move(sends);
    return c;
}

const RG::Placed* find(const RG::Plan& p, uint32_t id) {
    for (const auto& n : p.nodes)
        if (n.id == id)
            return &n;
    return nullptr;
}

} // namespace

int main() {
    std::cout << "[1] layout: plain channels are listed, the rest sit in depth columns\n";
    {
        // 1 Kick, 2 Snare, 3 Hats -> 4 Drum Bus; 5 Lead, 6 Double -> 7 Vox Bus; 7 sends to 8 Reverb;
        // 1 sidechains 9 Bass; 10 Pad is plain.
        std::vector<RG::Channel> chs = {
            C(1, 4, RG::Role::Track, {{9, true}}),
            C(2, 4),
            C(3, 4),
            C(4, RG::kMaster, RG::Role::Bus),
            C(5, 7),
            C(6, 7),
            C(7, RG::kMaster, RG::Role::Bus, {{8, false}}),
            C(8, RG::kMaster, RG::Role::Return),
            C(9),
            C(10),
        };
        const RG::Plan p = RG::plan(chs);
        check(p.direct.size() == 1 && p.direct[0] == 10, "only Pad goes straight to Master and is listed");
        check(p.nodes.size() == 9, "the other nine are drawn (" + std::to_string(p.nodes.size()) + ")");
        check(find(p, 1)->column == 0 && find(p, 5)->column == 0, "feeders are sources (column 0)");
        check(find(p, 4)->column == 1 && find(p, 7)->column == 1 && find(p, 9)->column == 1,
              "buses and the sidechained Bass are depth 1");
        check(find(p, 8)->column == 2, "the return fed by a bus is depth 2");
        check(p.columns == 3, "three columns");
        check(find(p, 4)->row == find(p, 2)->row, "Drum Bus is level with the middle of its three feeders");
        check(find(p, 7)->row > find(p, 4)->row, "Vox Bus sits below Drum Bus");
        check(p.customRoutes == 5 && p.sends == 1 && p.sidechains == 1, "the header counts 5 routes, 1 send, 1 sidechain");
        check(RG::columnHeading(0, chs, p) == "SOURCES", "column 0 is SOURCES");
        check(RG::columnHeading(2, chs, p).rfind("RETURNS", 0) == 0, "column 2 is RETURNS");

        const RG::Plan all = RG::plan(chs, true);
        check(all.direct.empty() && all.nodes.size() == 10, "All tracks draws Pad too");

        bool noOverlap = true;
        for (const auto& a : p.nodes)
            for (const auto& b : p.nodes)
                if (a.id != b.id && a.column == b.column && std::abs(a.row - b.row) < 1.0f)
                    noOverlap = false;
        check(noOverlap, "no two nodes in a column overlap");
    }

    std::cout << "[2] trace and loops\n";
    {
        std::vector<RG::Channel> chs = {C(1, 3), C(2, 3), C(3, RG::kMaster, RG::Role::Bus, {{4, false}}), C(4)};
        const RG::Trace t = RG::trace(chs, 3);
        check(t.upstream.count(1) && t.upstream.count(2), "the bus traces up to both feeders");
        check(t.downstream.count(4) && !t.upstream.count(4), "and down to the return it sends to");
        const auto loop = RG::wouldLoop(chs, 3);
        check(loop.count(1) && loop.count(2) && !loop.count(4), "routing the bus into a feeder would loop; into the return would not");
    }

    std::cout << "[2b] a sidechain is the end of the line\n";
    {
        // 1 Kick -> Master, sidechains 2 Bass; 2 Bass -> 3 Low Bus.
        std::vector<RG::Channel> chs = {C(1, RG::kMaster, RG::Role::Track, {{2, true}}), C(2, 3), C(3, RG::kMaster, RG::Role::Bus)};
        const RG::Trace t = RG::trace(chs, 1);
        check(t.downstream.count(2) && !t.downstream.count(3), "Kick reaches Bass's detector but its audio does not go on to the bus");
        check(t.listening.count(2) == 1, "Bass is marked as only listening, so its own output stays out of Kick's chain");
        const RG::Trace b = RG::trace(chs, 2);
        check(b.upstream.count(1) && b.downstream.count(3), "Bass still traces back to the Kick it listens to");
    }

    std::cout << "[3] a corrupt cycle does not hang the layout\n";
    {
        std::vector<RG::Channel> chs = {C(1, 2), C(2, 1)};
        const RG::Plan p = RG::plan(chs);
        check(p.nodes.size() == 2, "both channels are still placed");
    }

    Audio::TrackManager tm;
    auto* kick = tm.addChannel("Kick");
    auto* snare = tm.addChannel("Snare");
    auto* bus = tm.addChannel("Drum Bus");
    auto* verb = tm.addChannel("Reverb");
    auto* pad = tm.addChannel("Pad");
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
    map.setBounds({0, 0, 1366, 700});
    map.onResize(1366, 700);
    map.onUpdate(0.016);

    std::cout << "[4] the widget draws the custom routes and lists the rest\n";
    check(map.nodeCount() == 4, "Kick, Snare, Drum Bus and Reverb are drawn (" + std::to_string(map.nodeCount()) + ")");
    check(map.directCount() == 1, "Pad is listed as Direct to Master");
    check(map.columnOf(pad->getChannelId()) == -1, "Pad has no column");
    check(map.columnOf(bus->getChannelId()) == 1, "Drum Bus is depth 1");
    check(map.edgeCount() == 5, "two routes into the bus, one send, two outputs to Master (" + std::to_string(map.edgeCount()) + ")");

    std::cout << "[5] a routing change shows up without being told\n";
    {
        kick->setMainOutputId(0);
        vm.syncFromEngine(tm, *tm.getChannelSlotMapShared());
        map.onUpdate(0.016);
        check(map.directCount() == 2, "Kick back on Master joins the list");
        check(map.nodeCount() == 3, "and leaves the graph");
    }

    std::cout << "[6] All tracks draws everything\n";
    {
        map.setShowAllTracks(true);
        map.onUpdate(0.016);
        check(map.nodeCount() == 5 && map.directCount() == 0, "five nodes, nothing listed");
        map.setShowAllTracks(false);
        map.onUpdate(0.016);
        check(map.nodeCount() == 3, "back to custom routes only");
    }

    std::cout << "[7] pointer and keys are harmless with nothing under them\n";
    {
        AestraUI::NUIMouseEvent move;
        move.type = AestraUI::NUIMouseEventType::Move;
        move.position = {600, 400};
        map.onMouseEvent(move);
        AestraUI::NUIKeyEvent f;
        f.keyCode = AestraUI::NUIKeyCode::F;
        f.pressed = true;
        map.onKeyEvent(f);
        map.onUpdate(0.016);
        check(map.nodeCount() == 3, "nothing changed");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
