// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// RoutingModel (V8-C15): the read-only routing view answers feeds / outputs /
// output-by-name / group-by-output from a snapshot, spells Master one way, and
// never mutates the manager. Legality is not its job (see RoutingContractTest).

#include "Core/AudioGraph.h"
#include "Core/MixerChannel.h"
#include "Models/RoutingModel.h"
#include "Models/TrackManager.h"

#include <iostream>

namespace {

#define require(cond, msg)                                        \
    do {                                                          \
        if (!(cond)) {                                            \
            std::cerr << "FAIL: " << msg << std::endl;            \
            return 1;                                             \
        }                                                         \
    } while (0)

using Aestra::Audio::AudioRoute;
using Aestra::Audio::RoutingModel;

constexpr uint32_t kEngineMaster = 0xFFFFFFFFu;

} // namespace

int main() {
    Aestra::Audio::TrackManager tm;
    auto* drums = tm.addChannelWithId("Drums", 101);
    auto* kick = tm.addChannelWithId("Kick", 102);
    auto* snare = tm.addChannelWithId("Snare", 103);
    auto* verb = tm.addChannelWithId("Verb", 104);
    require(drums && kick && snare && verb, "channel creation failed");

    kick->setMainOutputId(101);
    snare->setMainOutputId(101);
    drums->setMainOutputId(kEngineMaster);
    verb->setMainOutputId(kEngineMaster);

    AudioRoute toVerb;
    toVerb.targetChannelId = 104;
    toVerb.gain = 0.5f;
    toVerb.postFader = false;
    kick->addSend(toVerb);
    const uint64_t sendId = kick->getSends()[0].sendId;

    AudioRoute sidechain;
    sidechain.targetChannelId = 101;
    sidechain.sidechainOnly = true;
    verb->addSend(sidechain);

    const RoutingModel model = RoutingModel::capture(tm);

    // Master is one spelling, whatever the engine stored.
    require(model.channels().size() == 4, "all four channels captured");
    for (const auto& ch : model.channels()) {
        require(ch.mainOutputId != kEngineMaster, "engine master spelling must not leak");
    }
    require(model.nameOf(RoutingModel::kMaster) == "Master", "master has a name");
    require(model.nameOf(102) == "Kick", "name by id");
    require(model.nameOf(999).empty(), "unknown id has no name");

    // Feeds: mains first, then sends, sidechain flagged not hidden.
    const auto drumFeeds = model.feeds(101);
    require(drumFeeds.size() == 3, "Drums fed by Kick, Snare and a sidechain send");
    require(drumFeeds[0].kind == RoutingModel::EdgeKind::Main && drumFeeds[1].kind == RoutingModel::EdgeKind::Main,
            "main feeds come first");
    require(drumFeeds[2].kind == RoutingModel::EdgeKind::Send && drumFeeds[2].sidechainOnly &&
                drumFeeds[2].sourceId == 104,
            "sidechain send is listed and flagged");

    const auto verbFeeds = model.feeds(104);
    require(verbFeeds.size() == 1 && verbFeeds[0].sourceId == 102, "Verb fed by Kick's send");
    require(verbFeeds[0].sendId == sendId, "send keeps its stable id");
    require(verbFeeds[0].gain == 0.5f && !verbFeeds[0].postFader, "send tap and level carried over");

    // Outputs: main first, then sends.
    const auto kickOut = model.outputsOf(102);
    require(kickOut.size() == 2 && kickOut[0].kind == RoutingModel::EdgeKind::Main && kickOut[0].targetId == 101 &&
                kickOut[1].targetId == 104,
            "Kick goes to Drums, then Verb");
    require(model.feeds(RoutingModel::kMaster).size() == 2, "Master fed by Drums and Verb mains");

    // Output by name.
    require(model.idsNamed("Drums") == std::vector<uint32_t>{101}, "name resolves to id");
    require(model.idsNamed("Master") == std::vector<uint32_t>{RoutingModel::kMaster}, "Master resolves");
    require(model.idsNamed("Nope").empty(), "unknown name resolves to nothing");

    // Group by output: Master first, then Drums with Kick and Snare in order.
    const std::vector<uint32_t> masterGroup{101, 104};
    const std::vector<uint32_t> drumsGroup{102, 103};
    const auto groups = model.groupByOutput();
    require(groups.size() == 2, "two output groups");
    require(groups[0].targetId == RoutingModel::kMaster && groups[0].targetName == "Master" &&
                groups[0].sourceIds == masterGroup,
            "Master group lists Drums and Verb");
    require(groups[1].targetId == 101 && groups[1].targetName == "Drums" &&
                groups[1].sourceIds == drumsGroup,
            "Drums group lists Kick and Snare in mixer order");

    // A snapshot: later edits to the manager do not change it.
    kick->setMainOutputId(kEngineMaster);
    require(model.outputsOf(102)[0].targetId == 101, "snapshot is not live");
    require(RoutingModel::capture(tm).outputsOf(102)[0].targetId == RoutingModel::kMaster, "fresh capture sees the edit");

    std::cout << "RoutingModelTest: ok\n";
    return 0;
}
