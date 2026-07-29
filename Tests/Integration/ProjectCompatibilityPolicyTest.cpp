// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Executable project-format policy: schema coverage, migration completeness,
// observable outcomes, historical fixtures, and non-destructive rejection.

#include "../../Source/Core/ProjectSerializer.h"
#include "../Support/TestTempDirectory.h"
#include "Models/PatternSource.h"
#include "Models/TrackManager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace Aestra;
using namespace Aestra::Audio;

int g_failures = 0;

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        ++g_failures;
    }
}

bool writeText(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << contents;
    return static_cast<bool>(out);
}

std::shared_ptr<TrackManager> makeTracks() {
    auto tracks = std::make_shared<TrackManager>();
    tracks->getPlaylistModel().setPatternManager(&tracks->getPatternManager());
    return tracks;
}

int readFixtureVersion(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    bool consumedAll = false;
    const JSON root = JSON::parseStrict(contents, consumedAll);
    if (!consumedAll || !root.isObject() || !root.has("version") || !root["version"].isNumber()) {
        return 0;
    }
    return static_cast<int>(root["version"].asNumber());
}

std::vector<std::string> laneIds(const TrackManager& tracks) {
    std::vector<std::string> ids;
    for (const auto& id : tracks.getPlaylistModel().getLaneIDs()) {
        ids.push_back(id.toString());
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<uint64_t> patternIds(TrackManager& tracks) {
    std::vector<uint64_t> ids;
    for (const auto& pattern : tracks.getPatternManager().getAllPatterns()) {
        if (pattern) {
            ids.push_back(pattern->id.value);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<std::string> clipIds(TrackManager& tracks) {
    std::vector<std::string> ids;
    auto& playlist = tracks.getPlaylistModel();
    for (const auto& laneId : playlist.getLaneIDs()) {
        if (const auto* lane = playlist.getLane(laneId)) {
            for (const auto& clip : lane->clips) {
                ids.push_back(clip.id.toString());
            }
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<uint32_t> mixerChannelIds(const TrackManager& tracks) {
    std::vector<uint32_t> ids;
    for (size_t index = 0; index < tracks.getChannelCount(); ++index) {
        if (const auto* channel = tracks.getChannel(index)) {
            ids.push_back(channel->getChannelId());
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::string bytesHex(const std::vector<uint8_t>& bytes) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (uint8_t byte : bytes) {
        out << std::setw(2) << static_cast<unsigned>(byte);
    }
    return out.str();
}

MigrationStepResult unchanged(JSON&) {
    return MigrationStepResult::Unchanged;
}

// Everything about a fixture file that loading it must leave alone. Bytes are
// the substantive claim; last-write time and size are metadata a rewrite would
// disturb even if the new contents happened to be identical.
struct FileSnapshot {
    bool exists{false};
    std::string bytes;
    std::uintmax_t size{0};
    std::filesystem::file_time_type lastWrite{};

    bool operator==(const FileSnapshot& other) const {
        return exists == other.exists && size == other.size && lastWrite == other.lastWrite && bytes == other.bytes;
    }
};

FileSnapshot snapshotFile(const std::filesystem::path& path) {
    FileSnapshot snapshot;
    std::error_code ec;
    snapshot.exists = std::filesystem::exists(path, ec);
    if (!snapshot.exists) {
        return snapshot;
    }
    snapshot.size = std::filesystem::file_size(path, ec);
    snapshot.lastWrite = std::filesystem::last_write_time(path, ec);
    std::ifstream in(path, std::ios::binary);
    snapshot.bytes.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return snapshot;
}

// Constitution rule 3: migration runs on an in-memory parse and never
// overwrites the original project. Asserted directly rather than inferred from
// "load does not call save" — a future asset-relink, integrity-repair or
// index-rebuild path could touch the file without going through save at all,
// and the user's only original copy is what is at stake.
void testLoadNeverModifiesTheOriginal() {
    const std::vector<const char*> fixtures = {
        "v1_minimal.aes",
        "v1_rich.aes",
        "v1_rich_assets/kick.wav",
        "v2/serializer-v2-identity.aes",
        "v2/serializer-v2-positional-mixer.aes",
        "v3/serializer-v3-independent-mixer.aes",
    };

    for (const char* relative : fixtures) {
        const auto path = std::filesystem::path(AESTRA_PROJECT_FIXTURE_DIR) / relative;
        const FileSnapshot before = snapshotFile(path);
        require(before.exists, std::string("immutability probe: fixture missing: ") + relative);
        if (!before.exists) {
            continue;
        }

        // The referenced asset is not itself loadable as a project; snapshotting
        // it still proves a project load does not rewrite its dependencies.
        if (std::filesystem::path(relative).extension() == ".aes") {
            auto tracks = makeTracks();
            const auto result = ProjectSerializer::load(path.string(), tracks);
            require(result.ok, std::string("immutability probe: load failed: ") + relative + " " + result.errorMessage);
        }

        const FileSnapshot after = snapshotFile(path);
        require(after == before,
                std::string("loading modified the original fixture on disk: ") + relative +
                    " (bytes " + (after.bytes == before.bytes ? "same" : "CHANGED") + ", size " +
                    std::to_string(before.size) + " -> " + std::to_string(after.size) + ", mtime " +
                    (after.lastWrite == before.lastWrite ? "same" : "CHANGED") + ")");
    }
}

// Constitution rule 7: PROJECT_VERSION_MIN_SUPPORTED is a product promise, not
// a cleanup knob.
//
// Deliberately NOT a second copy of the constant — a mirrored value would be a
// competing version authority that drifts. This is a one-directional tripwire:
// it pins only the OLDEST schema Aestra ever released, so bumping
// PROJECT_VERSION_CURRENT needs no edit here, while narrowing compatibility
// fails and forces an explicit compatibility-policy decision in the same PR.
void testMinimumSupportedVersionIsAProductPromise() {
    constexpr int kOldestReleasedSchemaVersion = 1;
    require(ProjectSerializer::PROJECT_VERSION_MIN_SUPPORTED <= kOldestReleasedSchemaVersion,
            "PROJECT_VERSION_MIN_SUPPORTED was raised to " +
                std::to_string(ProjectSerializer::PROJECT_VERSION_MIN_SUPPORTED) +
                ", dropping support for schema v" + std::to_string(kOldestReleasedSchemaVersion) +
                ". That is a product decision requiring a release plan (compatibility constitution rule 7), "
                "not a convenience edit — update this test only alongside that decision.");
}

void testMigrationRegistry() {
    const auto& migrations = ProjectMigrations::getMigrations();
    std::string error;
    require(ProjectMigrations::validateRegistry(migrations, ProjectSerializer::PROJECT_VERSION_MIN_SUPPORTED,
                                                ProjectSerializer::PROJECT_VERSION_CURRENT, &error),
            "production migration registry incomplete: " + error);

    for (const auto& edge : migrations) {
        JSON root = JSON::object();
        root.set("version", JSON(static_cast<double>(edge.fromVersion)));
        const auto result =
            ProjectMigrations::runMigrationsWith(root, edge.fromVersion, edge.toVersion, std::vector<Migration>{edge});
        require(result.ok(), "registered migration edge failed");
        require(result.resultingVersion == edge.toVersion, "migration did not report its declared target");
        require(root["version"].asNumber() == static_cast<double>(edge.toVersion),
                "migration did not stamp its declared target");
    }

    std::vector<Migration> missing = {{1, 2, unchanged, "first edge only"}};
    JSON missingRoot = JSON::object();
    missingRoot.set("version", JSON(1.0));
    const auto missingResult = ProjectMigrations::runMigrationsWith(missingRoot, 1, 3, missing);
    require(!missingResult.ok() && missingResult.outcome == MigrationOutcome::Failed,
            "missing migration edge must fail loudly");

    std::vector<Migration> duplicate = {
        {1, 2, unchanged, "duplicate a"},
        {1, 2, unchanged, "duplicate b"},
        {2, 3, unchanged, "second edge"},
    };
    require(!ProjectMigrations::validateRegistry(duplicate, 1, 3, &error), "duplicate migration edge was accepted");

    std::vector<Migration> skipped = {{1, 3, unchanged, "skipped edge"}};
    require(!ProjectMigrations::validateRegistry(skipped, 1, 3, &error),
            "version-skipping migration edge was accepted");

    std::vector<Migration> transformed = {
        {1, 2,
         [](JSON& root) {
             root.set("renamedField", JSON("migrated"));
             return MigrationStepResult::Transformed;
         },
         "test-only real transformation"},
    };
    JSON transformedRoot = JSON::object();
    transformedRoot.set("version", JSON(1.0));
    const auto transformedResult = ProjectMigrations::runMigrationsWith(transformedRoot, 1, 2, transformed);
    require(transformedResult.ok() && transformedResult.outcome == MigrationOutcome::Transformed,
            "real transformation was not observable");

    ProjectSerializer::LoadResult lifecycleResult;
    lifecycleResult.migrationOutcome = transformedResult.outcome;
    require(lifecycleResult.requiresSaveAfterLoad(),
            "transformed load must require saving the upgraded representation");
    lifecycleResult.migrationOutcome = MigrationOutcome::None;
    require(!lifecycleResult.requiresSaveAfterLoad(), "plain/no-op load must not require an unsolicited save");

    std::vector<Migration> failed = {
        {1, 2, [](JSON&) { return MigrationStepResult::Failed; }, "test-only failure"},
    };
    JSON failedRoot = JSON::object();
    failedRoot.set("version", JSON(1.0));
    require(ProjectMigrations::runMigrationsWith(failedRoot, 1, 2, failed).outcome == MigrationOutcome::Failed,
            "failed migration outcome was not observable");
}

void testUnsupportedVersionsAreNonDestructive(const std::filesystem::path& tempDir) {
    auto tracks = makeTracks();
    tracks->getPlaylistModel().createLane("Existing Session");
    tracks->addChannel("Existing Session");
    const auto beforeLaneIds = laneIds(*tracks);
    const size_t beforeChannels = tracks->getChannelCount();

    for (int version : {0, ProjectSerializer::PROJECT_VERSION_CURRENT + 1}) {
        const auto path = tempDir / ("unsupported-" + std::to_string(version) + ".aes");
        require(writeText(path, "{\"version\":" + std::to_string(version) +
                                    ",\"tempo\":120,\"playhead\":0,\"sources\":[],\"patterns\":[],\"lanes\":[],"
                                    "\"arsenal\":{\"nextId\":1,\"units\":[]}}"),
                "could not write unsupported-version probe");
        const auto result = ProjectSerializer::load(path.string(), tracks);
        require(!result.ok, "unsupported version loaded");
        require(laneIds(*tracks) == beforeLaneIds && tracks->getChannelCount() == beforeChannels,
                "unsupported version mutated the existing in-memory session");
    }
}

void assertV2Semantics(TrackManager& tracks, const std::string& generation) {
    const auto ids = tracks.getPlaylistModel().getLaneIDs();
    require(ids.size() == 1, generation + ": v2 lane count");
    const auto* lane = ids.empty() ? nullptr : tracks.getPlaylistModel().getLane(ids.front());
    require(lane != nullptr && lane->name == "V2 Identity Lane", generation + ": v2 lane name");
    if (lane) {
        require(lane->clips.size() == 1, generation + ": v2 clip count");
        require(lane->automationCurves.size() == 1, generation + ": v2 automation count");
        if (!lane->automationCurves.empty()) {
            require(lane->automationCurves[0].effectSlot == 3 && lane->automationCurves[0].paramId == 17,
                    generation + ": v2 plugin automation address");
        }
    }
    require(tracks.getChannelCount() == 1, generation + ": v2 channel count");
}

// The v2 -> v3 change is topological: v2 pairs a lane with a mixer channel
// POSITIONALLY and writes the channel's state into the lane object; v3 persists
// lanes and channels as independent records with stable identities.
//
// serializer-v2-identity.aes has one lane and one channel, so it proves the
// traversal works but cannot distinguish a correct pairing from a wrong one.
// This fixture has three lanes, two of which SHARE A NAME, with every value
// distinct per position. A loader that pairs by name merges the first two; one
// that loses order swaps them; one that merges channels collapses the count.
// Each of those changes at least one assertion below.
void assertV2PositionalMixerSemantics(TrackManager& tracks, const std::string& generation) {
    auto& playlist = tracks.getPlaylistModel();
    const auto ids = playlist.getLaneIDs();

    require(ids.size() == 3, generation + ": v2 positional lane count (got " + std::to_string(ids.size()) + ")");
    require(tracks.getChannelCount() == 3,
            generation + ": v2 positional channel count (got " + std::to_string(tracks.getChannelCount()) +
                ") — a name-keyed pairing would merge the two same-named lanes");
    if (ids.size() != 3 || tracks.getChannelCount() != 3) {
        return;
    }

    struct Expected {
        const char* name;
        float volume;
        float pan;
        bool muted;
        bool solo;
        float width;
        int colorIndex;
        int inputIndex;
        bool soloSafe;
        bool armed;
    };
    // Ordered by position, matching generation order.
    const Expected expected[3] = {
        {"Shared Name",   0.40f, -0.75f, true,  false, 0.50f, 1, 2, true,  false},
        {"Shared Name",   0.90f,  0.50f, false, true,  1.50f, 2, 5, false, true},
        {"Distinct Lane", 0.65f,  0.25f, false, false, 1.00f, 3, 7, false, false},
    };

    for (size_t i = 0; i < 3; ++i) {
        const std::string at = generation + ": lane " + std::to_string(i);
        const auto* lane = playlist.getLane(ids[i]);
        require(lane != nullptr, at + " missing");
        if (lane) {
            require(lane->name == expected[i].name, at + " name (got '" + lane->name + "')");
            require(lane->volume == expected[i].volume, at + " volume");
            require(lane->pan == expected[i].pan, at + " pan");
            require(lane->muted == expected[i].muted, at + " mute");
            require(lane->solo == expected[i].solo, at + " solo");
        }

        const auto* channel = tracks.getChannel(i);
        require(channel != nullptr, at + " channel missing");
        if (channel) {
            require(channel->getWidth() == expected[i].width, at + " channel width");
            require(channel->getTrackColorIndex() == expected[i].colorIndex, at + " channel color index");
            require(channel->getInputChannelIndex() == expected[i].inputIndex, at + " channel input index");
            require(channel->isSoloSafe() == expected[i].soloSafe, at + " channel solo-safe");
            require(channel->isArmed() == expected[i].armed, at + " channel armed");
        }
    }

    // Routing is a separate concern from level state: it must survive the
    // topology change with its target still resolving to the intended channel.
    const auto* first = tracks.getChannel(0);
    const auto* second = tracks.getChannel(1);
    const auto* third = tracks.getChannel(2);
    if (first && second && third) {
        const auto sends = first->getSends();
        require(sends.size() == 1, generation + ": channel 0 send count (got " + std::to_string(sends.size()) + ")");
        if (sends.size() == 1) {
            require(sends[0].targetChannelId == third->getChannelId(),
                    generation + ": channel 0 send target must still resolve to channel 2");
            require(sends[0].gain == 0.625f, generation + ": channel 0 send gain");
            require(sends[0].pan == -0.25f, generation + ": channel 0 send pan");
            require(!sends[0].postFader, generation + ": channel 0 send pre-fader tap");
        }
        require(second->getMainOutputId() == third->getChannelId(),
                generation + ": channel 1 non-default main output");
        require(first->getChannelId() != second->getChannelId() &&
                    second->getChannelId() != third->getChannelId() &&
                    first->getChannelId() != third->getChannelId(),
                generation + ": v3 channel identities must be distinct");
    }
}

void assertV3Semantics(TrackManager& tracks, const ProjectSerializer::LoadResult& result,
                       const std::string& generation) {
    require(tracks.getChannelCount() == 2, generation + ": v3 independent channel count");
    require(tracks.getPlaylistModel().getLaneCount() == 1, generation + ": v3 lane/channel independence");
    require(mixerChannelIds(tracks) == std::vector<uint32_t>({41, 77}),
            generation + ": v3 stable mixer channel identities");
    require(result.missingPlugins.size() == 1, generation + ": missing plugin report");
    if (tracks.getChannelCount() >= 1) {
        const auto* slot = tracks.getChannel(0)->getEffectChain().getSlot(3);
        require(slot && slot->hasMissingPlugin() && slot->isOccupied(),
                generation + ": missing plugin placeholder occupancy");
        require(slot && slot->missingPluginId == "com.aestra.fixture.missing-effect",
                generation + ": missing plugin id");
        require(slot && slot->bypassed.load(), generation + ": missing plugin bypass");
        require(slot && slot->dryWetMix.load() == 0.375f, generation + ": missing plugin dry/wet");
        const std::vector<uint8_t> expectedState{0x10, 0x20, 0x30, 0x40, 0x00, 0xFF};
        require(slot && slot->missingPluginState == expectedState,
                generation + ": missing plugin opaque state (expected " + bytesHex(expectedState) + ", got " +
                    (slot ? bytesHex(slot->missingPluginState) : std::string("<no slot>")) + ")");
    }
}

void testFixtureCorpus(const std::filesystem::path& tempDir) {
    // `semantics` selects the per-fixture assertions: two fixtures can share a
    // schema version while pinning different things about it, so the version
    // number alone cannot dispatch.
    enum class Semantics { Structural, V2Identity, V2PositionalMixer, V3IndependentMixer };
    struct Fixture {
        int version;
        const char* relativePath;
        Semantics semantics;
    };
    const std::vector<Fixture> fixtures = {
        {1, "v1_minimal.aes", Semantics::Structural},
        {1, "v1_rich.aes", Semantics::Structural},
        {2, "v2/serializer-v2-identity.aes", Semantics::V2Identity},
        {2, "v2/serializer-v2-positional-mixer.aes", Semantics::V2PositionalMixer},
        {3, "v3/serializer-v3-independent-mixer.aes", Semantics::V3IndependentMixer},
    };

    for (int version = ProjectSerializer::PROJECT_VERSION_MIN_SUPPORTED;
         version <= ProjectSerializer::PROJECT_VERSION_CURRENT; ++version) {
        require(std::any_of(fixtures.begin(), fixtures.end(),
                            [version](const Fixture& fixture) { return fixture.version == version; }),
                "supported schema v" + std::to_string(version) + " has no fixture");
    }

    for (const auto& fixture : fixtures) {
        const auto path = std::filesystem::path(AESTRA_PROJECT_FIXTURE_DIR) / fixture.relativePath;
        require(std::filesystem::exists(path), std::string("fixture missing: ") + fixture.relativePath);
        if (!std::filesystem::exists(path)) {
            continue;
        }
        require(readFixtureVersion(path) == fixture.version,
                std::string("fixture version mismatch: ") + fixture.relativePath);

        auto firstTracks = makeTracks();
        const auto first = ProjectSerializer::load(path.string(), firstTracks);
        require(first.ok, std::string("fixture load failed: ") + fixture.relativePath + " " + first.errorMessage);
        if (!first.ok) {
            continue;
        }
        require(first.sourceSchemaVersion == fixture.version, "source schema metadata mismatch");
        require(first.resultingSchemaVersion == ProjectSerializer::PROJECT_VERSION_CURRENT,
                "resulting schema metadata mismatch");
        require(first.migrationOutcome == MigrationOutcome::None,
                "existing no-op fixture migration must not report a transformation");
        require(!first.requiresSaveAfterLoad(), "no-op/current fixture load must remain clean");

        const auto firstLaneIds = laneIds(*firstTracks);
        const auto firstPatternIds = patternIds(*firstTracks);
        const auto firstClipIds = clipIds(*firstTracks);
        const auto firstMixerChannelIds = mixerChannelIds(*firstTracks);
        const std::string label = std::string(fixture.relativePath) + " first load";
        switch (fixture.semantics) {
            case Semantics::V2Identity: assertV2Semantics(*firstTracks, label); break;
            case Semantics::V2PositionalMixer: assertV2PositionalMixerSemantics(*firstTracks, label); break;
            case Semantics::V3IndependentMixer: assertV3Semantics(*firstTracks, first, label); break;
            case Semantics::Structural: break;
        }

        const auto serialized = ProjectSerializer::serialize(firstTracks, first.tempo, first.playhead, 2);
        require(serialized.ok, std::string("fixture re-serialize failed: ") + fixture.relativePath);
        if (!serialized.ok) {
            continue;
        }
        const auto resavedPath = tempDir / ("resaved-v" + std::to_string(fixture.version) + "-" +
                                            std::filesystem::path(fixture.relativePath).filename().string());
        require(writeText(resavedPath, serialized.contents), "could not write fixture re-save");
        require(readFixtureVersion(resavedPath) == ProjectSerializer::PROJECT_VERSION_CURRENT,
                "fixture re-save did not emit current schema");

        auto secondTracks = makeTracks();
        const auto second = ProjectSerializer::load(resavedPath.string(), secondTracks);
        require(second.ok, std::string("fixture re-save reload failed: ") + fixture.relativePath);
        if (!second.ok) {
            continue;
        }
        require(laneIds(*secondTracks) == firstLaneIds, "lane identities changed across fixture re-save");
        require(patternIds(*secondTracks) == firstPatternIds, "pattern identities changed across fixture re-save");
        require(clipIds(*secondTracks) == firstClipIds, "clip identities changed across fixture re-save");
        require(mixerChannelIds(*secondTracks) == firstMixerChannelIds,
                "mixer channel identities changed across fixture re-save");
        // Save -> load stability: the same assertions must hold after the
        // document has been re-emitted as v3 and read back.
        const std::string secondLabel = std::string(fixture.relativePath) + " second load";
        switch (fixture.semantics) {
            case Semantics::V2Identity: assertV2Semantics(*secondTracks, secondLabel); break;
            case Semantics::V2PositionalMixer: assertV2PositionalMixerSemantics(*secondTracks, secondLabel); break;
            case Semantics::V3IndependentMixer: assertV3Semantics(*secondTracks, second, secondLabel); break;
            case Semantics::Structural: break;
        }
    }
}

void testCurrentLoadMetadata(const std::filesystem::path& tempDir) {
    auto source = makeTracks();
    source->getPlaylistModel().createLane("Current");
    source->addChannel("Current");
    const auto serialized = ProjectSerializer::serialize(source, 120.0, 0.0, 0);
    const auto path = tempDir / "current.aes";
    require(serialized.ok && writeText(path, serialized.contents), "current metadata probe write failed");

    auto destination = makeTracks();
    const auto result = ProjectSerializer::load(path.string(), destination);
    require(result.ok, "current metadata probe load failed");
    require(result.sourceSchemaVersion == ProjectSerializer::PROJECT_VERSION_CURRENT,
            "current load source version metadata");
    require(result.resultingSchemaVersion == ProjectSerializer::PROJECT_VERSION_CURRENT,
            "current load resulting version metadata");
    require(result.migrationOutcome == MigrationOutcome::None && !result.requiresSaveAfterLoad(),
            "plain current load must remain clean");
}

} // namespace

int main() {
    const Aestra::Tests::ScopedTempDirectory tempDir{"ProjectCompatibilityPolicy"};
    testMigrationRegistry();
    testMinimumSupportedVersionIsAProductPromise();
    testUnsupportedVersionsAreNonDestructive(tempDir.path());
    testCurrentLoadMetadata(tempDir.path());
    testFixtureCorpus(tempDir.path());
    testLoadNeverModifiesTheOriginal();

    if (g_failures != 0) {
        std::cerr << "[FAIL] ProjectCompatibilityPolicyTest: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "[PASS] ProjectCompatibilityPolicyTest\n";
    return 0;
}
