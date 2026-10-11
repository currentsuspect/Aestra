// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "IO/BounceInPlace.h"

#include "Commands/BounceInPlaceCommand.h"
#include "IO/AudioExporter.h"
#include "Models/TrackManager.h"

#include <algorithm>
#include <memory>

namespace Aestra {
namespace Audio {

namespace {

BounceInPlaceResult refuse(std::string why) {
    BounceInPlaceResult r;
    r.error = std::move(why);
    return r;
}

bool overlaps(double aStart, double aEnd, double bStart, double bEnd) {
    return aStart < bEnd && bStart < aEnd;
}

} // namespace

BounceInPlaceResult bounceClipsInPlace(AudioEngine& engine, TrackManager& trackManager,
                                       const std::vector<ClipInstanceID>& clipIds) {
    auto& playlist = trackManager.getPlaylistModel();
    if (clipIds.empty())
        return refuse("Select the audio clips to bounce first.");

    uint32_t channelId = 0;
    double spanStart = 0.0;
    double spanEnd = 0.0;
    bool first = true;
    for (const auto& id : clipIds) {
        const ClipInstance* clip = playlist.getClip(id);
        const PatternSource* pattern = clip ? trackManager.getPatternManager().getPattern(clip->patternId) : nullptr;
        if (!clip || !pattern || !pattern->isAudio())
            return refuse("Bounce in Place works on audio clips.");
        const uint32_t clipChannel = pattern->getMixerChannelId();
        if (clipChannel == 0)
            return refuse("This clip goes straight to the master. Route it to a track first, then bounce.");
        if (!first && clipChannel != channelId)
            return refuse("The selected clips are on different tracks. Bounce one track at a time.");
        channelId = clipChannel;
        spanStart = first ? clip->startBeat : std::min(spanStart, clip->startBeat);
        spanEnd = first ? clip->endBeat() : std::max(spanEnd, clip->endBeat());
        first = false;
    }
    if (!(spanEnd > spanStart))
        return refuse("The selected clips have no length to bounce.");
    if (!trackManager.getChannelById(channelId))
        return refuse("The clips' track no longer exists.");

    // Anything else this track plays across the span would be baked into the bounce and then
    // play again from its own source.
    const auto isSelected = [&clipIds](const ClipInstanceID& id) {
        return std::find(clipIds.begin(), clipIds.end(), id) != clipIds.end();
    };
    for (size_t l = 0; l < playlist.getLaneCount(); ++l) {
        const PlaylistLane* lane = playlist.getLane(playlist.getLaneId(l));
        if (!lane)
            continue;
        for (const auto& other : lane->clips) {
            if (isSelected(other.id) || !overlaps(spanStart, spanEnd, other.startBeat, other.endBeat()))
                continue;
            const PatternSource* otherPattern = trackManager.getPatternManager().getPattern(other.patternId);
            if (otherPattern && otherPattern->getMixerChannelId() == channelId)
                return refuse("Other clips on this track play during the same stretch. Select them too, or "
                              "move them away, so nothing plays twice.");
        }
    }
    const auto& units = trackManager.getUnitManager();
    for (const UnitID unitId : units.getAllUnitIDs()) {
        const auto* unit = units.getUnit(unitId);
        if (unit && unit->targetMixerChannelId == channelId)
            return refuse("This track has an instrument. Bounce for instrument tracks is not built yet.");
    }

    AudioExporter::Config config;
    config.scope = AudioExporter::RenderScope::FullSong;
    config.startBeat = spanStart;
    config.endBeat = spanEnd;
    config.sampleRate = static_cast<uint32_t>(std::max(1.0, playlist.getProjectSampleRate()));
    config.numChannels = 2;
    config.tailSeconds = 0.0; // the unit is the clips' span (owner ruling 2026-10-11)
    config.captureMixerChannelId = channelId;

    AudioExporter exporter(engine, trackManager);
    AudioExporter::RenderedAudio rendered;
    const auto result = exporter.renderToBuffer(config, rendered);
    if (!result.ok() || rendered.frames == 0)
        return refuse(result.errorMessage.empty() ? "The render produced no audio." : result.errorMessage);

    auto buffer = std::make_shared<AudioBufferData>();
    buffer->interleavedData = std::move(rendered.interleaved);
    buffer->sampleRate = rendered.sampleRate;
    buffer->numChannels = rendered.numChannels;
    buffer->numFrames = rendered.frames;

    auto command = std::make_shared<BounceInPlaceCommand>(trackManager, clipIds, channelId, std::move(buffer),
                                                          spanStart, spanEnd - spanStart);
    trackManager.getCommandHistory().pushAndExecute(command);
    if (!command->isUndoable())
        return refuse("The bounced audio could not be saved.");

    BounceInPlaceResult done;
    done.ok = true;
    done.clip = command->bouncedClipId();
    return done;
}

} // namespace Audio
} // namespace Aestra
