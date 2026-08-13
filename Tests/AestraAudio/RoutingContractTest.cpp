// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Routing Contract regression (D5 + D1 + D2): mutation-time cycle rejection
// lives in TrackManager::canRouteTo; routing mutations go through undoable
// commands that validate before applying; sends carry stable sendIds that
// survive index shifts, removal, and undo; a cyclic snapshot is marked
// corrupt by the compiler and never appended with leftover nodes.

#include "Commands/RoutingCommands.h"
#include "Core/AudioGraph.h"
#include "Core/MixerChannel.h"
#include "Models/TrackManager.h"

#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

#define require(cond, msg)                                        \
    do {                                                          \
        if (!(cond)) {                                            \
            std::cerr << "FAIL: " << msg << std::endl;            \
            return 1;                                             \
        }                                                         \
    } while (0)

constexpr uint32_t kMaster = 0xFFFFFFFFu;

int runCanRouteToTests(Aestra::Audio::TrackManager& tm) {
    auto* a = tm.addChannelWithId("A", 101);
    auto* b = tm.addChannelWithId("B", 102);
    auto* c = tm.addChannelWithId("C", 103);
    require(a && b && c, "channel creation failed");

    // Self-routes are illegal.
    require(!tm.canRouteTo(101, 101), "self-route must be illegal");

    // Master is a terminal sink: always a legal target, never a source.
    require(tm.canRouteTo(101, kMaster), "routing to master must be legal");
    require(tm.canRouteTo(101, 0), "routing to model-space master must be legal");
    require(!tm.canRouteTo(kMaster, 101), "master must not be a routable source");
    require(!tm.canRouteTo(0, 101), "model-space master must not be a routable source");

    // Dangling destinations are illegal.
    require(!tm.canRouteTo(101, 999), "dangling destination must be illegal");
    require(!tm.canRouteTo(999, 101), "dangling source must be illegal");

    // A -> B is legal; B's main -> A would be a cycle for A -> B.
    require(tm.canRouteTo(101, 102), "plain forward route must be legal");
    b->setMainOutputId(101);
    require(!tm.canRouteTo(101, 102), "cycle via main output must be illegal");
    // Routing into the loop without closing one stays legal: C -> A or C -> B
    // only feeds the loop, it never completes a path back to the source.
    require(tm.canRouteTo(103, 101), "feeding an existing loop must stay legal");
    require(tm.canRouteTo(103, 102), "feeding an existing loop must stay legal");
    b->setMainOutputId(kMaster);

    // Cycle via sends: C sends to B, then B sends to C is a cycle.
    {
        Aestra::Audio::AudioRoute sendToB;
        sendToB.targetChannelId = 102;
        c->addSend(sendToB);
        const uint64_t cSendId = c->getSends()[0].sendId;
        require(cSendId != 0, "send must be minted a stable id");
        require(!tm.canRouteTo(102, 103), "cycle via send must be illegal");
        require(tm.canRouteTo(102, 101), "non-cyclic route must remain legal");
        c->removeSend(cSendId);
    }

    // Sidechain edges are control-only and cannot form audible cycles.
    {
        Aestra::Audio::AudioRoute sidechain;
        sidechain.targetChannelId = 103;
        sidechain.sidechainOnly = true;
        b->addSend(sidechain);
        const uint64_t bSendId = b->getSends()[0].sendId;
        require(tm.canRouteTo(103, 102), "sidechain edge must not create an audible cycle");
        b->removeSend(bSendId);
    }

    // Long chains: A -> B -> C. C -> A would close the loop.
    a->setMainOutputId(102);
    b->setMainOutputId(103);
    require(tm.canRouteTo(101, 102), "A -> B legal in chain");
    require(!tm.canRouteTo(103, 101), "closing the chain into A must be illegal");
    require(!tm.canRouteTo(102, 101), "B -> A would close the chain back into itself");
    require(tm.canRouteTo(101, 103), "A -> C is legal; C terminates at master");
    a->setMainOutputId(kMaster);
    b->setMainOutputId(kMaster);

    std::cout << "canRouteTo contract cases passed\n";
    return 0;
}

