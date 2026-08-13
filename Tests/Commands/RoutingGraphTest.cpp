// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// get_routing_graph — authoritative read-only session topology for Muse.

#include "Commands/MuseGrammar.h"
#include "Commands/MuseService.h"
#include "Core/AudioGraph.h"
#include "Models/TrackManager.h"
#include "Plugin/PluginManager.h"

#include "AestraJSON.h"

#include <iostream>
#include <memory>
#include <string>

namespace {

using Aestra::JSON;
using Aestra::Audio::AudioRoute;
using Aestra::Audio::MuseService;
using Aestra::Audio::PatternID;
using Aestra::Audio::PluginManager;
using Aestra::Audio::TrackManager;

int g_failures = 0;

void check(bool condition, const std::string& label) {
    if (condition) {
        std::cout << "PASS: " << label << "\n";
    } else {
        std::cerr << "FAIL: " << label << "\n";
        ++g_failures;
    }
}

JSON call(MuseService& service, const std::string& request) {
    return JSON::parse(service.handleRequest(request));
}

JSON* findByString(JSON& entries, const std::string& field, const std::string& value) {
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].has(field) && entries[i][field].asString() == value) {
            return &entries[i];
        }
    }
    return nullptr;
}

bool hasIssueCode(JSON& entries, const std::string& code) {
    return findByString(entries, "issueCode", code) != nullptr;
}

} // namespace

