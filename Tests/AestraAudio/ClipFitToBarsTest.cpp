// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

// #747: varispeed tempo-fit math. Pitch follows tempo by definition — these
// cases pin the span/rate relationship, the varispeed clamp, the input guards,
// the pitch-folded base rate, and the command-level fit transaction.

#include "Commands/CommandTransaction.h"
#include "Commands/SetClipEditsCommand.h"
#include "Commands/TrimClipCommand.h"
#include "Models/ClipFit.h"
#include "Models/ClipRenderService.h"
#include "Models/TrackManager.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string>

namespace {

using namespace Aestra;

int g_failures = 0;

void expect(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "[FAIL] " << what << "\n";
        ++g_failures;
    }
}

void expectNear(double actual, double expected, double tol, const std::string& what) {
    expect(std::abs(actual - expected) <= tol, what + " (got " + std::to_string(actual) + ")");
}

} // namespace

int main() {
    // Identity fit: 2 s of content into 1 bar at 120 BPM (4/4 = 2 s).
    {
        const auto fit = Audio::computeFitToBars(2.0, 120.0, 1);
        expect(fit.has_value(), "identity fit computes");
        if (fit) {
            expectNear(fit->durationBeats, 4.0, 1e-9, "1 bar = 4 beats");
            expectNear(fit->durationSeconds, 2.0, 1e-9, "1 bar @120 = 2 s");
            expect(std::abs(fit->playbackRate - 1.0f) < 1e-6, "rate 1.0 when content equals span");
            expect(!fit->rateClamped, "identity fit not clamped");
        }
    }

    // Faster: long content squeezed into a short span.
    {
        const auto fit = Audio::computeFitToBars(8.0, 120.0, 1);
        expect(fit.has_value(), "squeeze fit computes");
        if (fit) {
            expect(std::abs(fit->playbackRate - 4.0f) < 1e-6, "8 s into 2 s = 4x");
            expect(!fit->rateClamped, "4x is exactly at the bound, not clamped");
        }
    }

    // Slower: short content stretched across a long span.
    {
        const auto fit = Audio::computeFitToBars(1.0, 120.0, 2);
        expect(fit.has_value(), "stretch fit computes");
        if (fit) {
            // 2 bars @120 = 4 s; 1 s content → 0.25x.
            expect(std::abs(fit->playbackRate - 0.25f) < 1e-6, "1 s into 4 s = 0.25x");
            expect(!fit->rateClamped, "0.25x inside bounds");
        }
    }

    // Clamp high: 20 s into 1 bar @120 needs 10x — clamped to 4x, flagged.
    {
        const auto fit = Audio::computeFitToBars(20.0, 120.0, 1);
        expect(fit.has_value(), "clamped-high fit computes");
        if (fit) {
            expect(std::abs(fit->playbackRate - 4.0f) < 1e-6, "clamped to 4x");
            expect(fit->rateClamped, "clamp flagged");
            expectNear(fit->durationSeconds, 2.0, 1e-9, "span still applied when clamped");
        }
    }

    // Clamp low: 0.25 s across 8 bars @120 (16 s) needs 0.015625x.
    {
        const auto fit = Audio::computeFitToBars(0.25, 120.0, 8);
        expect(fit.has_value(), "clamped-low fit computes");
        if (fit) {
            expect(std::abs(fit->playbackRate - 0.25f) < 1e-6, "clamped to 0.25x");
            expect(fit->rateClamped, "low clamp flagged");
        }
    }

    // Tempo scaling: same content, different BPM changes the span seconds.
    {
        const auto fit = Audio::computeFitToBars(3.0, 60.0, 1);
        expect(fit.has_value(), "60 bpm fit computes");
        if (fit) {
            expectNear(fit->durationSeconds, 4.0, 1e-9, "1 bar @60 = 4 s");
            expect(std::abs(fit->playbackRate - 0.75f) < 1e-6, "3 s into 4 s = 0.75x");
        }
    }

    // Guards.
    {
        expect(!Audio::computeFitToBars(0.0, 120.0, 1).has_value(), "zero content rejected");
        expect(!Audio::computeFitToBars(-1.0, 120.0, 1).has_value(), "negative content rejected");
        expect(!Audio::computeFitToBars(2.0, 0.0, 1).has_value(), "zero bpm rejected");
        expect(!Audio::computeFitToBars(2.0, -5.0, 1).has_value(), "negative bpm rejected");
        expect(!Audio::computeFitToBars(2.0, 120.0, 0).has_value(), "zero bars rejected");
        expect(!Audio::computeFitToBars(2.0, 120.0, -2).has_value(), "negative bars rejected");
    }

    // Non-finite inputs are rejected, not clamped into a bogus fit.
    {
        const double inf = std::numeric_limits<double>::infinity();
        expect(!Audio::computeFitToBars(inf, 120.0, 1).has_value(), "infinite content rejected");
        expect(!Audio::computeFitToBars(-inf, 120.0, 1).has_value(), "negative-infinite content rejected");
        expect(!Audio::computeFitToBars(2.0, inf, 1).has_value(), "infinite bpm rejected");
        expect(!Audio::computeFitToBars(2.0, -inf, 1).has_value(), "negative-infinite bpm rejected");
        // Tiny bpm passes the >0 gate but makes the span seconds overflow.
        expect(!Audio::computeFitToBars(2.0, 1e-320, 1).has_value(), "overflowing span seconds rejected");
    }

    // Pitch-folded base rate: effective varispeed must equal the fit rate.
    {
        // +12 st: the renderer plays base x 2^(12/12), so the base must be halved.
        const auto fit12 = Audio::computeFitToBars(2.0, 120.0, 1, 12.0f);
        expect(fit12.has_value(), "pitched fit computes");
        if (fit12) {
            expect(std::abs(fit12->playbackRate - 0.5f) < 1e-6, "+12 st halves the base rate");
            Audio::ClipEdits pitched;
            pitched.playbackRate = fit12->playbackRate;
            pitched.pitchSemitones = 12.0f;
            expect(std::abs(pitched.effectiveVarispeed() - 1.0f) < 1e-6,
                   "effective varispeed equals the span-filling rate at +12 st");
            expect(!fit12->rateClamped, "+12 st fit not clamped");
        }

        const auto fitNeg12 = Audio::computeFitToBars(2.0, 120.0, 1, -12.0f);
        expect(fitNeg12.has_value(), "negative-pitch fit computes");
        if (fitNeg12) {
            expect(std::abs(fitNeg12->playbackRate - 2.0f) < 1e-6, "-12 st doubles the base rate");
            Audio::ClipEdits pitched;
            pitched.playbackRate = fitNeg12->playbackRate;
            pitched.pitchSemitones = -12.0f;
            expect(std::abs(pitched.effectiveVarispeed() - 1.0f) < 1e-6,
                   "effective varispeed equals the fit rate at -12 st");
            expect(!fitNeg12->rateClamped, "-12 st fit not clamped");
        }

        // 4x fit at +24 st: base is 1.0, effective lands exactly on 4x — the
        // envelope edge, not a clamp.
        const auto fitEdge = Audio::computeFitToBars(8.0, 120.0, 1, 24.0f);
        expect(fitEdge.has_value(), "envelope-edge fit computes");
        if (fitEdge) {
            expect(std::abs(fitEdge->playbackRate - 1.0f) < 1e-6, "4x fit at +24 st base is 1.0");
            Audio::ClipEdits clampedEdge;
            clampedEdge.playbackRate = fitEdge->playbackRate;
            clampedEdge.pitchSemitones = 24.0f;
            expect(std::abs(clampedEdge.effectiveVarispeed() - 4.0f) < 1e-6,
                   "4x fit at +24 st renders at 4x effective");
            expect(!fitEdge->rateClamped, "envelope-edge fit not clamped");
        }

        // 2.0x fit at -24 st: the needed base is 8.0, but both factors are
        // envelope-clamped — the stored base caps at 4.0 and the rendered
        // effective lands on clamp(4.0 x 0.25) = 1.0. Content cannot fill the
        // span, so rateClamped must report it.
        const auto fitCapped = Audio::computeFitToBars(4.0, 120.0, 1, -24.0f);
        expect(fitCapped.has_value(), "base-capped fit computes");
        if (fitCapped) {
            expect(std::abs(fitCapped->playbackRate - 4.0f) < 1e-6, "base capped at 4.0, never 8.0");
            Audio::ClipEdits capped;
            capped.playbackRate = fitCapped->playbackRate;
            capped.pitchSemitones = -24.0f;
            expect(std::abs(capped.effectiveVarispeed() - 1.0f) < 1e-6,
                   "rendered effective is clamp(4.0 x 0.25) = 1.0");
            expect(fitCapped->rateClamped, "unreachable base reported via rateClamped");
        }
        expect(std::abs(Audio::computeFitToBars(2.0, 120.0, 1, 0.0f)->playbackRate - 1.0f) < 1e-6,
               "0 st keeps the fit rate");
    }

    // Clip-level regression: the fit OPERATION, not just the math helper.
    // The rate edit lands BEFORE the trim in the transaction so the trim's
    // canonical durationSeconds derives from the post-fit varispeed — the #746
    // invariant (durationSeconds == beatToSeconds(durationBeats) / varispeed)
    // must survive the fit or every durationSeconds-fed path (serializer,
    // region preview, render extraction) reads a stale source window.
    {
        Audio::TrackManager tm;
        auto& playlist = tm.getPlaylistModel();
        auto& history = tm.getCommandHistory();
        const auto lane = playlist.createLane("fit");

        Audio::ClipInstance clip;
        clip.id = Audio::ClipInstanceID::generate();
        clip.name = "Fit";
        clip.startBeat = 0.0;
        clip.durationBeats = 2.0; // pre-fit span is deliberately different
        clip.edits.pitchSemitones = 12.0f; // +12 st: the renderer plays at 2x the base rate
        const auto clipId = playlist.addClip(lane, clip);
        expect(clipId.isValid(), "fit clip added");
        if (!clipId.isValid()) {
            return 1;
        }

        // 2 s of content into 1 bar @120 BPM (span 2 s): identity fit, rate 1.0.
        constexpr double contentSeconds = 2.0;
        constexpr double bpm = 120.0;
        constexpr int bars = 1;
        const auto fit = Audio::computeFitToBars(contentSeconds, bpm, bars, clip.edits.pitchSemitones);
        expect(fit.has_value(), "fit computes for the clip scenario");
        if (!fit) {
            return 1;
        }

        auto* modelClip = playlist.getClip(clipId);
        Audio::ClipEdits edits = modelClip->edits;
        edits.playbackRate = fit->playbackRate;
        history.beginTransaction(std::make_shared<Audio::CommandTransaction>("Fit clip to bars"));
        history.pushAndExecute(std::make_shared<Audio::SetClipEditsCommand>(playlist, clipId, edits));
        history.pushAndExecute(std::make_shared<Audio::TrimClipCommand>(
            playlist, clipId, -1.0, modelClip->startBeat + fit->durationBeats));
        history.commitTransaction();

        modelClip = playlist.getClip(clipId);
        expect(modelClip != nullptr, "clip still present after fit");
        if (modelClip) {
            expectNear(modelClip->startBeat + modelClip->durationBeats, fit->durationBeats, 1e-9,
                       "clip end lands on the fitted span (2 beats -> 4 beats)");
            expect(std::abs(modelClip->edits.playbackRate - 0.5f) < 1e-6,
                   "base rate halved for +12 st pitch");
            const float effective = modelClip->edits.effectiveVarispeed();
            expect(std::abs(effective - fit->playbackRate * 2.0f) < 1e-6,
                   "effective varispeed equals the span-filling fit rate");
            const double renderedSeconds = contentSeconds / static_cast<double>(effective);
            expectNear(renderedSeconds, fit->durationSeconds, 1e-9,
                       "rendered duration fills the fitted span at nonzero pitch");
        }

        // ONE undo restores edits and trim together; redo reapplies both.
        expect(history.undo(), "fit transaction undoes in one step");
        modelClip = playlist.getClip(clipId);
        if (modelClip) {
            expect(std::abs(modelClip->edits.playbackRate - 1.0f) < 1e-6, "undo restores the original rate");
            expect(std::abs(modelClip->edits.pitchSemitones - 12.0f) < 1e-6, "undo keeps the clip's pitch");
            expectNear(modelClip->startBeat + modelClip->durationBeats, 2.0, 1e-9,
                       "undo restores the original span");
        }
        expect(history.redo(), "fit transaction redoes");
        modelClip = playlist.getClip(clipId);
        if (modelClip) {
            expectNear(modelClip->startBeat + modelClip->durationBeats, fit->durationBeats, 1e-9,
                       "redo reapplies the fitted span");
            expect(std::abs(modelClip->edits.effectiveVarispeed() -
                            fit->playbackRate * std::pow(2.0f, modelClip->edits.pitchSemitones / 12.0f)) < 1e-6,
                   "redo reapplies the fitted effective varispeed");
        }
    }

    // Trimmed/offset clip at a non-1 rate: the fit must preserve the source
    // window — same offset, and a rendered span that consumes exactly the
    // pre-fit content (fit rate x fit span == pre-fit content seconds).
    {
        const double kSampleRate = 48000.0;
        Audio::TrackManager tm;
        tm.setOutputSampleRate(kSampleRate);
        auto& playlist = tm.getPlaylistModel();
        auto& history = tm.getCommandHistory();

        auto sourceBuffer = std::make_shared<Audio::AudioBufferData>();
        sourceBuffer->sampleRate = kSampleRate;
        sourceBuffer->numChannels = 1;
        sourceBuffer->numFrames = 96000; // 2 s of source
        sourceBuffer->interleavedData.assign(96000, 0.5f);

        const auto sourceId =
            tm.getSourceManager().createRecordedSource("fit-region.wav", "Fit Region", sourceBuffer);
        Audio::AudioSlicePayload payload;
        payload.audioSourceId = sourceId;
        payload.durationSeconds = 2.0;
        payload.slices.push_back({0.0, 1.0, 0.0, 96000.0});
        const auto patternId = tm.getPatternManager().createAudioPattern("Fit Region", 4.0, payload);
        const auto laneId = playlist.createLane("fit-region");
        const auto clipId = playlist.addClipFromPattern(laneId, patternId, 0.0, 2.0);

        auto* clip = playlist.getClip(clipId);
        expect(clip != nullptr, "region-fit clip created");
        if (!clip) {
            return 1;
        }

        // Shape the pre-fit clip: rate 2.0 (non-1), 0.5 s slip into the
        // source, right-edge trim to 2 beats (0.5 s of source at 2x).
        Audio::ClipEdits edits = clip->edits;
        edits.playbackRate = 2.0f;
        edits.sourceStart = 0.5 * kSampleRate;
        playlist.setClipEdits(clipId, edits);
        history.pushAndExecute(std::make_shared<Audio::TrimClipCommand>(playlist, clipId, -1.0, 0.0 + 2.0));

        clip = playlist.getClip(clipId);
        const auto preRegion = Audio::ClipRenderService(tm.getSourceManager(), tm.getPatternManager())
                                   .resolveClipRegion(*clip, tm.getPlaylistModel().getProjectSampleRate());
        expect(preRegion.frameCount > 0, "pre-fit region resolves");
        const double contentSeconds = static_cast<double>(preRegion.frameCount) / kSampleRate;

        const auto fit = Audio::computeFitToBars(contentSeconds, playlist.getBPM(), 1);
        expect(fit.has_value(), "region fit computes");
        if (!fit) {
            return 1;
        }

        // Fit exactly as the panel does: edit first, then trim.
        clip = playlist.getClip(clipId);
        Audio::ClipEdits fitEdits = clip->edits;
        fitEdits.playbackRate = fit->playbackRate;
        history.beginTransaction(std::make_shared<Audio::CommandTransaction>("Fit clip to bars"));
        history.pushAndExecute(std::make_shared<Audio::SetClipEditsCommand>(playlist, clipId, fitEdits));
        history.pushAndExecute(std::make_shared<Audio::TrimClipCommand>(
            playlist, clipId, -1.0, clip->startBeat + fit->durationBeats));
        history.commitTransaction();

        clip = playlist.getClip(clipId);
        const auto postRegion = Audio::ClipRenderService(tm.getSourceManager(), tm.getPatternManager())
                                    .resolveClipRegion(*clip, tm.getPlaylistModel().getProjectSampleRate());
        expect(postRegion.startFrame == preRegion.startFrame,
               "fit preserves the source offset (start frame)");
        // The fitted clip's region window equals the pre-fit window exactly:
        // resolveClipRegion caps at the kernel consumption (canonical x v^2 =
        // span x v), and the fit targets span' x v' == pre-fit content.
        const int64_t preFrames = static_cast<int64_t>(preRegion.frameCount);
        const int64_t postFrames = static_cast<int64_t>(postRegion.frameCount);
        expect(std::abs(postFrames - preFrames) <= 1,
               "region frame count preserved across the fit");
        // The fitted clip consumes exactly the pre-fit content: fit rate x
        // fitted span == pre-fit content seconds.
        const double consumed =
            static_cast<double>(fit->playbackRate) * fit->durationSeconds;
        expectNear(consumed, contentSeconds, 1e-9,
                   "fitted clip consumes exactly the pre-fit window");
        // Canonical invariant under the post-fit varispeed (the stale-rate
        // bug would leave pre-fit span/v instead).
        const double durationSecondsExpected =
            playlist.beatToSeconds(clip->durationBeats) /
            static_cast<double>(clip->edits.effectiveVarispeed());
        expectNear(clip->durationSeconds, durationSecondsExpected, 1e-9,
                   "canonical durationSeconds tracks the post-fit varispeed");
    }

    if (g_failures == 0) {
        std::cout << "[PASS] ClipFitToBarsTest\n";
        return 0;
    }
    std::cerr << "[FAIL] ClipFitToBarsTest: " << g_failures << " failure(s)\n";
    return 1;
}
