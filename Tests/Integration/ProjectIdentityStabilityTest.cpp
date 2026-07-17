// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// ProjectIdentityStabilityTest (#446) — serialized identities must survive
// load, and save→load→save must be a byte-identical fixed point.
//
// Before this fix the loader re-minted clip UUIDs, pattern IDs, lane IDs and
// source IDs on every load (remapping references so semantics survived but
// identity didn't), and the writer emitted patterns/sources in unordered_map
// order. Consequences: no whole-file fixed-point regression gate, and no
// stable identity for Takes across sessions, diffing, or external references.
//
// Asserted here:
//   - clip / pattern / lane / source IDs are identical after a load cycle
//   - save→load→save produces byte-identical project contents (fixed point),
//     and stays identical over a further generation (idempotence)
//   - IDs minted AFTER a load never collide with restored IDs
//   - AestraUUID::tryParse round-trips toString() and rejects garbage
//     (invalid/absent ids keep minting, so legacy files still load)
//   - duplicate clip ids in a hand-edited file are re-minted, not aliased

#include "../../Source/Core/ProjectSerializer.h"
#include "../Support/TestTempDirectory.h"
#include "Models/ClipSource.h"
#include "Models/PatternSource.h"
#include "Models/TrackManager.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

// Minimal PCM 16-bit mono WAV writer (enough to satisfy SourceManager loading).
bool writeMinimalWavMono16(const std::filesystem::path& path, int sampleRate, int numSamples) {
    if (sampleRate <= 0 || numSamples <= 0)
        return false;
    const int numChannels = 1;
    const int bitsPerSample = 16;
    const int bytesPerSample = bitsPerSample / 8;
    const int blockAlign = numChannels * bytesPerSample;
    const int byteRate = sampleRate * blockAlign;
    const std::uint32_t dataSize = static_cast<std::uint32_t>(numSamples * blockAlign);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    auto writeU32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto writeU16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    out.write("RIFF", 4);
    writeU32(36u + dataSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeU32(16u);
    writeU16(1u); // PCM
    writeU16(static_cast<std::uint16_t>(numChannels));
    writeU32(static_cast<std::uint32_t>(sampleRate));
    writeU32(static_cast<std::uint32_t>(byteRate));
    writeU16(static_cast<std::uint16_t>(blockAlign));
    writeU16(static_cast<std::uint16_t>(bitsPerSample));
    out.write("data", 4);
    writeU32(dataSize);
    std::vector<char> silence(dataSize, 0);
    out.write(silence.data(), static_cast<std::streamsize>(silence.size()));
    return static_cast<bool>(out);
}

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

std::vector<std::string> collectClipIds(Aestra::Audio::TrackManager& tm) {
    std::vector<std::string> ids;
    auto& playlist = tm.getPlaylistModel();
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

std::vector<std::string> collectLaneIds(Aestra::Audio::TrackManager& tm) {
    std::vector<std::string> ids;
    for (const auto& laneId : tm.getPlaylistModel().getLaneIDs()) {
        ids.push_back(laneId.toString());
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<uint64_t> collectPatternIds(Aestra::Audio::TrackManager& tm) {
    std::vector<uint64_t> ids;
    for (const auto& p : tm.getPatternManager().getAllPatterns()) {
        ids.push_back(p->id.value);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<uint64_t> collectSourceIds(Aestra::Audio::TrackManager& tm) {
    std::vector<uint64_t> ids;
    for (const auto& id : tm.getSourceManager().getAllSourceIDs()) {
        ids.push_back(id.value);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

} // namespace

int main() {
    using namespace Aestra::Audio;

    const Aestra::Tests::ScopedTempDirectory tempDirScope{"ProjectIdentityStability"};
    const auto& tempDir = tempDirScope.path();
    const auto wavPath = tempDir / "identity.wav";
    const auto projectPath = tempDir / "identity.aes";
    require(writeMinimalWavMono16(wavPath, 48000, 4800), "could not write test WAV");

    constexpr double kTempo = 120.0;
    constexpr double kPlayhead = 0.0;

    // ---------------- AestraUUID parsing contract
    {
        const AestraUUID original = AestraUUID::generate();
        AestraUUID parsed;
        require(AestraUUID::tryParse(original.toString(), parsed), "tryParse must accept toString output");
        require(parsed == original, "tryParse(toString()) must round-trip exactly");
        require(!AestraUUID::tryParse("", parsed), "empty string must not parse");
        require(!AestraUUID::tryParse("not-a-uuid", parsed), "garbage must not parse");
        require(!AestraUUID::tryParse(std::string(31, 'a'), parsed), "short string must not parse");
        require(!AestraUUID::tryParse(std::string(31, 'a') + "g", parsed), "non-hex chars must not parse");
        require(!ClipInstanceID::fromString("").isValid(), "legacy empty clip id must stay invalid (mint on add)");
    }

    // ---------------- Arrange: lanes, MIDI + audio patterns, clips
    auto tm1 = std::make_shared<TrackManager>();
    tm1->getPlaylistModel().setPatternManager(&tm1->getPatternManager());
    auto& playlist1 = tm1->getPlaylistModel();
    playlist1.setBPM(kTempo);

    PlaylistLaneID laneAId = playlist1.createLane("Identity A");
    auto* chanA = tm1->addChannel("Identity A");
    PlaylistLaneID laneBId = playlist1.createLane("Identity B");
    auto* chanB = tm1->addChannel("Identity B");
    require(laneAId.isValid() && laneBId.isValid() && chanA && chanB, "setup: lanes/channels");

    MidiPayload midi;
    MidiNote note;
    note.pitch = 64;
    note.startBeat = 1.5;
    note.durationBeats = 0.5;
    note.velocity = 0.8f;
    midi.notes.push_back(note);
    PatternID midiPatId = tm1->getPatternManager().createMidiPattern("IdentityMelody", 8.0, midi);

    ClipSourceID srcId = tm1->getSourceManager().getOrCreateSource(wavPath.string());
    AudioSlicePayload slicePayload;
    slicePayload.audioSourceId = srcId;
    PatternID audioPatId = tm1->getPatternManager().createAudioPattern("IdentityAudio", 4.0, slicePayload);
    require(midiPatId.isValid() && srcId.isValid() && audioPatId.isValid(), "setup: patterns/source");

    ClipInstanceID midiClipId = playlist1.addClipFromPattern(laneAId, midiPatId, 0.0, 8.0);
    ClipInstanceID audioClipId = playlist1.addClipFromPattern(laneBId, audioPatId, 4.0, 4.0);
    require(midiClipId.isValid() && audioClipId.isValid(), "setup: clips");

    const auto clipIds1 = collectClipIds(*tm1);
    const auto laneIds1 = collectLaneIds(*tm1);
    const auto patternIds1 = collectPatternIds(*tm1);
    const auto sourceIds1 = collectSourceIds(*tm1);

    // ---------------- Generation 2: identities must survive the load
    auto ser1 = ProjectSerializer::serialize(tm1, kTempo, kPlayhead, 2);
    require(ser1.ok && !ser1.contents.empty(), "generation-1 serialize failed");
    require(ProjectSerializer::writeAtomically(projectPath.string(), ser1.contents), "generation-1 write failed");

    auto tm2 = std::make_shared<TrackManager>();
    tm2->getPlaylistModel().setPatternManager(&tm2->getPatternManager());
    ProjectSerializer::LoadResult load1 = ProjectSerializer::load(projectPath.string(), tm2);
    require(load1.ok, "generation-2 load failed");

    require(collectClipIds(*tm2) == clipIds1, "clip UUIDs must survive load unchanged");
    require(collectLaneIds(*tm2) == laneIds1, "lane UUIDs must survive load unchanged");
    require(collectPatternIds(*tm2) == patternIds1, "pattern IDs must survive load unchanged");
    require(collectSourceIds(*tm2) == sourceIds1, "source IDs must survive load unchanged");

    // ---------------- Byte-stable fixed point + idempotence
    auto ser2 = ProjectSerializer::serialize(tm2, load1.tempo, load1.playhead, 2);
    require(ser2.ok, "generation-2 serialize failed");
    require(ser2.contents == ser1.contents, "save->load->save must be byte-identical (fixed point)");

    const auto gen3Path = tempDir / "identity_gen3.aes";
    require(ProjectSerializer::writeAtomically(gen3Path.string(), ser2.contents), "generation-3 write failed");
    auto tm3 = std::make_shared<TrackManager>();
    tm3->getPlaylistModel().setPatternManager(&tm3->getPatternManager());
    ProjectSerializer::LoadResult load2 = ProjectSerializer::load(gen3Path.string(), tm3);
    require(load2.ok, "generation-3 load failed");
    auto ser3 = ProjectSerializer::serialize(tm3, load2.tempo, load2.playhead, 2);
    require(ser3.ok, "generation-3 serialize failed");
    require(ser3.contents == ser2.contents, "fixed point must hold across further generations");

    // ---------------- Collision safety: post-load mints never reuse restored IDs
    {
        const uint64_t maxPattern = patternIds1.back();
        PatternID freshPattern = tm2->getPatternManager().createMidiPattern("PostLoad", 4.0, MidiPayload{});
        require(freshPattern.value > maxPattern, "post-load pattern mint must exceed restored pattern IDs");

        const uint64_t maxSource = sourceIds1.back();
        const auto wav2 = tempDir / "identity2.wav";
        require(writeMinimalWavMono16(wav2, 48000, 480), "could not write second WAV");
        ClipSourceID freshSource = tm2->getSourceManager().getOrCreateSource(wav2.string());
        require(freshSource.value > maxSource, "post-load source mint must exceed restored source IDs");

        PlaylistLaneID freshLane = tm2->getPlaylistModel().createLane("PostLoad");
        require(freshLane.isValid(), "post-load lane create failed");
        auto laneIds2 = collectLaneIds(*tm2);
        require(std::count(laneIds2.begin(), laneIds2.end(), freshLane.toString()) == 1,
                "post-load lane ID must be unique");

        ClipInstanceID freshClip =
            tm2->getPlaylistModel().addClipFromPattern(freshLane, freshPattern, 0.0, 4.0);
        require(freshClip.isValid(), "post-load clip create failed");
        require(std::count(clipIds1.begin(), clipIds1.end(), freshClip.toString()) == 0,
                "post-load clip UUID must not collide with restored clips");
    }

    // ---------------- Duplicate clip ids in a hand-edited file get re-minted
    {
        std::string mangled = ser1.contents;
        const std::string fromId = audioClipId.toString();
        const std::string toId = midiClipId.toString();
        const size_t pos = mangled.find(fromId);
        require(pos != std::string::npos, "mangle setup: audio clip id not found in contents");
        mangled.replace(pos, fromId.size(), toId);

        const auto dupPath = tempDir / "identity_dup.aes";
        require(ProjectSerializer::writeAtomically(dupPath.string(), mangled), "duplicate-id write failed");
        auto tmDup = std::make_shared<TrackManager>();
        tmDup->getPlaylistModel().setPatternManager(&tmDup->getPatternManager());
        ProjectSerializer::LoadResult loadDup = ProjectSerializer::load(dupPath.string(), tmDup);
        require(loadDup.ok, "duplicate-id load failed");

        auto dupClipIds = collectClipIds(*tmDup);
        require(dupClipIds.size() == 2, "both clips must survive a duplicate-id file");
        require(dupClipIds[0] != dupClipIds[1], "duplicate clip ids must be re-minted, not aliased");
    }

    std::cout << "[PASS] ProjectIdentityStabilityTest\n";
    return 0;
}
