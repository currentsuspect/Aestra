// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// RecordingCleanupTest — reference-aware recording file cleanup.
//
// Regression for the recording-lifecycle sweep: WAVs accumulate in the
// Recordings folder when takes are discarded. cleanupOrphanedRecordings() may
// only remove files no live clip references and no on-disk project references,
// and only when recording is not in progress.
//
// Covered:
//   - a committed (kept) take's WAV survives cleanup,
//   - an undone (discarded) take's WAV is removed and its source dropped,
//   - an on-disk project reference (keepCheck) keeps a discard,
//   - cleanup no-ops while recording is in progress.

#include "Core/MixerChannel.h"
#include "Models/TrackManager.h"
#include "../../Source/Core/ProjectSerializer.h"
#include "../Support/TestTempDirectory.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr double kBpm = 120.0;

int g_failures = 0;

void check(bool condition, const std::string& label) {
    if (!condition) {
        std::cerr << "  FAIL: " << label << "\n";
        ++g_failures;
    } else {
        std::cout << "  PASS: " << label << "\n";
    }
}

std::shared_ptr<TrackManager> makeRecorder(const std::string& projectPath) {
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(kSampleRate));
    tm->getPlaylistModel().setBPM(kBpm);
    tm->setInputChannelCount(1);
    tm->setMaxRecordingSeconds(5.0);
    tm->setRecordingProjectPath(projectPath);
    return tm;
}

uint64_t armTrack(TrackManager& tm, const std::string& name) {
    const PlaylistLaneID laneId = tm.getPlaylistModel().createLane(name);
    MixerChannel* ch = tm.addChannel(name);
    if (!ch) {
        return 0;
    }
    ch->setInputChannelIndex(0);
    const uint64_t trackId = tm.createTrack(laneId, name, ch->getChannelId());
    if (trackId != 0) {
        tm.setTrackArmed(trackId, true);
    }
    return trackId;
}

void feedInput(TrackManager& tm, double seconds) {
    const uint32_t frames = static_cast<uint32_t>(seconds * static_cast<double>(kSampleRate));
    std::vector<float> input(frames);
    for (uint32_t i = 0; i < frames; ++i) {
        input[i] = 0.25f * std::sin(2.0 * 3.14159265 * 440.0 * static_cast<double>(i) / static_cast<double>(kSampleRate));
    }
    tm.processInput(input.data(), frames);
}

/** Record a take: transport on → capture → transport off → commit. */
void recordTake(TrackManager& tm, double seconds) {
    tm.onTransportStateApplied(true, 0, static_cast<double>(kSampleRate));
    feedInput(tm, seconds);
    tm.onTransportStateApplied(false, static_cast<uint64_t>(seconds * kSampleRate), static_cast<double>(kSampleRate));
}

std::string recordingsDir(const std::string& projectPath) {
    return (std::filesystem::path(projectPath).parent_path() / "Recordings").string();
}

size_t wavCount(const std::string& dir) {
    std::error_code ec;
    size_t n = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".wav") {
            ++n;
        }
    }
    return n;
}

// Test 1: A committed take's WAV survives cleanup.
bool testKeptTakeSurvives() {
    std::cout << "  [1/5] Committed take survives cleanup... ";
    Aestra::Tests::ScopedTempDirectory dir{"RecordingCleanupKept"};
    const std::string projectPath = (dir.path() / "kept.aes").string();
    auto tm = makeRecorder(projectPath);

    const uint64_t trackId = armTrack(*tm, "Guitar");
    check(trackId != 0, "track created");
    if (trackId == 0) {
        return false;
    }
    tm->record();
    recordTake(*tm, 0.5);
    check(wavCount(recordingsDir(projectPath)) == 1, "one WAV written after take");
    check(tm->isRecording() == false, "capture finalized");

    const size_t removed = tm->cleanupOrphanedRecordings({});
    check(removed == 0, "no orphaned recordings removed");
    check(wavCount(recordingsDir(projectPath)) == 1, "kept take's WAV still on disk");

    std::cout << "PASSED\n";
    return true;
}

// Test 2: An undone (discarded) take's WAV is removed.
bool testDiscardedTakeRemoved() {
    std::cout << "  [2/5] Discarded take removed... ";
    Aestra::Tests::ScopedTempDirectory dir{"RecordingCleanupDiscard"};
    const std::string projectPath = (dir.path() / "discard.aes").string();
    auto tm = makeRecorder(projectPath);

    const uint64_t trackId = armTrack(*tm, "Guitar");
    if (trackId == 0) {
        return false;
    }
    tm->record();
    recordTake(*tm, 0.5);
    check(wavCount(recordingsDir(projectPath)) == 1, "one WAV after take");

    // User discards the take (undo). The clip/lane are peeled off; the WAV
    // remains on disk (nothing deleted at undo time — redo must still work).
    tm->getCommandHistory().undo();
    check(wavCount(recordingsDir(projectPath)) == 1, "undo alone leaves the WAV for redo");

    const std::string realWavPath = [&] {
        const std::string dirPath = recordingsDir(projectPath);
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(dirPath, ec)) {
            if (e.path().extension() == ".wav") {
                return e.path().string();
            }
        }
        return std::string{};
    }();
    check(!realWavPath.empty(), "discarded WAV path resolved");
    check(tm->getSourceManager().findSourceByPath(realWavPath).isValid(), "WAV registered as a source");

    // Session boundary: history is being discarded, so the take is unreachable.
    const size_t removed = tm->cleanupOrphanedRecordings({});
    check(removed == 1, "discarded take's WAV removed");
    check(wavCount(recordingsDir(projectPath)) == 0, "no recording files remain");

    // The orphaned source registration must be dropped too (no dangling save).
    check(tm->getSourceManager().findSourceByPath(realWavPath).isValid() == false,
          "orphaned source registration dropped");

    std::cout << "PASSED\n";
    return true;
}

