// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Folio baseline fixture — structural validator.
//
// Loads the canonical fixture through the real ProjectSerializer and proves the
// reopened project still satisfies every claim in CAPABILITIES.json. The
// declaration file is the authority: this tool reads its numbers rather than
// hard-coding them, so weakening the fixture without weakening the declaration
// is a failure, and vice versa.
//
// This runs BEFORE the behavioural probes. A render that disagrees with the
// fixture is only worth debugging once the structure it renders is known good.
//
// Exit code 0 = every check passed. Non-zero = at least one failed; all
// failures are printed, not just the first, so one run tells the whole story.

#include "Core/ProjectSerializer.h"
#include "Models/TrackManager.h"
#include "Plugin/BuiltInPlugins.h"
#include "Plugin/PluginManager.h"
#include "AestraJSON.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace Aestra::Audio;
using Aestra::JSON;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool cond, const std::string& what) {
    ++g_checks;
    if (cond) {
        std::cout << "  [ok]   " << what << "\n";
    } else {
        std::cout << "  [FAIL] " << what << "\n";
        ++g_failures;
    }
}

/// Reports the observed value on failure. A bare "expected X" tells you a check
/// failed; this tells you what the fixture actually contains.
void checkEq(double actual, double expected, double tolerance, const std::string& what) {
    const bool ok = std::fabs(actual - expected) <= tolerance;
    ++g_checks;
    if (ok) {
        std::cout << "  [ok]   " << what << " (" << actual << ")\n";
    } else {
        std::cout << "  [FAIL] " << what << " — expected " << expected << ", got " << actual << "\n";
        ++g_failures;
    }
}

[[noreturn]] void abort(const std::string& msg) {
    std::cerr << "[validate-fixture] ABORT: " << msg << "\n";
    std::exit(2);
}

JSON parseFile(const fs::path& path) {
    std::ifstream in(path);
    if (!in) {
        abort("cannot open " + path.string());
    }
    std::stringstream ss;
    ss << in.rdbuf();
    JSON json = JSON::parse(ss.str());
    if (!json.isObject()) {
        abort("not a JSON object: " + path.string());
    }
    return json;
}

double num(JSON& obj, const char* key, double fallback) {
    return (obj.has(key) && obj[key].isNumber()) ? obj[key].asNumber() : fallback;
}

MixerChannel* channelByName(TrackManager& tm, const std::string& name) {
    for (size_t i = 0; i < tm.getChannelCount(); ++i) {
        auto* channel = tm.getChannel(i);
        if (channel && channel->getName() == name) {
            return channel;
        }
    }
    return nullptr;
}

} // namespace

