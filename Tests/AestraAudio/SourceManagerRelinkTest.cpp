// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// T-7 relink/recovery (C-004): a project whose audio files moved or vanished
// loads with retryable placeholder sources; relinking rebinds a source to a
// new path without corrupting the manager's bookkeeping.
//
// The path-dedupe map is the part that can silently rot: a relink that only
// calls ClipSource::setFilePath leaves the OLD path as a live key, so a later
// getOrCreateSource(oldPath) mints a duplicate source and the project carries
// two identities for one asset. These cases pin the map move, the collision
// rejection, and the serializer round-trip (a relinked path must survive
// save/reload with no missingAssets).

#include "Models/SourceManager.h"
#include "../../Source/Core/ProjectSerializer.h"
#include "Models/TrackManager.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <filesystem>

namespace {
using namespace Aestra::Audio;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }
}

std::shared_ptr<AudioBufferData> makeBuffer(uint32_t frames = 128) {
    auto buffer = std::make_shared<AudioBufferData>();
    buffer->numChannels = 2;
    buffer->sampleRate = 48000;
    buffer->interleavedData.assign(static_cast<size_t>(frames) * 2, 0.25f);
    buffer->numFrames = frames;
    return buffer;
}

// A minimal but genuinely decodable PCM16 WAV (mono, 48 kHz, 240 frames), so a
// relinked path survives a reload's real decode attempt.
void writeTinyWav(const std::filesystem::path& path) {
    const uint32_t sampleRate = 48000;
    const uint16_t channels = 1;
    const uint16_t bitsPerSample = 16;
    const uint32_t frames = 240;
    const uint32_t dataBytes = frames * channels * (bitsPerSample / 8);

    std::ofstream out(path, std::ios::binary);
    auto u32 = [&out](uint32_t v) {
        char b[4] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF),
                     static_cast<char>((v >> 16) & 0xFF), static_cast<char>((v >> 24) & 0xFF)};
        out.write(b, 4);
    };
    auto u16 = [&out](uint16_t v) {
        char b[2] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF)};
        out.write(b, 2);
    };
    out.write("RIFF", 4);
    u32(36 + dataBytes);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    u32(16);
    u16(1); // PCM
    u16(channels);
    u32(sampleRate);
    u32(sampleRate * channels * (bitsPerSample / 8));
    u16(channels * (bitsPerSample / 8));
    u16(bitsPerSample);
    out.write("data", 4);
    u32(dataBytes);
    for (uint32_t i = 0; i < frames; ++i) {
        const int16_t sample = static_cast<int16_t>(2000 * ((i % 48) < 24 ? 1 : -1));
        u16(static_cast<uint16_t>(sample));
    }
    require(out.good(), "writeTinyWav: writing the fixture must succeed");
}

// A relink must move the dedupe key: the old path stops resolving, the new
// path resolves to the same id, and a later getOrCreateSource on the old path
// must NOT mint a duplicate for the same asset.
void testRelinkMovesDedupeKey() {
    SourceManager mgr;
    const char* oldPath = "/tmp/aestra-relink-moved.wav";
    const char* newPath = "/tmp/aestra-relink-new.wav";
    const ClipSourceID id = mgr.getOrCreateSource(oldPath);

    const uint64_t before = mgr.getRevision();
    require(mgr.relinkSource(id, newPath), "movesKey: relink to a free path must succeed");
    require(mgr.getSource(id) && mgr.getSource(id)->getFilePath() == newPath,
            "movesKey: source must report the new path");
    require(!mgr.findSourceByPath(oldPath).isValid(), "movesKey: the old path must stop resolving");
    require(mgr.findSourceByPath(newPath) == id, "movesKey: the new path must resolve to the same id");
    require(mgr.getRevision() != before, "movesKey: moving a key changes the source set, revision must bump");

    const ClipSourceID again = mgr.getOrCreateSource(oldPath);
    require(again.isValid() && again != id,
            "movesKey: re-creating the old path must mint a NEW source, not resurrect the relinked one");
}

// Two sources must never share a path: a relink onto a taken path is a merge
// decision, not a side effect, so it is refused and nothing changes.
void testRelinkRejectsTakenPath() {
    SourceManager mgr;
    const ClipSourceID a = mgr.getOrCreateSource("/tmp/aestra-relink-a.wav");
    const ClipSourceID b = mgr.getOrCreateSource("/tmp/aestra-relink-b.wav");

    require(!mgr.relinkSource(a, "/tmp/aestra-relink-b.wav"),
            "takenPath: relinking onto another source's path must fail");
    require(mgr.getSource(a) && mgr.getSource(a)->getFilePath() == "/tmp/aestra-relink-a.wav",
            "takenPath: refused relink must leave the source where it was");
    require(mgr.findSourceByPath("/tmp/aestra-relink-b.wav") == b,
            "takenPath: the existing owner must keep the path");
}