int runCommandSeamTests(Aestra::Audio::TrackManager& tm) {
    auto* a = tm.addChannelWithId("A", 201);
    auto* b = tm.addChannelWithId("B", 202);
    require(a && b, "channel creation failed");
    auto& history = tm.getCommandHistory();

    // AddSendCommand: execute -> undo -> redo roundtrip.
    Aestra::Audio::AudioRoute route;
    route.targetChannelId = 202;
    route.gain = 0.5f;
    route.pan = 0.25f;
    route.postFader = false;
    {
        auto cmd = std::make_shared<Aestra::Audio::AddSendCommand>(tm, 201, route);
        history.pushAndExecute(cmd);
        auto sends = a->getSends();
        require(sends.size() == 1 && sends[0].targetChannelId == 202 && sends[0].gain == 0.5f,
                "add send command must apply the route");
        const uint64_t id = sends[0].sendId;
        require(id != 0, "add send must mint a stable sendId");

        history.undo();
        require(a->getSends().empty(), "undo of add send must remove the send");

        history.redo();
        sends = a->getSends();
        require(sends.size() == 1 && sends[0].gain == 0.5f && sends[0].pan == 0.25f &&
                    !sends[0].postFader,
                "redo of add send must restore the exact route");
        require(sends[0].sendId == id, "redo of add send must mint the same id");
        history.undo();
    }

    // SetMainOutputCommand: undo restores the previous main output.
    {
        auto cmd = std::make_shared<Aestra::Audio::SetMainOutputCommand>(tm, 201, 202);
        history.pushAndExecute(cmd);
        require(a->getMainOutputId() == 202, "set main output must apply");
        history.undo();
        require(a->getMainOutputId() == kMaster, "undo of main output must restore master");
        history.redo();
        require(a->getMainOutputId() == 202, "redo of main output must reapply");
        history.undo();
    }

    // EditSendCommand: level edit roundtrip; destination edit is validated.
    {
        Aestra::Audio::AudioRoute route2;
        route2.targetChannelId = 202;
        route2.gain = 0.5f;
        auto addCmd = std::make_shared<Aestra::Audio::AddSendCommand>(tm, 201, route2);
        history.pushAndExecute(addCmd);
        const uint64_t id = a->getSends()[0].sendId;

        auto edited = a->getSends()[0];
        edited.gain = 1.0f;
        auto editCmd = std::make_shared<Aestra::Audio::EditSendCommand>(tm, 201, id, edited);
        history.pushAndExecute(editCmd);
        require(a->getSends()[0].gain == 1.0f, "edit send level must apply");
        history.undo();
        require(a->getSends()[0].gain == 0.5f, "undo of send edit must restore previous level");
        history.redo();
        require(a->getSends()[0].gain == 1.0f, "redo of send edit must reapply");

        // Destination edit into a cycle must be rejected: the send stays
        // unchanged and no undo entry is recorded (pushAndExecute swallows
        // the validation exception internally).
        a->setMainOutputId(202); // A -> B main path
        auto cyclic = a->getSends()[0];
        cyclic.targetChannelId = 201; // send back to self
        auto badCmd = std::make_shared<Aestra::Audio::EditSendCommand>(tm, 201, id, cyclic);
        history.pushAndExecute(badCmd);
        require(a->getSends()[0].targetChannelId == 202, "rejected edit must not mutate the send");
        history.undo(); // undoes the gain edit; a phantom entry would undo something else
        auto afterReject = a->getSends();
        require(afterReject.size() == 1 && afterReject[0].gain == 0.5f,
                "rejected edit must not create an undo entry");
        history.undo(); // undo add send
        require(a->getSends().empty(), "add send must unwind cleanly");
        a->setMainOutputId(kMaster);
    }

    // RemoveSendCommand: undo restores the exact send (same id) at its index.
    {
        Aestra::Audio::AudioRoute s1;
        s1.targetChannelId = 202;
        s1.gain = 0.3f;
        Aestra::Audio::AudioRoute s2;
        s2.targetChannelId = 202;
        s2.gain = 0.7f;
        auto add1 = std::make_shared<Aestra::Audio::AddSendCommand>(tm, 201, s1);
        auto add2 = std::make_shared<Aestra::Audio::AddSendCommand>(tm, 201, s2);
        history.pushAndExecute(add1);
        history.pushAndExecute(add2);
        const uint64_t id0 = a->getSends()[0].sendId;
        const uint64_t id1 = a->getSends()[1].sendId;
        require(id0 != id1 && id0 != 0 && id1 != 0, "sends must mint distinct stable ids");

        auto rmCmd = std::make_shared<Aestra::Audio::RemoveSendCommand>(tm, 201, id0);
        history.pushAndExecute(rmCmd);
        auto sends = a->getSends();
        require(sends.size() == 1 && sends[0].gain == 0.7f, "remove send must remove by id");
        history.undo();
        sends = a->getSends();
        require(sends.size() == 2 && sends[0].gain == 0.3f && sends[1].gain == 0.7f,
                "undo of remove send must restore at original index");
        require(sends[0].sendId == id0 && sends[1].sendId == id1,
                "undo of remove send must restore the same stable ids");
        history.redo();
        sends = a->getSends();
        require(sends.size() == 1 && sends[0].gain == 0.7f, "redo of remove send must re-remove");
        history.undo();
        history.undo();
        history.undo();
    }

    std::cout << "command seam roundtrips passed\n";
    return 0;
}

