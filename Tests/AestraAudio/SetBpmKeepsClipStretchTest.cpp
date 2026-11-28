// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// SetBpmKeepsClipStretchTest — setting the tempo must not undo a clip's stretch.
//
// Reported on the owner's "Wavy" project: every clip that had been stretched (fit to bars, or a rate edit) came
// back unstretched after reopening. The file was fine. After a load the app calls PlaylistModel::setBPM, and
// setBPM re-derived each audio clip's beat span from durationSeconds alone. durationSeconds is source-domain time
// (#746: durationSeconds == beatToSeconds(durationBeats) / varispeed), so a clip at 1.6x lost the factor and a
// 16-beat clip became 10.
//
// The span a clip occupies on the timeline is durationSeconds * varispeed. A tempo change keeps that real time, so
// it moves the beat count, but a tempo "change" to the same value must not move anything.

#include "GoldenAudio/GoldenAudioHarness.h"
#include "Models/PlaylistModel.h"

#include <cmath>
#include <iostream>

using namespace Aestra::Audio;
using namespace GoldenAudio;

namespace {
int g_failures = 0;

void check(bool condition, const std::string& label) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << label << std::endl;
    if (!condition) {
        ++g_failures;
    }
}

// The rate is stored as a float (1.6f is 1.60000002), so exact-to-1e-9 would fail on the premise, not the code.
bool nearEq(double a, double b) { return std::abs(a - b) < 1e-6; }

// One audio clip, 0..4 beats at 120 BPM (2 s), edited to the given speed and pitch.
std::shared_ptr<TrackManager> sessionWithClip(float speed, float pitchSemitones) {
    const SessionConfig cfg;
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(cfg.sampleRate));
    const uint32_t frames = static_cast<uint32_t>(cfg.sampleRate) * 2;
    std::vector<float> silence(static_cast<size_t>(frames) * cfg.channels, 0.1f);
    addAudioTrack(*tm, "SetBpm", silence, frames, cfg);

    auto& model = tm->getPlaylistModel();
    const ClipInstanceID id = model.getLane(model.getLaneId(0))->clips.front().id;
    ClipEdits edits;
    edits.playbackRate = speed;
    edits.pitchSemitones = pitchSemitones;
    model.setClipEdits(id, edits);
    return tm;
}

const ClipInstance& theClip(PlaylistModel& model) { return model.getLane(model.getLaneId(0))->clips.front(); }
} // namespace

int main() {
    {
        // The reported case: 1.6x, the Sweet Vocals ratio.
        auto tm = sessionWithClip(1.6f, 0.0f);
        auto& model = tm->getPlaylistModel();
        const double span = theClip(model).durationBeats;
        check(nearEq(span, 4.0), "premise: the stretched clip spans 4 beats");
        check(nearEq(theClip(model).durationSeconds, 2.0 / theClip(model).edits.effectiveVarispeed()), "premise: its canonical seconds are source-domain");

        model.setBPM(120.0); // what a project load does, at the tempo it already has
        check(nearEq(theClip(model).durationBeats, 4.0), "setBPM to the same tempo keeps a stretched clip's span");

        model.setBPM(60.0);
        check(nearEq(theClip(model).durationBeats, 2.0),
              "a tempo change keeps the stretched clip's real time (2 s is 2 beats at 60 BPM)");
        check(nearEq(model.beatToSeconds(theClip(model).durationBeats),
                     theClip(model).durationSeconds * theClip(model).edits.effectiveVarispeed()),
              "the #746 invariant still holds after the tempo change");
    }

    {
        // Pitch is part of the varispeed, so it must not be lost either.
        auto tm = sessionWithClip(1.0f, 12.0f);
        auto& model = tm->getPlaylistModel();
        model.setBPM(120.0);
        check(nearEq(theClip(model).durationBeats, 4.0), "setBPM to the same tempo keeps a pitched clip's span");
    }

    {
        // Control: an unstretched clip behaves as it always did.
        auto tm = sessionWithClip(1.0f, 0.0f);
        auto& model = tm->getPlaylistModel();
        model.setBPM(120.0);
        check(nearEq(theClip(model).durationBeats, 4.0), "control: an unstretched clip keeps its span");
        model.setBPM(60.0);
        check(nearEq(theClip(model).durationBeats, 2.0), "control: an unstretched clip follows a tempo change");
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "set bpm keeps clip stretch passed\n";
    return 0;
}