int main() {
    auto& pluginManager = PluginManager::getInstance();
    if (!pluginManager.initialize()) {
        std::cerr << "FAIL: plugin manager initialize\n";
        return 1;
    }

    TrackManager tracks;
    tracks.getUnitManager().setPatternManager(&tracks.getPatternManager());
    MuseService service(&tracks, nullptr);

    // An empty session still has a stable Master destination.
    JSON response = call(service, R"({"id":1,"verb":"get_routing_graph"})");
    check(response["status"].asString() == "ok" &&
              response["result"]["status"].asString() == "resolved",
          "empty routing graph is resolved");
    check(response["result"]["destinations"].size() == 1 &&
              response["result"]["destinations"][0]["nodeId"].asString() == "master" &&
              response["result"]["destinations"][0]["stableIdentityAvailable"].asBool(),
          "empty graph exposes stable Master destination");
    check(response["result"]["sources"].size() == 0 &&
              response["result"]["mainRoutes"].size() == 0 &&
              response["result"]["sends"].size() == 0 &&
              response["result"]["unresolvedRoutes"].size() == 0,
          "empty graph has no fabricated routes");

    auto* drums = tracks.addChannelWithId("Drums", 41);
    auto* bus = tracks.addChannelWithId("Bus", 77);
    check(drums != nullptr && bus != nullptr, "mixer fixture created");
    if (!drums || !bus) return 1;

    drums->setMainOutputId(bus->getChannelId());
    AudioRoute masterSend{};
    masterSend.targetChannelId = 0xFFFFFFFFu;
    masterSend.gain = 0.25f;
    masterSend.pan = -0.2f;
    masterSend.postFader = false;
    drums->addSend(masterSend);
    AudioRoute busSidechain{};
    busSidechain.targetChannelId = bus->getChannelId();
    busSidechain.sidechainOnly = true;
    drums->addSend(busSidechain);

    auto& unitManager = tracks.getUnitManager();
    const auto unitId =
        unitManager.createUnit("Bass Unit", Aestra::Audio::UnitType::Instrument);
    unitManager.setUnitMixerChannel(unitId, bus->getChannelId());

    constexpr uint64_t kLargePatternId = 9007199254740993ULL;
    auto& patternManager = tracks.getPatternManager();
    const auto audioPatternId = patternManager.createAudioPatternWithId(
        PatternID{kLargePatternId}, "Long Audio", 4.0, Aestra::Audio::AudioSlicePayload{});
    patternManager.setPatternMixerChannel(audioPatternId, drums->getChannelId());

    auto plugin = pluginManager.createInstanceById("com.Aestrastudios.eq");
    check(plugin != nullptr, "built-in EQ instance created");
    if (plugin) {
        const bool initialized =
            plugin->initialize(pluginManager.getDefaultSampleRate(), pluginManager.getDefaultBlockSize());
        check(initialized, "built-in EQ initialized");
        if (initialized) {
            plugin->activate();
            drums->getEffectChain().prepare(pluginManager.getDefaultSampleRate(),
                                            pluginManager.getDefaultBlockSize());
            check(drums->getEffectChain().insertPlugin(4, plugin),
                  "built-in EQ inserted at positional slot 4");
        }
    }

    response = call(service, R"({"id":2,"verb":"get_routing_graph"})");
    JSON& graph = response["result"];
    check(response["status"].asString() == "ok" &&
              graph["status"].asString() == "resolved" &&
              graph["authority"].asString() == "project_model" &&
              graph["unresolvedRoutes"].size() == 0,
          "resolved project-model topology reports no unresolved routes");
    check(graph["sources"].size() == 2 && graph["destinations"].size() == 3 &&
              graph["mainRoutes"].size() == 2 && graph["sends"].size() == 2,
          "graph exposes sources, destinations, main paths, and sends");

    JSON* drumsDestination = findByString(graph["destinations"], "nodeId", "mixer:41");
    check(drumsDestination != nullptr &&
              (*drumsDestination)["stableIdentityAvailable"].asBool() &&
              (*drumsDestination)["mixerChannelId"].asNumber() == 41.0,
          "mixer destination exposes stable channel identity");
    if (drumsDestination) {
        JSON& chain = (*drumsDestination)["insertChain"];
        check(chain["identityKind"].asString() == "positional" &&
                  !chain["stableSlotIdentityAvailable"].asBool() &&
                  chain["slotCount"].asNumber() == 10.0 && chain["slots"].size() == 10,
              "insert chain declares ten positional slot boundaries");
        JSON& slot = chain["slots"][4];
        check(slot["state"].asString() == "active" &&
                  slot["pluginId"].asString() == "com.Aestrastudios.eq" &&
                  !slot["stableIdentityAvailable"].asBool() &&
                  slot["positionalIdentityAvailable"].asBool() &&
                  slot["position"]["mixerChannelId"].asNumber() == 41.0 &&
                  slot["position"]["slotIndex"].asNumber() == 4.0,
              "active plugin exposes only its channel-and-slot position");
    }

    JSON* unitSource = findByString(graph["sources"], "nodeId", "unit:" + std::to_string(unitId));
    check(unitSource != nullptr && (*unitSource)["unitId"].asString() == std::to_string(unitId) &&
              (*unitSource)["destination"]["nodeId"].asString() == "mixer:77" &&
              (*unitSource)["destination"]["resolved"].asBool(),
          "Arsenal unit source resolves by stable IDs");

    JSON* audioSource =
        findByString(graph["sources"], "nodeId", "audio_pattern:" + std::to_string(kLargePatternId));
    check(audioSource != nullptr &&
              (*audioSource)["patternId"].asString() == std::to_string(kLargePatternId) &&
              (*audioSource)["destination"]["nodeId"].asString() == "mixer:41",
          "audio-pattern source preserves IDs beyond JSON integer precision");

    JSON* drumsMain = findByString(graph["mainRoutes"], "sourceNodeId", "mixer:41");
    check(drumsMain != nullptr && (*drumsMain)["resolved"].asBool() &&
              (*drumsMain)["destinationNodeId"].asString() == "mixer:77",
          "main route resolves stable mixer destination");
    JSON& send = graph["sends"][0];
    check(send["destinationNodeId"].asString() == "master" &&
              send["routeType"].asString() == "send" &&
              send["stableIdentityAvailable"].asBool() &&
              send["sendId"].asNumber() != 0.0 &&
              !send["positionalIdentityAvailable"].asBool() &&
              !send["postFader"].asBool(),
          "send exposes topology and stable send identity");
    JSON& sidechain = graph["sends"][1];
    check(sidechain["destinationNodeId"].asString() == "mixer:77" &&
              sidechain["routeType"].asString() == "sidechain_send" &&
              sidechain["sidechainOnly"].asBool(),
          "sidechain send remains distinct from audible routing");

    // Missing targets stay visible as unresolved evidence instead of silently
    // falling back to Master.
    unitManager.setUnitMixerChannel(unitId, 9001);
    patternManager.setPatternMixerChannel(audioPatternId, 9002);
    drums->setMainOutputId(9003);
    AudioRoute missingSend{};
    missingSend.targetChannelId = 9004;
    bus->addSend(missingSend);

    response = call(service, R"({"id":3,"verb":"get_routing_graph"})");
    JSON& degraded = response["result"];
    check(degraded["status"].asString() == "degraded" &&
              degraded["unresolvedRoutes"].size() == 4,
          "missing destinations degrade the graph without hiding routes");
    check(hasIssueCode(degraded["unresolvedRoutes"], "unresolved_unit_destination") &&
              hasIssueCode(degraded["unresolvedRoutes"], "unresolved_audio_pattern_destination") &&
              hasIssueCode(degraded["unresolvedRoutes"], "unresolved_main_destination") &&
              hasIssueCode(degraded["unresolvedRoutes"], "unresolved_send_destination"),
          "unresolved route types have stable issue codes");

    JSON schema = JSON::parse(Aestra::Audio::MuseGrammar::schemaToJsonString());
    bool documented = false;
    for (size_t i = 0; i < schema["queries"].size(); ++i) {
        if (schema["queries"][i]["verb"].asString() == "get_routing_graph") {
            documented = schema["queries"][i]["args"].asString() == "none";
        }
    }
    check(documented, "schema discovers get_routing_graph with no arguments");

    response =
        call(service, R"({"id":4,"verb":"get_routing_graph","args":{"includeLatency":true}})");
    check(response["status"].asString() == "validation_error",
          "get_routing_graph rejects latency and all other arguments");

    if (g_failures == 0) {
        std::cout << "All RoutingGraph tests passed\n";
        return 0;
    }
    std::cerr << g_failures << " RoutingGraph test(s) failed\n";
    return 1;
}