// Relinking to the path the source already has (user restored the original
// file and re-picked it) is a valid retry and must not disturb the map.
void testRelinkToSamePathSucceeds() {
    SourceManager mgr;
    const char* path = "/tmp/aestra-relink-same.wav";
    const ClipSourceID id = mgr.getOrCreateSource(path);
    const uint64_t before = mgr.getRevision();

    require(mgr.relinkSource(id, path), "samePath: relinking to the current path must succeed");
    require(mgr.findSourceByPath(path) == id, "samePath: the path key must survive");
    require(mgr.getRevision() == before, "samePath: no bookkeeping changed, revision must not bump");
}

void testRelinkUnknownIdFails() {
    SourceManager mgr;
    mgr.getOrCreateSource("/tmp/aestra-relink-unknown.wav");
    require(!mgr.relinkSource(ClipSourceID{987654}, "/tmp/aestra-relink-elsewhere.wav"),
            "unknownId: relinking an absent source must fail");
    require(!mgr.relinkSource(mgr.getOrCreateSource("/tmp/aestra-relink-unknown.wav"), ""),
            "unknownId: an empty path must be refused");
}

// The full loop the dialog drives: load a project whose asset is gone
// (missingAssets reported, source present but not ready), relink, attach, and
// confirm a save/reload cycle comes back clean at the new path.
void testMissingAssetRoundTrip() {
    const std::filesystem::path testDir =
        std::filesystem::temp_directory_path() / "aestra-relink-test";
    std::filesystem::create_directories(testDir);

    const char* ghostPath = "/tmp/aestra-relink-ghost.wav";
    const char* realPath = "/tmp/aestra-relink-real.wav";

    // Project A: one source pointing at a file that does not exist.
    {
        auto trackManager = std::make_shared<TrackManager>();
        trackManager->getSourceManager().getOrCreateSource(ghostPath);
        const auto serialized = ProjectSerializer::serialize(trackManager, 120.0, 0.0, 0);
        require(serialized.ok, "roundTrip: precondition — serialization must succeed");
        std::ofstream out(testDir / "missing.aes");
        out << serialized.contents;
        require(out.good(), "roundTrip: writing missing.aes must succeed");
    }

    auto loaded = std::make_shared<TrackManager>();
    const auto loadResult = ProjectSerializer::load((testDir / "missing.aes").string(), loaded);
    require(loadResult.ok, "roundTrip: loading a project with a missing asset must still succeed");
    if (loadResult.missingAssets.size() != 1 || loadResult.missingAssets[0] != ghostPath) {
        std::cerr << "[FAIL] roundTrip: missingAssets size=" << loadResult.missingAssets.size() << " contents:";
        for (const auto& p : loadResult.missingAssets) std::cerr << " '" << p << "'";
        std::cerr << '\n';
        std::exit(1);
    }

    auto& sourceManager = loaded->getSourceManager();
    const ClipSourceID id = sourceManager.findSourceByPath(ghostPath);
    require(id.isValid(), "roundTrip: the placeholder source must exist under the stored path");
    const auto* source = sourceManager.getSource(id);
    require(source && !source->isReady(),
            "roundTrip: precondition — the placeholder must not be ready before relink");

    // The dialog's relink flow: the user picks a real, decodable file —
    // rebind, then attach the decoded buffer.
    writeTinyWav(realPath);
    require(sourceManager.relinkSource(id, realPath), "roundTrip: relink must succeed");
    sourceManager.attachBuffer(sourceManager.getSource(id), makeBuffer());
    require(sourceManager.getSource(id)->isReady(), "roundTrip: source must be ready after attach");

    const auto reserialized = ProjectSerializer::serialize(loaded, loadResult.tempo, loadResult.playhead, 0);
    require(reserialized.ok, "roundTrip: reserialization must succeed");
    std::ofstream out2(testDir / "relinked.aes");
    require(out2.good(), "roundTrip: opening relinked.aes must succeed");
    out2 << reserialized.contents;
    out2.close();
    require(out2.good() && std::filesystem::file_size(testDir / "relinked.aes") > 0,
            "roundTrip: writing relinked.aes must succeed and land bytes");

    auto reloaded = std::make_shared<TrackManager>();
    const auto reloadResult = ProjectSerializer::load((testDir / "relinked.aes").string(), reloaded);
    require(reloadResult.ok, "roundTrip: reloading the relinked project must succeed");
    require(reloadResult.missingAssets.empty(),
            "roundTrip: the relinked path must not be reported missing after save/reload");
    const ClipSourceID reloadedId = reloaded->getSourceManager().findSourceByPath(realPath);
    require(reloadedId.isValid(), "roundTrip: the relinked path must persist through serialization");
    const auto* reloadedSource = reloaded->getSourceManager().getSource(reloadedId);
    require(reloadedSource && reloadedSource->isReady() && reloadedSource->getNumFrames() == 240,
            "roundTrip: the reloaded source must decode the real file (240 frames)");

    std::filesystem::remove_all(testDir);
}

} // namespace

int main() {
    testRelinkMovesDedupeKey();
    testRelinkRejectsTakenPath();
    testRelinkToSamePathSucceeds();
    testRelinkUnknownIdFails();
    testMissingAssetRoundTrip();

    std::cout << "[PASS] SourceManagerRelinkTest\n";
    return 0;
}
