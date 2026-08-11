// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// PlaylistSplitVarispeedTest — split and trim must keep the audio clip's
// source-domain invariants when Speed/Pitch varispeed != 1 (#746 regression,
// user report: "slicing pitched clips is not slicing as intended").
//
// Contract under test (canonical audio-clip fields, see TrimClipCommand):
//   durationSeconds      == beatToSeconds(durationBeats) / varispeed
//   sourceOffsetSeconds  == original + trimmedBeats * beatToSeconds(1) * varispeed
// where varispeed = clamp(speed * 2^(pitch/12), 0.25, 4). The renderer reads
// these two fields directly (ClipRenderService::resolveClipRegion), so a
// split/trim that ignores varispeed plays the second half at the wrong source
// position and for the wrong length.
//
// Model-level regression gate: fast, deterministic, no audio rendering. The
// end-to-end splice-null proof lives in RealtimeExportParityTest
// (runSplitVarispeedParityCase).

#include "GoldenAudio/GoldenAudioHarness.h"
#include "Commands/TrimClipCommand.h"
#include "Models/PlaylistModel.h"

#include <cmath>
#include <iostream>

using namespace Aestra::Audio;
using namespace GoldenAudio;

namespace {

constexpr double kBeats = 4.0; // 2 s at 120 BPM
constexpr double kSecondsPerBeat = 0.5; // 120 BPM
constexpr double kTau = 6.28318530717958647692;

bool nearEq(double a, double b, double tol = 1e-9) {
    return std::abs(a - b) <= tol;
}

struct CaseResult {
    int failures = 0;
    void check(bool cond, const std::string& what) {
        if (!cond) {
            ++failures;
            std::cout << "    FAIL: " << what << "\n";
        }
    }
};

// Build a one-track session whose clip spans beat 0..kBeats (2 s of 440 Hz).
std::shared_ptr<TrackManager> buildSession(const SessionConfig& cfg) {
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(cfg.sampleRate));
    const uint32_t totalFrames = static_cast<uint32_t>(cfg.sampleRate) * 2;
    std::vector<float> sine(static_cast<size_t>(totalFrames) * cfg.channels, 0.0f);
    for (uint32_t i = 0; i < totalFrames; ++i) {
        const float v = static_cast<float>(std::sin(kTau * 440.0 * static_cast<double>(i) / cfg.sampleRate)) * 0.3f;
        sine[static_cast<size_t>(i) * cfg.channels] = v;
        sine[static_cast<size_t>(i) * cfg.channels + 1] = v;
    }
    addAudioTrack(*tm, "SplitVarispeed", sine, totalFrames, cfg);
    return tm;
}

ClipInstanceID firstClipId(PlaylistModel& model) {
    const auto laneId = model.getLaneId(0);
    const auto* lane = model.getLane(laneId);
    if (!lane || lane->clips.empty()) {
        return ClipInstanceID();
    }
    return lane->clips.front().id;
}

void applyVarispeed(PlaylistModel& model, float speed, float pitchSemitones) {
    ClipEdits edits;
    edits.playbackRate = speed;
    edits.pitchSemitones = pitchSemitones;
    const ClipInstanceID id = firstClipId(model);
    if (!model.setClipEdits(id, edits)) {
        std::cout << "    FAIL: setClipEdits\n";
    }
}

const ClipInstance* getClip(PlaylistModel& model, ClipInstanceID id) {
    return model.getClip(id);
}

