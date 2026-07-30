// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// get_latency_report — observational PDC solve and application diagnostics for Muse.

#include "AestraJSON.h"
#include "Commands/MuseGrammar.h"
#include "Commands/MuseService.h"
#include "Core/AudioEngine.h"
#include "Core/AudioGraph.h"
#include "Models/TrackManager.h"
#include "PDCTestHelpers.h"

#include <iostream>
#include <memory>
#include <string>

namespace {

using Aestra::JSON;
using Aestra::Audio::AudioEngine;
using Aestra::Audio::AudioRoute;
using Aestra::Audio::MuseService;
using Aestra::Audio::TrackManager;
using Aestra::Audio::PDCTest::MockLatencyPlugin;

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

struct Fixture {
    std::shared_ptr<TrackManager> tracks{std::make_shared<TrackManager>()};
    AudioEngine engine;
    MuseService service{tracks.get(), &engine};

    Fixture() {
        engine.setSampleRate(48000);
        engine.setBufferConfig(256, 2);
        engine.setTrackManager(tracks);
    }
};

void testUnobservedWithoutEngine() {
    TrackManager tracks;
    MuseService service(&tracks, nullptr);
    JSON response = call(service, R"({"id":1,"verb":"get_latency_report"})");
    JSON& report = response["result"];
    check(response["status"].asString() == "ok" && report["status"].asString() == "unobserved" &&
              !report["observed"].asBool(),
          "missing engine reports unobserved");
    check(report["nodes"].size() == 0 && report["edges"].size() == 0 && report["mismatches"].size() == 0,
          "unobserved report fabricates no topology");
}

void testCleanSolveExposesNodeAndAppliedLatency() {
    Fixture fx;
    auto* slow = fx.tracks->addChannelWithId("Slow", 41);
    auto* dry = fx.tracks->addChannelWithId("Dry", 77);
    check(slow != nullptr && dry != nullptr, "clean latency fixture created");
    if (!slow || !dry)
        return;

    auto plugin = std::make_shared<MockLatencyPlugin>(256, "Slow Plugin");
    check(slow->getEffectChain().insertPlugin(0, plugin), "latency plugin inserted");
    fx.engine.calculateLatencyCompensation();

    JSON response = call(fx.service, R"({"id":2,"verb":"get_latency_report"})");
    JSON& report = response["result"];
    check(report["status"].asString() == "clean" && report["observed"].asBool() &&
              report["authority"].asString() == "audio_engine_pdc" && report["mismatches"].size() == 0,
          "current solved and applied PDC state reports clean");
    check(report["graphMaximum"]["projectAlignmentSamples"].asNumber() == 256.0 &&
              report["graphMaximum"]["engineMaxProjectLatencySamples"].asNumber() == 256.0,
          "graph maximum exposes solved and engine values");

    JSON* slowNode = findByString(report["nodes"], "nodeId", "mixer:41");
    JSON* dryNode = findByString(report["nodes"], "nodeId", "mixer:77");
    JSON* masterNode = findByString(report["nodes"], "nodeId", "master");
    check(slowNode != nullptr && dryNode != nullptr && masterNode != nullptr,
          "node report uses stable mixer and Master identities");
    if (slowNode && dryNode) {
        check((*slowNode)["intrinsicLatencySamples"].asNumber() == 256.0 &&
                  (*slowNode)["outputCompensationSamples"].asNumber() == 0.0 &&
                  (*slowNode)["applied"]["intrinsicLatencySamples"].asNumber() == 256.0 &&
                  !(*slowNode)["mismatch"].asBool(),
              "slow node exposes solved and applied latency");
        check((*dryNode)["intrinsicLatencySamples"].asNumber() == 0.0 &&
                  (*dryNode)["outputCompensationSamples"].asNumber() == 256.0 &&
                  (*dryNode)["applied"]["outputCompensationSamples"].asNumber() == 256.0 &&
                  !(*dryNode)["mismatch"].asBool(),
              "dry node exposes compensation applied for alignment");
    }
}

void testBranchCompensationAndSidechainLimitation() {
    Fixture fx;
    auto* source = fx.tracks->addChannelWithId("Source", 10);
    auto* bus = fx.tracks->addChannelWithId("Bus", 20);
    check(source != nullptr && bus != nullptr, "branch latency fixture created");
    if (!source || !bus)
        return;

    auto busPlugin = std::make_shared<MockLatencyPlugin>(300, "Bus Plugin");
    check(bus->getEffectChain().insertPlugin(0, busPlugin), "bus latency plugin inserted");

    AudioRoute send;
    send.targetChannelId = bus->getChannelId();
    source->addSend(send);
    fx.engine.calculateLatencyCompensation();

    JSON response = call(fx.service, R"({"id":3,"verb":"get_latency_report"})");
    JSON& clean = response["result"];
    check(clean["status"].asString() == "clean" && clean["edges"].size() == 3,
          "audible branch is fully represented and clean");

    JSON* mainEdge = nullptr;
    JSON* sendEdge = nullptr;
    for (size_t i = 0; i < clean["edges"].size(); ++i) {
        JSON& edge = clean["edges"][i];
        if (edge["sourceNodeId"].asString() != "mixer:10")
            continue;
        if (edge["routeType"].asString() == "main")
            mainEdge = &edge;
        if (edge["routeType"].asString() == "send")
            sendEdge = &edge;
    }
    check(mainEdge != nullptr && sendEdge != nullptr, "branch edges retain main and positional send boundaries");
    if (mainEdge && sendEdge) {
        check((*mainEdge)["solverCompensationSamples"].asNumber() == 300.0 &&
                  (*mainEdge)["appliedCompensationSamples"].asNumber() == 300.0 && !(*mainEdge)["mismatch"].asBool(),
              "main branch exposes matched solver and RT compensation");
        check((*sendEdge)["solverCompensationSamples"].asNumber() == 0.0 &&
                  (*sendEdge)["appliedCompensationSamples"].asNumber() == 0.0 &&
                  (*sendEdge)["positionalIdentityAvailable"].asBool() && (*sendEdge)["sendIndex"].asNumber() == 0.0,
              "send branch exposes positional identity and applied delay");
    }

    source->removeSend(0);
    send.sidechainOnly = true;
    source->addSend(send);
    fx.engine.calculateLatencyCompensation();

    response = call(fx.service, R"({"id":4,"verb":"get_latency_report"})");
    JSON& degraded = response["result"];
    check(degraded["status"].asString() == "degraded" && degraded["uncompensatedPaths"].size() == 1 &&
              hasIssueCode(degraded["uncompensatedPaths"], "sidechain_latency_compensation_unavailable"),
          "sidechain path is explicitly reported as uncompensated");
    check(degraded["uncompensatedPaths"][0]["evidence"]["stableEndpointIdentityAvailable"].asBool() &&
              degraded["uncompensatedPaths"][0]["evidence"]["positionalIdentityAvailable"].asBool() &&
              degraded["uncompensatedPaths"][0]["evidence"]["sendIndex"].asNumber() == 0.0,
          "uncompensated sidechain evidence identifies stable endpoints and positional send");
}

void testPendingSolveStaysObservational() {
    Fixture fx;
    auto* channel = fx.tracks->addChannelWithId("Changing", 99);
    check(channel != nullptr, "pending latency fixture created");
    if (!channel)
        return;

    auto plugin = std::make_shared<MockLatencyPlugin>(128, "Changing Plugin");
    check(channel->getEffectChain().insertPlugin(0, plugin), "changing latency plugin inserted");
    fx.engine.calculateLatencyCompensation();
    const std::string generationBefore =
        call(fx.service, R"({"id":5,"verb":"get_latency_report"})")["result"]["generation"].asString();

    plugin->setLatencySamples(512);
    fx.engine.markLatencyDirty();
    JSON response = call(fx.service, R"({"id":6,"verb":"get_latency_report"})");
    JSON& report = response["result"];
    check(report["status"].asString() == "degraded" && report["recalculationPending"].asBool() &&
              hasIssueCode(report["mismatches"], "pdc_recalculation_pending") &&
              hasIssueCode(report["mismatches"], "pdc_node_intrinsic_latency_stale"),
          "pending recalculation and stale intrinsic latency have stable issue codes");
    check(report["generation"].asString() == generationBefore &&
              fx.engine.getLastSolvedLatencyTopology().generation ==
                  static_cast<uint64_t>(std::stoull(generationBefore)),
          "query does not recalculate or mutate the published topology");
}

void testDisabledCompensationStatus() {
    Fixture fx;
    auto* channel = fx.tracks->addChannelWithId("Disabled", 123);
    check(channel != nullptr, "disabled latency fixture created");
    if (!channel)
        return;

    fx.engine.calculateLatencyCompensation();
    const uint64_t generationBefore = fx.engine.getLastSolvedLatencyTopology().generation;
    fx.engine.setLatencyCompensationEnabled(false);

    JSON response = call(fx.service, R"({"id":7,"verb":"get_latency_report"})");
    JSON& report = response["result"];
    check(report["status"].asString() == "disabled" && report["observed"].asBool() &&
              !report["compensationEnabled"].asBool(),
          "disabled global compensation has an explicit report status");
    check(fx.engine.getLastSolvedLatencyTopology().generation == generationBefore,
          "disabled query does not publish a replacement topology");
}

void testSchemaAndArgumentRejection() {
    JSON schema = JSON::parse(Aestra::Audio::MuseGrammar::schemaToJsonString());
    bool documented = false;
    for (size_t i = 0; i < schema["queries"].size(); ++i) {
        if (schema["queries"][i]["verb"].asString() == "get_latency_report") {
            documented = schema["queries"][i]["args"].asString() == "none";
        }
    }
    check(documented, "schema discovers get_latency_report with no arguments");

    TrackManager tracks;
    MuseService service(&tracks, nullptr);
    JSON response = call(service, R"({"id":8,"verb":"get_latency_report","args":{"recalculate":true}})");
    check(response["status"].asString() == "validation_error",
          "get_latency_report rejects recalculation and all other arguments");
}

} // namespace

int main() {
    testUnobservedWithoutEngine();
    testCleanSolveExposesNodeAndAppliedLatency();
    testBranchCompensationAndSidechainLimitation();
    testPendingSolveStaysObservational();
    testDisabledCompensationStatus();
    testSchemaAndArgumentRejection();

    if (g_failures == 0) {
        std::cout << "All LatencyReport tests passed\n";
        return 0;
    }
    std::cerr << g_failures << " LatencyReport test(s) failed\n";
    return 1;
}