int runSendIdentityTests(Aestra::Audio::TrackManager& tm) {
    auto* a = tm.addChannelWithId("A", 301);
    auto* b = tm.addChannelWithId("B", 302);
    auto* c = tm.addChannelWithId("C", 303);
    require(a && b && c, "channel creation failed");
    auto& history = tm.getCommandHistory();

    // Three sends on A; remove the FIRST; edit the LAST by its id. A
    // positional implementation would now target the shifted send.
    Aestra::Audio::AudioRoute r1;
    r1.targetChannelId = 302;
    r1.gain = 0.1f;
    Aestra::Audio::AudioRoute r2;
    r2.targetChannelId = 302;
    r2.gain = 0.2f;
    Aestra::Audio::AudioRoute r3;
    r3.targetChannelId = 303;
    r3.gain = 0.3f;
    history.pushAndExecute(std::make_shared<Aestra::Audio::AddSendCommand>(tm, 301, r1));
    history.pushAndExecute(std::make_shared<Aestra::Audio::AddSendCommand>(tm, 301, r2));
    history.pushAndExecute(std::make_shared<Aestra::Audio::AddSendCommand>(tm, 301, r3));

    auto sends = a->getSends();
    require(sends.size() == 3, "three sends expected");
    const uint64_t idA = sends[0].sendId;
    const uint64_t idB = sends[1].sendId;
    const uint64_t idC = sends[2].sendId;
    require(idA < idB && idB < idC, "send ids must be monotonically minted");

    // Remove send A by id, then edit send C by id.
    history.pushAndExecute(std::make_shared<Aestra::Audio::RemoveSendCommand>(tm, 301, idA));
    sends = a->getSends();
    require(sends.size() == 2 && sends[0].sendId == idB && sends[1].sendId == idC,
            "removal by id must leave the others in order");

    auto editedC = a->getSends()[1]; // idC now at index 1
    editedC.gain = 0.9f;
    history.pushAndExecute(std::make_shared<Aestra::Audio::EditSendCommand>(tm, 301, idC, editedC));
    sends = a->getSends();
    require(sends[1].gain == 0.9f && sends[1].targetChannelId == 303,
            "edit by id must target the right send after a shift");
    require(sends[0].gain == 0.2f, "the shifted send must be untouched");

    // A new send gets a fresh id — never a reuse of the removed one.
    Aestra::Audio::AudioRoute r4;
    r4.targetChannelId = 302;
    history.pushAndExecute(std::make_shared<Aestra::Audio::AddSendCommand>(tm, 301, r4));
    sends = a->getSends();
    require(sends.size() == 3, "third send back");
    require(sends[2].sendId != idA && sends[2].sendId > idC, "send ids must never be reused");

    // Undo back to the pre-edit state: identity survives the whole stack.
    history.undo(); // undo r4 add
    history.undo(); // undo edit C
    history.undo(); // undo remove A
    sends = a->getSends();
    require(sends.size() == 3 && sends[0].sendId == idA && sends[1].sendId == idB &&
                sends[2].sendId == idC,
            "full unwind must restore every original id");
    require(sends[0].gain == 0.1f && sends[1].gain == 0.2f && sends[2].gain == 0.3f,
            "full unwind must restore every original route");

    // Undo the three adds.
    history.undo();
    history.undo();
    history.undo();
    require(a->getSends().empty(), "sends must unwind cleanly");

    std::cout << "send identity cases passed\n";
    return 0;
}

int runSnapshotCycleTests() {
    // Compiler marks a cyclic snapshot corrupt; it never appends leftover nodes.
    Aestra::Audio::AudioGraph graph;
    Aestra::Audio::TrackRenderState a;
    a.trackId = 1;
    a.mainOutputId = 2;
    Aestra::Audio::TrackRenderState b;
    b.trackId = 2;
    b.mainOutputId = 1; // cycle A <-> B
    Aestra::Audio::TrackRenderState c;
    c.trackId = 3;
    c.mainOutputId = kMaster;
    graph.tracks = {a, b, c};

    Aestra::Audio::finalizeAudioGraphRouting(graph);
    require(graph.hasRoutingCycle, "cyclic snapshot must be flagged corrupt");
    require(graph.topologicalOrder.size() < graph.tracks.size(),
            "cycle must not be papered over with appended leftover nodes");

    // Acyclic: full order, no flag.
    Aestra::Audio::AudioGraph clean;
    Aestra::Audio::TrackRenderState x;
    x.trackId = 1;
    x.mainOutputId = 2;
    Aestra::Audio::TrackRenderState y;
    y.trackId = 2;
    y.mainOutputId = kMaster;
    clean.tracks = {x, y};
    Aestra::Audio::finalizeAudioGraphRouting(clean);
    require(!clean.hasRoutingCycle, "acyclic snapshot must not be flagged");
    require(clean.topologicalOrder.size() == clean.tracks.size(), "acyclic snapshot must be fully ordered");

    std::cout << "snapshot cycle handling passed\n";
    return 0;
}

} // namespace

int main() {
    {
        Aestra::Audio::TrackManager tm;
        if (int rc = runCanRouteToTests(tm); rc != 0) {
            return rc;
        }
    }
    {
        Aestra::Audio::TrackManager tm;
        if (int rc = runCommandSeamTests(tm); rc != 0) {
            return rc;
        }
    }
    {
        Aestra::Audio::TrackManager tm;
        if (int rc = runSendIdentityTests(tm); rc != 0) {
            return rc;
        }
    }
    if (int rc = runSnapshotCycleTests(); rc != 0) {
        return rc;
    }
    std::cout << "[PASS] RoutingContractTest\n";
    return 0;
}
