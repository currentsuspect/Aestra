// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Commands/DeleteSilenceCommand.h"

#include "Commands/MoveClipCommand.h"
#include "Commands/RemoveClipCommand.h"
#include "Commands/SetClipEditsCommand.h"
#include "Commands/SplitClipCommand.h"
#include "Models/ClipRenderService.h"
#include "Models/TrackManager.h"

#include <algorithm>
#include <cmath>

namespace Aestra {
namespace Audio {

namespace {

/// Beat positions closer than this are the same position (a split at an edge would be refused).
constexpr double kBeatEps = 1e-9;

/// A cut in absolute timeline beats.
struct BeatRange {
    double start;
    double end;
};

} // namespace

DeleteSilenceCommand::DeleteSilenceCommand(TrackManager& manager, std::vector<ClipInstanceID> clips,
                                           SilenceRipple ripple, SilenceParams params)
    : m_manager(manager), m_clips(std::move(clips)), m_ripple(ripple), m_params(params) {}

void DeleteSilenceCommand::execute() {
    if (m_executed) {
        redo();
        return;
    }
    m_executed = true;

    // Latest clip first: a cut only moves clips that start after it, so the clips still to be
    // measured, which start earlier, are never shifted under us.
    auto& playlist = m_manager.getPlaylistModel();
    std::vector<std::pair<double, ClipInstanceID>> order;
    for (const auto& id : m_clips) {
        if (const ClipInstance* clip = playlist.getClip(id))
            order.emplace_back(clip->startBeat, id);
    }
    std::stable_sort(order.begin(), order.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    order.erase(std::unique(order.begin(), order.end(),
                            [](const auto& a, const auto& b) { return a.second == b.second; }),
                order.end());
    for (const auto& entry : order)
        cutClip(entry.second);
}

void DeleteSilenceCommand::cutClip(const ClipInstanceID& clipId) {
    auto& playlist = m_manager.getPlaylistModel();
    const ClipInstance* clip = playlist.getClip(clipId);
    if (!clip)
        return;

    // The audio the clip plays now, trims and slips applied: pattern and MIDI clips resolve to nothing.
    ClipRenderService service(m_manager.getSourceManager(), m_manager.getPatternManager());
    const auto region = service.resolveClipRegion(*clip, playlist.getProjectSampleRate());
    if (!region.isValid())
        return;
    const auto ranges = SilenceDetector::findRemovableRanges(*region.buffer, region.startFrame, region.frameCount, m_params);
    if (ranges.empty())
        return;

    // Source seconds to timeline beats. A varispeed clip plays source audio v times faster, so a
    // stretch of source takes 1/v of the time on the timeline (the #746 canonical).
    const double sourceRate = static_cast<double>(region.buffer->sampleRate);
    const double varispeed = static_cast<double>(clip->edits.effectiveVarispeed());
    const auto frameToBeat = [&](uint64_t frame) {
        return playlist.secondsToBeats(static_cast<double>(frame) / sourceRate / varispeed);
    };
    const double clipStart = clip->startBeat;
    const double fadeBeats = playlist.secondsToBeats(std::max(0.0, m_params.fadeSec));
    const PlaylistLaneID laneId = playlist.findClipLane(clipId);

    // Beat ranges, clamped to the clip so rounding at its edges never asks for a split outside it.
    std::vector<BeatRange> cuts;
    for (const auto& r : ranges) {
        BeatRange b{clipStart + frameToBeat(r.startFrame), clipStart + frameToBeat(r.endFrame)};
        b.start = std::max(b.start, clipStart);
        b.end = std::min(b.end, clip->endBeat());
        if (b.end - b.start > kBeatEps)
            cuts.push_back(b);
    }

    // Right to left, so the cuts still to come sit where they were measured. The clip's own id always
    // stays the left-most piece: splitting only ever hands the right-hand part a new id.
    for (auto it = cuts.rbegin(); it != cuts.rend(); ++it) {
        const double a = it->start;
        const double b = it->end;
        const ClipInstance* current = playlist.getClip(clipId);
        if (!current)
            return;
        const double curStart = current->startBeat;
        const double curEnd = current->endBeat();
        if (a > curEnd - kBeatEps || b < curStart + kBeatEps)
            continue;

        // 1. The piece after the cut (if the cut stops short of the clip's end).
        ClipInstanceID rightId;
        if (b < curEnd - kBeatEps) {
            auto split = std::make_shared<SplitClipCommand>(playlist, clipId, b);
            split->execute();
            m_macro.addCommand(split);
            rightId = split->newClipId();
        }

        // 2. The piece inside the cut (if the cut starts after the clip's start): it is removed.
        ClipInstanceID middleId = clipId;
        if (a > curStart + kBeatEps) {
            auto split = std::make_shared<SplitClipCommand>(playlist, clipId, a);
            split->execute();
            m_macro.addCommand(split);
            middleId = split->newClipId();
        }
        if (!middleId.isValid())
            continue;
        auto remove = std::make_shared<RemoveClipCommand>(playlist, middleId);
        remove->execute();
        m_macro.addCommand(remove);

        // 3. Close the gap: everything that starts at or after the cut's end moves left by its length.
        const double length = b - a;
        struct Mover {
            ClipInstanceID id;
            double start;
        };
        std::vector<Mover> movers;
        playlist.withLanes([&](const std::vector<PlaylistLane>& lanes) {
            for (const auto& lane : lanes) {
                if (m_ripple == SilenceRipple::ThisLane && !(lane.id == laneId))
                    continue;
                for (const auto& other : lane.clips) {
                    if (other.startBeat >= b - 1e-6)
                        movers.push_back({other.id, other.startBeat});
                }
            }
            return 0;
        });
        for (const auto& mover : movers) {
            auto move = std::make_shared<MoveClipCommand>(playlist, mover.id, mover.start - length);
            move->execute();
            m_macro.addCommand(move);
        }

        // 4. Short fades on the two new edges, so a cut never clicks. Clamped to half the piece.
        const auto fade = [&](const ClipInstanceID& id, bool fadeIn) {
            const ClipInstance* piece = playlist.getClip(id);
            if (!piece || fadeBeats <= 0.0)
                return;
            ClipEdits edits = piece->edits;
            const float beats = static_cast<float>(std::min(fadeBeats, piece->durationBeats * 0.5));
            (fadeIn ? edits.fadeInBeats : edits.fadeOutBeats) = beats;
            auto set = std::make_shared<SetClipEditsCommand>(playlist, id, edits);
            set->execute();
            m_macro.addCommand(set);
        };
        if (middleId != clipId)
            fade(clipId, false); // the piece left of the cut fades out into it
        if (rightId.isValid())
            fade(rightId, true); // the piece right of the cut fades in out of it

        m_removedSeconds += playlist.beatToSeconds(length);
        ++m_cuts;
    }
}

} // namespace Audio
} // namespace Aestra
