// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// get_project_load_report — loader truth exposed through Muse without recovery side effects.

#include "App/MuseProjectLoadReport.h"
#include "Commands/MuseGrammar.h"
#include "Commands/MuseService.h"
#include "Models/TrackManager.h"
#include "Plugin/PluginManager.h"
#include "Support/TestTempDirectory.h"

#include "AestraJSON.h"

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace {

using Aestra::JSON;
using Aestra::MuseProjectLoadOrigin;
using Aestra::makeMuseProjectLoadReport;
using Aestra::Audio::MuseService;
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

std::shared_ptr<TrackManager> makeTracks() {
    auto tracks = std::make_shared<TrackManager>();
    tracks->getPlaylistModel().setPatternManager(&tracks->getPatternManager());
    tracks->getUnitManager().setPatternManager(&tracks->getPatternManager());
    return tracks;
}

bool containsIssueCode(JSON& entries, const std::string& code) {
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].has("issueCode") && entries[i]["issueCode"].asString() == code) {
            return true;
        }
    }
    return false;
}

} // namespace

int main() {
    if (!Aestra::Audio::PluginManager::getInstance().initialize()) {
        std::cerr << "FAIL: plugin manager initialize\n";
        return 1;
    }

    MuseService service(nullptr, nullptr);

    // 1. A blank/new session has no historical load result.
    JSON response = call(service, R"({"id":1,"verb":"get_project_load_report"})");
    check(response["status"].asString() == "ok" &&
              response["result"]["status"].asString() == "unobserved" &&
              !response["result"]["observed"].asBool(),
          "no project load observed -> unobserved");

    Aestra::Tests::ScopedTempDirectory temp("project_load_report");
    const std::filesystem::path cleanPath = temp.path() / "clean.aes";
    auto cleanSource = makeTracks();
    check(ProjectSerializer::save(cleanPath.string(), cleanSource, 120.0, 0.0),
          "clean project fixture saved");

    // 2. A current-format project with no loader findings is clean.
    auto cleanTracks = makeTracks();
    auto cleanResult = ProjectSerializer::load(cleanPath.string(), cleanTracks);
    JSON cleanReport = makeMuseProjectLoadReport(cleanResult, MuseProjectLoadOrigin::Canonical);
    check(cleanResult.ok && cleanReport["status"].asString() == "clean",
          "clean fixture -> clean");
    check(cleanReport["format"]["encounteredVersion"].asNumber() ==
              ProjectSerializer::PROJECT_VERSION_CURRENT &&
              cleanReport["format"]["resultingVersion"].asNumber() ==
                  ProjectSerializer::PROJECT_VERSION_CURRENT &&
              cleanReport["warnings"].size() == 0 && cleanReport["errors"].size() == 0,
          "clean report exposes current format and no findings");

    // 3. Recovery of a migrated historical fixture is degraded and explains why.
    const std::filesystem::path fixtureRoot(AESTRA_PROJECT_FIXTURE_DIR);
    const std::filesystem::path migratedPath =
        fixtureRoot / "v2" / "serializer-v2-legacy-audio-split.aes";
    auto migratedTracks = makeTracks();
    auto migratedResult = ProjectSerializer::load(migratedPath.string(), migratedTracks);
    JSON migratedReport =
        makeMuseProjectLoadReport(migratedResult, MuseProjectLoadOrigin::Recovery);
    check(migratedResult.ok && migratedReport["status"].asString() == "degraded",
          "migrated/recovered fixture -> degraded");
    check(migratedReport["format"]["encounteredVersion"].asNumber() == 2.0 &&
              migratedReport["format"]["resultingVersion"].asNumber() ==
                  ProjectSerializer::PROJECT_VERSION_CURRENT &&
              migratedReport["migration"]["state"].asString() == "transformed" &&
              migratedReport["migration"]["schemaVersionAdvanced"].asBool() &&
              migratedReport["recovery"]["state"].asString() == "recovered" &&
              migratedReport["recovery"]["loadOrigin"].asString() == "recovery",
          "migrated/recovered report carries version, migration, and recovery evidence");

    // 4. A failed load is retained as a failed observed attempt.
    auto failedTracks = makeTracks();
    auto failedResult =
        ProjectSerializer::load((temp.path() / "does-not-exist.aes").string(), failedTracks);
    JSON failedReport = makeMuseProjectLoadReport(failedResult, MuseProjectLoadOrigin::Canonical);
    check(!failedResult.ok && failedReport["status"].asString() == "failed" &&
              failedReport["observed"].asBool() &&
              containsIssueCode(failedReport["errors"], "project_load_failed"),
          "failed load -> failed with stable issue code");

    // 5. Missing plugins remain explicit preserved placeholders. The loader only
    // owns a human-readable owner label here, so positional identity must not be invented.
    const std::filesystem::path missingPluginPath =
        fixtureRoot / "v3" / "serializer-v3-independent-mixer.aes";
    auto missingPluginTracks = makeTracks();
    auto missingPluginResult =
        ProjectSerializer::load(missingPluginPath.string(), missingPluginTracks);
    JSON missingPluginReport =
        makeMuseProjectLoadReport(missingPluginResult, MuseProjectLoadOrigin::Canonical);
    check(missingPluginResult.ok && missingPluginReport["status"].asString() == "degraded" &&
              missingPluginReport["missingPlugins"].size() > 0,
          "missing-plugin fixture -> degraded with plugin findings");
    JSON& missingPlugin = missingPluginReport["missingPlugins"][0];
    check(missingPlugin["placeholderPreserved"].asBool() &&
              missingPlugin["issueCode"].asString() == "missing_plugin_placeholder" &&
              !missingPlugin["evidence"]["stableIdentityAvailable"].asBool() &&
              !missingPlugin["evidence"]["positionalIdentityAvailable"].asBool() &&
              missingPlugin["evidence"]["identityKind"].asString() == "owner_label",
          "missing plugin reports placeholder preservation and identity boundary");

    // Durable loader object IDs are emitted as strings so JSON double precision
    // cannot silently alter them.
    ProjectSerializer::LoadResult referencedResult;
    referencedResult.ok = true;
    referencedResult.sourceSchemaVersion = ProjectSerializer::PROJECT_VERSION_CURRENT;
    referencedResult.resultingSchemaVersion = ProjectSerializer::PROJECT_VERSION_CURRENT;
    referencedResult.report = std::make_unique<Aestra::ProjectLoadReport>();
    referencedResult.report->issues.push_back(
        {Aestra::LoadIssueSeverity::Warning, "clip", "missing pattern", 9007199254740993ULL,
         "9007199254740993", ""});
    JSON referencedReport =
        makeMuseProjectLoadReport(referencedResult, MuseProjectLoadOrigin::Canonical);
    check(referencedReport["warnings"][0]["evidence"]["stableIdentityAvailable"].asBool() &&
              !referencedReport["warnings"][0]["evidence"]["positionalIdentityAvailable"].asBool() &&
              referencedReport["warnings"][0]["evidence"]["objectId"].asString() ==
                  "9007199254740993",
          "durable object evidence preserves stable identity exactly");

    // 6. A second load replaces the first report rather than accumulating findings.
    service.setProjectLoadReport(missingPluginReport);
    response = call(service, R"({"id":2,"verb":"get_project_load_report"})");
    check(response["result"]["status"].asString() == "degraded",
          "first observed report is visible");
    service.setProjectLoadReport(cleanReport);
    response = call(service, R"({"id":3,"verb":"get_project_load_report"})");
    check(response["result"]["status"].asString() == "clean" &&
              response["result"]["missingPlugins"].size() == 0,
          "second load replaces the previous report");

    // 7. Creating a new project clears the application-owned snapshot.
    service.clearProjectLoadReport();
    response = call(service, R"({"id":4,"verb":"get_project_load_report"})");
    check(response["result"]["status"].asString() == "unobserved" &&
              !response["result"]["observed"].asBool(),
          "new-project clearing restores unobserved");

    // 8. Discovery includes the exact verb and ignored arguments are rejected.
    JSON schema = JSON::parse(Aestra::Audio::MuseGrammar::schemaToJsonString());
    bool documented = false;
    for (size_t i = 0; i < schema["queries"].size(); ++i) {
        if (schema["queries"][i]["verb"].asString() == "get_project_load_report") {
            documented = schema["queries"][i]["args"].asString() == "none";
        }
    }
    check(documented, "schema discovers get_project_load_report with no arguments");

    response = call(
        service,
        R"({"id":5,"verb":"get_project_load_report","args":{"recover":true}})");
    check(response["status"].asString() == "validation_error",
          "get_project_load_report rejects arguments");

    // 8b. Wide send ids survive the project round trip exactly (Contract D2:
    // ids above 2^53 must not be mangled by JSON doubles).
    {
        Aestra::Tests::ScopedTempDirectory wideTemp("wide_send_id");
        auto wideSource = makeTracks();
        auto* wideChannel = wideSource->addChannelWithId("Wide", 81);
        auto* wideTarget = wideSource->addChannelWithId("WideTarget", 82);
        check(wideChannel != nullptr && wideTarget != nullptr, "wide-id fixture channels created");
        if (wideChannel && wideTarget) {
            Aestra::Audio::AudioRoute wideSend;
            wideSend.targetChannelId = wideTarget->getChannelId(); // master sends are illegal (D4)
            wideSend.sendId = 9007199254740993ull;
            wideChannel->addSend(wideSend);
            const std::filesystem::path widePath = wideTemp.path() / "wide.aes";
            check(ProjectSerializer::save(widePath.string(), wideSource, 120.0, 0.0),
                  "wide-id project saved");
            if (std::filesystem::exists(widePath)) {
                auto wideTracks = makeTracks();
                auto wideResult = ProjectSerializer::load(widePath.string(), wideTracks);
                check(wideResult.ok, "wide-id project loads");
                auto* loadedWide = wideTracks->getChannel(0);
                check(loadedWide != nullptr && loadedWide->getSends().size() == 1 &&
                          loadedWide->getSends()[0].sendId == 9007199254740993ull,
                      "a send id above 2^53 survives the project round trip exactly");
            }
        }
    }

    // 9. Routing repair at load (Contract I8/D3/D4): dangling mains reroute to
    // master; dangling sends and sends to master are removed with diagnostics.
    {
        Aestra::Tests::ScopedTempDirectory repairTemp("routing_repair");
        auto source = makeTracks();
        auto* ch1 = source->addChannelWithId("A", 71);
        auto* ch2 = source->addChannelWithId("B", 72);
        check(ch1 != nullptr && ch2 != nullptr, "routing repair fixture channels created");
        if (ch1 && ch2) {
            ch1->setMainOutputId(72);
            Aestra::Audio::AudioRoute send;
            send.targetChannelId = 72;
            ch1->addSend(send);

            const std::filesystem::path path = repairTemp.path() / "dangling.aes";
            check(ProjectSerializer::save(path.string(), source, 120.0, 0.0),
                  "routing repair fixture saved");
            if (std::filesystem::exists(path)) {
                // Corrupt the saved routing: dangling main, send to master, dangling send.
                std::ifstream in(path);
                std::stringstream buffer;
                buffer << in.rdbuf();
                JSON root = JSON::parse(buffer.str());
                for (size_t ci = 0; ci < root["mixerChannels"].size(); ++ci) {
                    JSON& ch = root["mixerChannels"][ci];
                    if (ch["id"].asNumber() == 71.0) {
                        ch["routing"]["mainOutputId"] = 999999.0;
                        ch["routing"]["sends"][0]["targetId"] = 0.0; // master
                        JSON extra = JSON::object();
                        extra.set("targetId", JSON(999999.0));
                        extra.set("gain", JSON(1.0));
                        extra.set("pan", JSON(0.0));
                        extra.set("postFader", JSON(true));
                        extra.set("mute", JSON(false));
                        extra.set("sidechainOnly", JSON(false));
                        ch["routing"]["sends"].push(extra);
                    }
                }
                std::ofstream out(path);
                out << root.toString();
                out.close();

                auto repairedTracks = makeTracks();
                auto repairedResult = ProjectSerializer::load(path.string(), repairedTracks);
                JSON repairedReport =
                    makeMuseProjectLoadReport(repairedResult, MuseProjectLoadOrigin::Canonical);
                check(repairedResult.ok, "corrupt-routing project still loads");
                auto* repairedA = repairedTracks->getChannel(0);
                check(repairedA != nullptr &&
                          repairedA->getMainOutputId() == 0xFFFFFFFFu,
                      "dangling main output rerouted to master at load");
                check(repairedA != nullptr && repairedA->getSends().empty(),
                      "master send and dangling send removed at load");
                bool routingIssueReported = false;
                for (size_t wi = 0; wi < repairedReport["warnings"].size(); ++wi) {
                    if (repairedReport["warnings"][wi]["issueCode"].asString() == "routing_repaired") {
                        routingIssueReported = true;
                    }
                }
                check(routingIssueReported, "routing repairs are surfaced as diagnostics");
            }
        }
    }

    if (g_failures == 0) {
        std::cout << "All ProjectLoadReport tests passed\n";
        return 0;
    }
    std::cerr << g_failures << " ProjectLoadReport test(s) failed\n";
    return 1;
}
