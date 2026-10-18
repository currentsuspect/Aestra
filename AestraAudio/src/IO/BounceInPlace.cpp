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

// Every mixer destination a timeline clip's pattern plays through, each once: an audio pattern's own, a MIDI
// pattern's units' (MASTER_MIXER_CHANNEL_ID for a unit routed straight to Master). False, with a reason, for a
// pattern that cannot be bounced.
bool patternChannels(const TrackManager& trackManager, const PatternSource& pattern, std::vector<uint32_t>& out,
                     std::string* why) {
    const auto add = [&out](uint32_t c) { if (std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c); };
    if (pattern.isAudio()) {
        add(pattern.getMixerChannelId());
        return true;
    }
    if (!pattern.isMidi()) {
        if (why)
            *why = "Bounce in Place works on audio and pattern clips.";
        return false;
    }
    const auto& units = trackManager.getUnitManager();
    bool any = false;
    for (const auto& note : pattern.getMidiNotes()) {
        const auto* unit = units.getUnit(static_cast<UnitID>(note.unitId));
        if (!unit) {
            if (why)
                *why = "This pattern plays an instrument that no longer exists.";
            return false;
        }
        add(unit->targetMixerChannelId);
        any = true;
    }
    if (!any && why)
        *why = "This pattern has no notes to bounce.";
    return any;
}

bool overlaps(double aStart, double aEnd, double bStart, double bEnd) {
    return aStart < bEnd && bStart < aEnd;
}

bool shares(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    return std::any_of(a.begin(), a.end(), [&b](uint32_t c) { return std::find(b.begin(), b.end(), c) != b.end(); });
}

} // namespace

BounceInPlaceResult bounceClipsInPlace(AudioEngine& engine, TrackManager& trackManager,
                                       const std::vector<ClipInstanceID>& clipIds) {
    auto& playlist = trackManager.getPlaylistModel();
    if (clipIds.empty())
        return refuse("Select the clips to bounce first.");

    std::vector<uint32_t> channels;
    double spanStart = 0.0;
    double spanEnd = 0.0;
    bool first = true;
    for (const auto& id : clipIds) {
        const ClipInstance* clip = playlist.getClip(id);
        const PatternSource* pattern = clip ? trackManager.getPatternManager().getPattern(clip->patternId) : nullptr;
        if (!clip || !pattern)
            return refuse("Bounce in Place works on audio and pattern clips.");
        std::string why;
        if (!patternChannels(trackManager, *pattern, channels, &why))
            return refuse(why);
        spanStart = first ? clip->startBeat : std::min(spanStart, clip->startBeat);
        spanEnd = first ? clip->endBeat() : std::max(spanEnd, clip->endBeat());
        first = false;
    }
    if (!(spanEnd > spanStart))
        return refuse("The selected clips have no length to bounce.");
    for (const uint32_t c : channels)
        if (c != MASTER_MIXER_CHANNEL_ID && !trackManager.getChannelById(c))
            return refuse("The clips' track no longer exists.");

    // One track: the bounce goes back onto that track, rendered before its fader (which stays live) with its
    // inserts bypassed afterwards. Several tracks, or Master directly (owner ruling 2026-10-15): one clip that
    // sounds the same, rendered after each track's fader and pan and summed, placed on Master, so it passes
    // through none of those tracks and nothing is applied twice.
    const bool toMaster = channels.size() != 1 || channels.front() == MASTER_MIXER_CHANNEL_ID;

    // Anything else playing through the same tracks across the span would be baked into the bounce and then
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
            std::vector<uint32_t> otherChannels;
            if (otherPattern && patternChannels(trackManager, *otherPattern, otherChannels, nullptr) &&
                shares(otherChannels, channels))
                return refuse(toMaster ? "Other clips play through the same tracks during this stretch. Select them "
                                         "too, or move them away, so nothing plays twice."
                                       : "Other clips on this track play during the same stretch. Select them too, or "
                                         "move them away, so nothing plays twice.");
        }
    }
    AudioExporter::Config config;
    config.scope = AudioExporter::RenderScope::FullSong;
    config.startBeat = spanStart;
    config.endBeat = spanEnd;
    config.sampleRate = static_cast<uint32_t>(std::max(1.0, playlist.getProjectSampleRate()));
    config.numChannels = 2;
    config.tailSeconds = 0.0; // the unit is the clips' span (owner ruling 2026-10-11)

    // Pattern clips reach their instruments through the pattern scheduler, which only timeline PLAY fills.
    // Schedule the timeline into it for each render without touching the transport, and take it out again
    // afterwards so nothing leaks into the next play.
    auto& scheduler = trackManager.getPatternPlaybackEngine();
    struct SchedulerGuard {
        PatternPlaybackEngine& scheduler;
        ~SchedulerGuard() { scheduler.clearScheduledInstances(); }
    } schedulerGuard{scheduler};
    const double startSeconds = spanStart * 60.0 / std::max(1.0, playlist.getBPM());

    // One render per destination: a track's own output (before its fader for one track, after it for several),
    // or for Master what the direct sources add. Several are summed.
    std::vector<float> mixed;
    uint64_t frames = 0;
    uint32_t rate = 0;
    for (const uint32_t c : toMaster ? channels : std::vector<uint32_t>{channels.front()}) {
        config.captureMixerChannelId = c == MASTER_MIXER_CHANNEL_ID ? TrackOutputTap::kMasterDirect : c;
        config.captureAfterFader = toMaster;
        scheduler.clearScheduledInstances();
        trackManager.scheduleTimelineForOfflineRender(startSeconds);
        AudioExporter exporter(engine, trackManager);
        AudioExporter::RenderedAudio rendered;
        const auto result = exporter.renderToBuffer(config, rendered);
        if (!result.ok() || rendered.frames == 0)
            return refuse(result.errorMessage.empty() ? "The render produced no audio." : result.errorMessage);
        if (mixed.empty()) {
            mixed = std::move(rendered.interleaved);
            frames = rendered.frames;
            rate = rendered.sampleRate;
        } else {
            frames = std::min<uint64_t>(frames, rendered.frames);
            mixed.resize(static_cast<size_t>(frames) * 2u);
            for (size_t i = 0; i < mixed.size(); ++i)
                mixed[i] += rendered.interleaved[i];
        }
    }

    auto buffer = std::make_shared<AudioBufferData>();
    buffer->interleavedData = std::move(mixed);
    buffer->sampleRate = rate;
    buffer->numChannels = 2;
    buffer->numFrames = frames;

    auto command = std::make_shared<BounceInPlaceCommand>(trackManager, clipIds,
                                                          toMaster ? MASTER_MIXER_CHANNEL_ID : channels.front(),
                                                          std::move(buffer), spanStart, spanEnd - spanStart);
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