// Test 3: An on-disk project reference keeps a discarded take.
bool testProjectReferenceKeeps() {
    std::cout << "  [3/5] On-disk reference keeps the WAV (real project-file scan)... ";
    Aestra::Tests::ScopedTempDirectory dir{"RecordingCleanupReferenced"};
    const std::string projectPath = (dir.path() / "referenced.aes").string();
    auto tm = makeRecorder(projectPath);

    const uint64_t trackId = armTrack(*tm, "Guitar");
    if (trackId == 0) {
        return false;
    }
    tm->record();
    recordTake(*tm, 0.5);

    // Save the project the way AestraApp does: sources[] persists the take's
    // path in generic (forward-slash) form. Then discard the take.
    const bool saved = ProjectSerializer::save(projectPath, tm, kBpm, 0.0);
    check(saved, "project saved with the take");
    if (!saved) {
        return false;
    }
    const std::string wavPath = [&] {
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(recordingsDir(projectPath), ec)) {
            if (e.path().extension() == ".wav") {
                return e.path().string();
            }
        }
        return std::string{};
    }();
    check(!wavPath.empty(), "recorded WAV path resolved");
    tm->getCommandHistory().undo();

    // Exercise the app's real keeper flow (AestraApp::cleanupUnreferencedRecordings
    // → pathAppearsInFile): keep the WAV when the on-disk project text contains its
    // generic serialized path. This catches serialized-path vs scan mismatches.
    const auto keeper = [&projectPath](const std::string& p) -> bool {
        const std::string generic = std::filesystem::path(p).generic_string();
        std::ifstream in(projectPath, std::ios::in | std::ios::binary);
        if (!in) {
            return false;
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str().find(generic) != std::string::npos;
    };
    const size_t removed = tm->cleanupOrphanedRecordings(keeper);
    check(removed == 0, "project-referenced discard kept");
    check(wavCount(recordingsDir(projectPath)) == 1, "WAV still on disk");

    std::cout << "PASSED\n";
    return true;
}

// Test 4: Cleanup no-ops while recording is in progress.
bool testNoCleanupWhileCapturing() {
    std::cout << "  [4/5] No cleanup while recording... ";
    Aestra::Tests::ScopedTempDirectory dir{"RecordingCleanupInFlight"};
    const std::string projectPath = (dir.path() / "inflight.aes").string();
    auto tm = makeRecorder(projectPath);

    const uint64_t trackId = armTrack(*tm, "Guitar");
    if (trackId == 0) {
        return false;
    }
    tm->record();
    tm->onTransportStateApplied(true, 0, static_cast<double>(kSampleRate));
    check(tm->isRecording(), "capture in progress");
    const size_t removed = tm->cleanupOrphanedRecordings({});
    check(removed == 0, "cleanup refused during capture");

    tm->onTransportStateApplied(false, static_cast<uint64_t>(0.5 * kSampleRate), static_cast<double>(kSampleRate));
    std::cout << "PASSED\n";
    return true;
}

// Test 5: Cleanup only touches files under the recording root.
bool testScopedToRecordingRoot() {
    std::cout << "  [5/5] Cleanup scoped to the recording root... ";
    Aestra::Tests::ScopedTempDirectory dir{"RecordingCleanupScope"};
    const std::string projectPath = (dir.path() / "scope.aes").string();
    auto tm = makeRecorder(projectPath);

    // Unrelated audio files next to the recording root must never be touched.
    const std::string outsider = (dir.path() / "keepme.wav").string();
    {
        std::ofstream out(outsider, std::ios::binary | std::ios::trunc);
        out << "not a recording";
    }
    const size_t removed = tm->cleanupOrphanedRecordings({});
    check(removed == 0, "no recordings to remove");
    check(std::filesystem::exists(outsider), "unrelated WAV untouched");

    std::cout << "PASSED\n";
    return true;
}

} // namespace

int main() {
    std::cout << "=== Recording Cleanup Tests ===\n";
    std::cout << "(Reference-aware recording file lifecycle, headless)\n\n";

    const bool ok = testKeptTakeSurvives() & testDiscardedTakeRemoved() & testProjectReferenceKeeps() &
                    testNoCleanupWhileCapturing() & testScopedToRecordingRoot();
    check(g_failures == 0, "no failures");

    std::cout << "\n";
    if (!ok || g_failures > 0) {
        std::cout << "FAILED: " << g_failures << " recording cleanup assertions failed.\n";
        return 1;
    }
    std::cout << "All recording cleanup tests passed.\n";
    return 0;
}