int main(int argc, char* argv[]) {
    std::string fixtureRoot;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--fixture" && i + 1 < argc) {
            fixtureRoot = argv[++i];
        }
    }
    if (fixtureRoot.empty()) {
        std::cerr << "Usage: validate-fixture --fixture <fixture-root>\n";
        return 2;
    }

    std::error_code ec;
    fs::current_path(fixtureRoot, ec);
    if (ec) {
        abort("cannot chdir to fixture root: " + fixtureRoot);
    }

    JSON caps = parseFile("CAPABILITIES.json");
    JSON project = caps["project"];
    JSON workload = caps["workload"];

    const std::string projectFile = project.has("file") ? project["file"].asString() : "folio-baseline.aes";

    // -----------------------------------------------------------------------
    // Project loads, and is the current format.
    // -----------------------------------------------------------------------
    std::cout << "\n== project ==\n";

    BuiltInPlugins::registerCoreBuiltIns();
    auto& pluginManager = PluginManager::getInstance();
    if (!pluginManager.initialize()) {
        abort("PluginManager failed to initialize");
    }

    auto tm = std::make_shared<TrackManager>();
    auto result = ProjectSerializer::load(projectFile, tm);
    check(result.ok, "project loads successfully" + (result.ok ? "" : " — " + result.errorMessage));
    if (!result.ok) {
        std::cout << "\nload failed; remaining checks cannot be trusted\n";
        return 1;
    }

    // The version lives in the file, not in LoadResult, so it is read directly.
    JSON projectJson = parseFile(projectFile);
    checkEq(num(projectJson, "version", -1), num(project, "requiredVersion", 3), 0.0,
            "project is the current format version");
    checkEq(num(projectJson, "tempo", -1), num(project, "tempoBPM", 120.0), 1e-9, "tempo survives");

    check(result.missingPlugins.empty(),
          "no missing plugins (" + std::to_string(result.missingPlugins.size()) + " reported)");

    // -----------------------------------------------------------------------
    // Assets resolve — declared on disk AND decoded into a real buffer.
    // -----------------------------------------------------------------------
    std::cout << "\n== assets ==\n";
    auto& sources = tm->getSourceManager();
    JSON assets = caps["assets"];
    for (size_t i = 0; i < assets.size(); ++i) {
        JSON asset = assets[i];
        const std::string path = asset["path"].asString();
        const auto sourceId = static_cast<uint64_t>(num(asset, "sourceId", 0));

        check(fs::is_regular_file(path), "asset present on disk: " + path);

        auto* source = sources.getSource(ClipSourceID{sourceId});
        check(source != nullptr, "source id " + std::to_string(sourceId) + " survives reopen (" + path + ")");
        if (!source) {
            continue;
        }
        check(fs::path(source->getFilePath()).is_relative(),
              "source path stays relative (relocatable): " + source->getFilePath());

        auto buffer = source->getSharedBuffer();
        const bool decoded = buffer && buffer->isValid() && buffer->numFrames > 0;
        check(decoded, "asset decodes to a real buffer: " + path);
        if (decoded) {
            checkEq(static_cast<double>(buffer->numFrames), num(asset, "frames", -1), 0.0,
                    "frame count matches declaration: " + path);
            checkEq(static_cast<double>(buffer->sampleRate), num(asset, "sampleRate", -1), 0.0,
                    "sample rate matches declaration: " + path);
            checkEq(static_cast<double>(buffer->numChannels), num(asset, "channels", -1), 0.0,
                    "channel count matches declaration: " + path);
        }
    }

    // -----------------------------------------------------------------------
    // Topology: lane/channel counts, and everything audible.
    // -----------------------------------------------------------------------
    std::cout << "\n== topology ==\n";
    auto& playlist = tm->getPlaylistModel();

    checkEq(static_cast<double>(playlist.getLaneCount()), num(workload, "laneCount", 8), 0.0, "lane count");
    checkEq(static_cast<double>(tm->getChannelCount()), num(workload, "mixerChannelCount", 9), 0.0,
            "mixer channel count");
    check(playlist.getLaneCount() >= static_cast<size_t>(num(workload, "minActiveLanes", 6)),
          "at least the declared minimum number of lanes");

    int lanesWithClips = 0;
    int mutedLanes = 0;
    int mutedClips = 0;
    int audioClips = 0;
    int midiClips = 0;
    int slicedClips = 0;
    auto& patternManager = tm->getPatternManager();

    for (const auto& laneId : playlist.getLaneIDs()) {
        auto* lane = playlist.getLane(laneId);
        if (!lane) {
            continue;
        }
        if (lane->muted) {
            ++mutedLanes;
        }
        if (!lane->clips.empty()) {
            ++lanesWithClips;
        }
        for (const auto& clip : lane->clips) {
            if (clip.edits.muted) {
                ++mutedClips;
            }
            const auto* pattern = patternManager.getPattern(clip.patternId);
            if (pattern && pattern->isAudio()) {
                ++audioClips;
                if (clip.sourceOffsetSeconds > 0.0) {
                    ++slicedClips;
                }
            } else if (pattern && pattern->isMidi()) {
                ++midiClips;
            }
        }
    }

    check(lanesWithClips >= static_cast<int>(num(workload, "minActiveLanes", 6)),
          "at least " + std::to_string(static_cast<int>(num(workload, "minActiveLanes", 6))) +
              " lanes carry clips (" + std::to_string(lanesWithClips) + ")");
    checkEq(mutedLanes, 0, 0.0, "no muted lanes");
    checkEq(mutedClips, 0, 0.0, "no muted clips");
    check(audioClips >= static_cast<int>(num(workload, "minAudioClips", 500)),
          "audio clip count meets declaration (" + std::to_string(audioClips) + ")");
    check(midiClips >= static_cast<int>(num(workload, "minMidiClips", 100)),
          "midi clip count meets declaration (" + std::to_string(midiClips) + ")");

    int mutedChannels = 0;
    for (size_t i = 0; i < tm->getChannelCount(); ++i) {
        if (auto* channel = tm->getChannel(i); channel && channel->isMuted()) {
            ++mutedChannels;
        }
    }
    checkEq(mutedChannels, 0, 0.0, "no muted mixer channels");

    const double totalBeats = playlist.getTotalDurationBeats();
    const double totalSeconds = totalBeats * 60.0 / std::max(playlist.getBPM(), 1.0);
    check(totalSeconds >= num(workload, "minArrangementSeconds", 120.0),
          "arrangement covers at least the declared minimum (" + std::to_string(totalSeconds) + " s)");

    // -----------------------------------------------------------------------
    // Slice bounds: present, valid, and nontrivial in both directions.
    // -----------------------------------------------------------------------
    std::cout << "\n== slice ==\n";
    JSON sliceSpec = caps["slice"];
    const double sliceOffset = num(sliceSpec, "sourceOffsetSeconds", 0.5);
    const double sliceDuration = num(sliceSpec, "durationSeconds", 0.75);
    const double sliceSourceLen = num(sliceSpec, "sourceDurationSeconds", 1.5);

    check(slicedClips >= static_cast<int>(num(workload, "minSlicedClips", 1)),
          "at least one sliced clip survives (" + std::to_string(slicedClips) + ")");
    check(sliceOffset > 0.0, "slice starts after the source start (nontrivial head)");
    check(sliceOffset + sliceDuration < sliceSourceLen, "slice ends before the source end (nontrivial tail)");

    bool foundDeclaredSlice = false;
    for (const auto& laneId : playlist.getLaneIDs()) {
        auto* lane = playlist.getLane(laneId);
        if (!lane || lane->name != sliceSpec["lane"].asString()) {
            continue;
        }
        for (const auto& clip : lane->clips) {
            if (std::fabs(clip.sourceOffsetSeconds - sliceOffset) < 1e-9 &&
                std::fabs(clip.durationSeconds - sliceDuration) < 1e-9) {
                foundDeclaredSlice = true;
            }
        }
    }
    check(foundDeclaredSlice, "the declared slice window survives byte-for-byte on its lane");

    // -----------------------------------------------------------------------
    // Automation.
    // -----------------------------------------------------------------------
    std::cout << "\n== automation ==\n";
    JSON autoSpec = caps["automation"];
    const std::string autoLaneName = autoSpec["lane"].asString();
    auto* autoChannel = channelByName(*tm, autoSpec["channel"].asString());
    check(autoChannel != nullptr, "automation target channel exists: " + autoSpec["channel"].asString());

    const AutomationCurve* curve = nullptr;
    for (const auto& laneId : playlist.getLaneIDs()) {
        auto* lane = playlist.getLane(laneId);
        if (lane && lane->name == autoLaneName && !lane->automationCurves.empty()) {
            curve = &lane->automationCurves.front();
        }
    }
    check(curve != nullptr, "automation curve survives on lane: " + autoLaneName);

    if (curve && autoChannel) {
        check(curve->target == AutomationTarget::Volume, "curve target is Volume");
        checkEq(static_cast<double>(curve->mixerChannelId), static_cast<double>(autoChannel->getChannelId()), 0.0,
                "mixerChannelId survives save/reopen and points at the intended channel");

        JSON points = autoSpec["points"];
        checkEq(static_cast<double>(curve->points.size()), static_cast<double>(points.size()), 0.0,
                "automation point count");

        std::set<double> distinctBeats;
        std::set<double> distinctValues;
        bool everyPointMatches = true;
        bool neverSilent = true;
        const double minValue = num(autoSpec, "minValue", 0.6);

        for (size_t i = 0; i < curve->points.size() && i < points.size(); ++i) {
            JSON expected = points[i];
            const auto& actual = curve->points[i];
            distinctBeats.insert(actual.beat);
            distinctValues.insert(actual.value);
            if (std::fabs(actual.beat - num(expected, "beat", -1)) > 1e-6 ||
                std::fabs(actual.value - num(expected, "value", -1)) > 1e-6) {
                everyPointMatches = false;
            }
            if (actual.value < minValue) {
                neverSilent = false;
            }
        }
        check(everyPointMatches, "every automation point round-trips at its declared beat and value");
        check(distinctBeats.size() == curve->points.size(), "all automation beats are distinct");
        check(distinctValues.size() > 1, "automation contains distinct values (curve is not flat)");
        check(neverSilent, "automation never silences the lane");
    }

    // -----------------------------------------------------------------------
    // Send.
    // -----------------------------------------------------------------------
    std::cout << "\n== send ==\n";
    JSON sendSpec = caps["send"];
    auto* sendFrom = channelByName(*tm, sendSpec["fromChannel"].asString());
    auto* sendTo = channelByName(*tm, sendSpec["toChannel"].asString());
    check(sendFrom != nullptr, "send source channel exists: " + sendSpec["fromChannel"].asString());
    check(sendTo != nullptr, "return channel exists: " + sendSpec["toChannel"].asString());

    if (sendFrom && sendTo) {
        const auto sends = sendFrom->getSends();
        // Exactly one. Two would mean the loader applied both the
        // mixerChannels[] block and the legacy lanes[] block, doubling the gain.
        checkEq(static_cast<double>(sends.size()), num(sendSpec, "expectedSendCount", 1), 0.0,
                "send is applied exactly once (no duplicate from the legacy lane block)");

        if (!sends.empty()) {
            const auto& route = sends.front();
            checkEq(static_cast<double>(route.targetChannelId), static_cast<double>(sendTo->getChannelId()), 0.0,
                    "send destination survives and points at the return channel");
            checkEq(route.gain, num(sendSpec, "gain", 0.35), 1e-6, "send gain survives");
            checkEq(route.pan, num(sendSpec, "pan", 0.0), 1e-6, "send pan survives");
            check(route.postFader, "send is post-fader");
            check(!route.mute, "send is not muted (audible)");
            check(!route.sidechainOnly, "send is audible, not sidechain-only");
        }

        check(sendFrom->getMainOutputId() == 0xFFFFFFFFu, "send source keeps its direct output to Master");
        check(sendTo->getMainOutputId() == 0xFFFFFFFFu, "return channel routes to Master");
        check(!sendTo->isMuted(), "return channel is unmuted");
    }

    // -----------------------------------------------------------------------
    // Effects.
    // -----------------------------------------------------------------------
    std::cout << "\n== effects ==\n";
    JSON fxSpec = caps["effects"];
    auto* fxChannel = channelByName(*tm, fxSpec["channel"].asString());
    check(fxChannel != nullptr, "effect channel exists: " + fxSpec["channel"].asString());

    if (fxChannel) {
        auto& chain = fxChannel->getEffectChain();
        JSON slots = fxSpec["chain"];
        for (size_t i = 0; i < slots.size(); ++i) {
            JSON slotSpec = slots[i];
            const auto slotIndex = static_cast<size_t>(num(slotSpec, "slot", static_cast<double>(i)));
            const std::string expectedId = slotSpec["pluginId"].asString();

            auto plugin = chain.getPlugin(slotIndex);
            check(plugin != nullptr, "slot " + std::to_string(slotIndex) + " holds a live plugin");
            if (!plugin) {
                continue;
            }
            check(plugin->getInfo().id == expectedId,
                  "slot " + std::to_string(slotIndex) + " is " + expectedId + " (got " + plugin->getInfo().id + ")");
            check(!chain.isSlotBypassed(slotIndex), "slot " + std::to_string(slotIndex) + " is not bypassed");

            // Non-default parameters: proves the chain carries a deliberate
            // setting, not a freshly-constructed plugin at its defaults.
            JSON nonDefaults = slotSpec["nonDefaultParameters"];
            for (auto& [key, entryConst] : nonDefaults.asObject()) {
                JSON entry = entryConst;
                const auto paramId = static_cast<uint32_t>(std::stoul(key));
                const double expected = num(entry, "value", -1);
                const double defaultValue = num(entry, "default", -1);
                const double actual = plugin->getParameter(paramId);
                checkEq(actual, expected, 1e-5,
                        expectedId + " " + entry["name"].asString() + " survives");
                check(std::fabs(actual - defaultValue) > 1e-6,
                      expectedId + " " + entry["name"].asString() + " is genuinely non-default");
            }

            JSON neutral = slotSpec["neutralParameters"];
            for (auto& [key, entryConst] : neutral.asObject()) {
                JSON entry = entryConst;
                const auto paramId = static_cast<uint32_t>(std::stoul(key));
                checkEq(plugin->getParameter(paramId), num(entry, "value", -1), 1e-5,
                        expectedId + " " + entry["name"].asString() + " stays neutral");
            }
        }
    }

    // -----------------------------------------------------------------------
    std::cout << "\n== summary ==\n"
              << "  checks: " << g_checks << ", failures: " << g_failures << "\n";
    if (g_failures == 0) {
        std::cout << "[validate-fixture] PASS\n";
        return 0;
    }
    std::cout << "[validate-fixture] FAIL\n";
    return 1;
}
