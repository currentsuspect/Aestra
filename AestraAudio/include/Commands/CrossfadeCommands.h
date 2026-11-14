// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/SetClipEditsCommand.h"
#include "Models/CrossfadeResolve.h"
#include "Models/PatternManager.h"
#include "Models/PlaylistModel.h"

#include <memory>
#include <string>
#include <variant>

namespace Aestra {
namespace Audio {

/**
 * @brief The edits behind Crossfade, Cut it here and the Smooth/Straight choice (V83-E10, FD-94).
 *
 * Each returns ONE undoable command (null, with a reason, when it would do nothing), built on the existing
 * SetClipEditsCommand: a crossfade is only a link stored on the later clip, so linking, unlinking and reshaping
 * are all a change to that clip's edits.
 */

/** @brief Straight when both clips are cuts of the same recording (their sum stays level), Smooth otherwise. */
inline FadeShape defaultCrossfadeShape(PatternManager& patterns, const ClipInstance& a, const ClipInstance& b) {
    const PatternSource* pa = patterns.getPattern(a.patternId);
    const PatternSource* pb = patterns.getPattern(b.patternId);
    if (pa && pb && pa->isAudio() && pb->isAudio() &&
        std::get<AudioSlicePayload>(pa->payload).audioSourceId == std::get<AudioSlicePayload>(pb->payload).audioSourceId)
        return FadeShape::Linear;
    return FadeShape::EqualPower;
}

/** @brief Crossfade two overlapping audio clips on one lane. Shape: pass nothing to take the same-recording guess. */
inline std::shared_ptr<SetClipEditsCommand> makeCrossfadeCommand(PlaylistModel& model, PatternManager& patterns,
                                                                 const ClipInstanceID& earlierId,
                                                                 const ClipInstanceID& laterId, std::string* why,
                                                                 const FadeShape* shape = nullptr) {
    const auto refuse = [why](const char* text) {
        if (why)
            *why = text;
        return std::shared_ptr<SetClipEditsCommand>{};
    };
    const ClipInstance* earlier = model.getClip(earlierId);
    const ClipInstance* later = model.getClip(laterId);
    if (!earlier || !later)
        return refuse("One of the clips is gone.");
    if (!model.isAudioClip(*earlier) || !model.isAudioClip(*later))
        return refuse("Crossfades work between audio clips.");
    if (model.findClipLane(earlierId) != model.findClipLane(laterId))
        return refuse("The clips are on different tracks.");
    if (crossfadeOverlapBeats(*earlier, *later) <= 0.0)
        return refuse("The clips do not overlap.");
    if (later->edits.crossfadeFrom == earlierId && later->edits.crossfadeShape == (shape ? *shape : later->edits.crossfadeShape))
        return refuse("These clips already crossfade.");
    ClipEdits edits = later->edits;
    edits.crossfadeFrom = earlierId;
    edits.crossfadeShape = shape ? *shape : defaultCrossfadeShape(patterns, *earlier, *later);
    auto command = std::make_shared<SetClipEditsCommand>(model, laterId, edits);
    command->named("Crossfade");
    return command;
}

/** @brief Cut it here: take the crossfade away; both clips go back to their own fades and play as plain overlap. */
inline std::shared_ptr<SetClipEditsCommand> makeCutCrossfadeCommand(PlaylistModel& model, const ClipInstanceID& laterId,
                                                                    std::string* why) {
    const ClipInstance* later = model.getClip(laterId);
    if (!later || !later->edits.crossfadeFrom.isValid()) {
        if (why)
            *why = "There is no crossfade here.";
        return nullptr;
    }
    ClipEdits edits = later->edits;
    edits.crossfadeFrom = ClipInstanceID{};
    auto command = std::make_shared<SetClipEditsCommand>(model, laterId, edits);
    command->named("Cut Crossfade");
    return command;
}

/** @brief Smooth or Straight for an existing crossfade. */
inline std::shared_ptr<SetClipEditsCommand> makeCrossfadeShapeCommand(PlaylistModel& model, const ClipInstanceID& laterId,
                                                                      FadeShape shape, std::string* why) {
    const ClipInstance* later = model.getClip(laterId);
    if (!later || !later->edits.crossfadeFrom.isValid()) {
        if (why)
            *why = "There is no crossfade here.";
        return nullptr;
    }
    if (later->edits.crossfadeShape == shape) {
        if (why)
            *why = "It already has that shape.";
        return nullptr;
    }
    ClipEdits edits = later->edits;
    edits.crossfadeShape = shape;
    auto command = std::make_shared<SetClipEditsCommand>(model, laterId, edits);
    command->named("Crossfade Shape");
    return command;
}

} // namespace Audio
} // namespace Aestra