// Split at beat 2 (half the clip). With varispeed v:
//   left  half: durationSeconds == 1 s / v
//   right half: sourceOffsetSeconds == 1 s * v, durationSeconds == 1 s / v
bool runSplitCase(const SessionConfig& cfg, float speed, float pitchSemitones, const char* label,
                  CaseResult& out) {
    std::cout << "TEST: split " << label << "\n";
    auto tm = buildSession(cfg);
    auto& model = tm->getPlaylistModel();
    applyVarispeed(model, speed, pitchSemitones);
    const ClipInstanceID id = firstClipId(model);
    if (!id.isValid()) {
        out.check(false, "no clip on lane 0");
        return false;
    }

    const ClipInstanceID secondId = model.splitClip(id, 2.0);
    out.check(secondId.isValid(), "splitClip returned invalid id");

    const auto* left = getClip(model, id);
    const auto* right = getClip(model, secondId);
    if (!left || !right) {
        out.check(false, "split halves not found");
        return false;
    }

    out.check(nearEq(left->durationBeats, 2.0), "left durationBeats");
    out.check(nearEq(right->durationBeats, 2.0), "right durationBeats");
    out.check(nearEq(right->startBeat, 2.0), "right startBeat");

    const double varispeed =
        std::clamp(speed * std::pow(2.0, pitchSemitones / 12.0), 0.25, 4.0);
    // Split at beat 2 of 4: each half is 2 beats = 1 s at 120 BPM.
    out.check(nearEq(left->durationSeconds, 1.0 / varispeed),
              "left durationSeconds (source seconds)");
    out.check(nearEq(right->sourceOffsetSeconds, 1.0 * varispeed),
              "right sourceOffsetSeconds advances by 1 s * varispeed");
    out.check(nearEq(right->durationSeconds, 1.0 / varispeed),
              "right durationSeconds (source seconds)");
    out.check(nearEq(left->sourceOffsetSeconds, 0.0), "left sourceOffsetSeconds unchanged");

    const double expectedSourceStartSamples = 1.0 * varispeed * cfg.sampleRate;
    out.check(model.getClipSourceStartSamples(*right) == static_cast<uint64_t>(expectedSourceStartSamples),
              "right runtime/visual source start matches split offset");
    return true;
}

// Left-edge trim of 1 beat via TrimClipCommand (lazy variant):
//   durationSeconds == 1.5 s / v, sourceOffsetSeconds == 0.5 s * v
bool runTrimCase(const SessionConfig& cfg, float speed, float pitchSemitones, const char* label,
                 CaseResult& out) {
    std::cout << "TEST: trim " << label << "\n";
    auto tm = buildSession(cfg);
    auto& model = tm->getPlaylistModel();
    applyVarispeed(model, speed, pitchSemitones);
    const ClipInstanceID id = firstClipId(model);
    if (!id.isValid()) {
        out.check(false, "no clip on lane 0");
        return false;
    }

    TrimClipCommand command(model, id, 1.0, -1.0);
    command.execute();
    const auto* clip = getClip(model, id);
    if (!clip) {
        out.check(false, "clip missing after trim");
        return false;
    }

    const double varispeed =
        std::clamp(speed * std::pow(2.0, pitchSemitones / 12.0), 0.25, 4.0);
    out.check(nearEq(clip->startBeat, 1.0), "trim startBeat");
    out.check(nearEq(clip->durationBeats, 3.0), "trim durationBeats");
    out.check(nearEq(clip->durationSeconds, 3.0 * kSecondsPerBeat / varispeed),
              "trim durationSeconds (source seconds)");
    out.check(nearEq(clip->sourceOffsetSeconds, kSecondsPerBeat * varispeed),
              "trim sourceOffsetSeconds advance");

    command.undo();
    const auto* restored = getClip(model, id);
    out.check(restored != nullptr, "clip present after undo");
    if (restored) {
        out.check(nearEq(restored->startBeat, 0.0), "undo startBeat");
        out.check(nearEq(restored->durationBeats, kBeats), "undo durationBeats");
        out.check(nearEq(restored->durationSeconds, kBeats * kSecondsPerBeat), "undo durationSeconds");
        out.check(nearEq(restored->sourceOffsetSeconds, 0.0), "undo sourceOffsetSeconds");
    }
    return true;
}

} // namespace

int main() {
    const SessionConfig cfg;
    std::cout << "\n=== Playlist Split/Trim Varispeed Tests ===\n\n";

    CaseResult result;
    runSplitCase(cfg, 1.0f, 12.0f, "pitch +12 (varispeed 2)", result);
    runSplitCase(cfg, 2.0f, 0.0f, "speed 2x (varispeed 2)", result);
    runSplitCase(cfg, 1.0f, 0.0f, "unity control (varispeed 1)", result);
    runTrimCase(cfg, 1.0f, 12.0f, "pitch +12 (varispeed 2)", result);
    runTrimCase(cfg, 1.0f, 0.0f, "unity control (varispeed 1)", result);

    if (result.failures == 0) {
        std::cout << "\nAll PlaylistSplitVarispeed tests passed.\n";
        return 0;
    }
    std::cout << "\n" << result.failures << " PlaylistSplitVarispeed assertion(s) failed.\n";
    return 1;
}
