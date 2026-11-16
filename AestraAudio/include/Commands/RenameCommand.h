// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/TrackManager.h"

#include <memory>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Rename a track or a mixer channel as one undo step.
 *
 * A track's header shows its first lane's name, and its mixer strip shows its channel's name. When the two were the
 * same, renaming one renames the other too, so the timeline and the mixer keep agreeing; a channel the producer gave a
 * different name keeps it. Names are trimmed and capped; an empty or unchanged name is a no-op (empty()).
 */
class RenameCommand : public ICommand {
public:
    static constexpr size_t kMaxNameBytes = 64;

    static std::string cleaned(std::string name) {
        const auto first = name.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return {};
        name = name.substr(first, name.find_last_not_of(" \t\r\n") - first + 1);
        if (name.size() > kMaxNameBytes) {
            size_t cut = kMaxNameBytes;
            while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80) --cut; // whole UTF-8 characters
            name.resize(cut);
        }
        return name;
    }

    /// Rename the row whose lane is @p rowLane (any lane of the track).
    static std::shared_ptr<RenameCommand> forTrack(TrackManager& tm, PlaylistLaneID rowLane, const std::string& newName) {
        auto cmd = std::shared_ptr<RenameCommand>(new RenameCommand(tm, cleaned(newName), "Rename Track"));
        auto& playlist = tm.getPlaylistModel();
        const auto* lane = playlist.getLane(rowLane);
        if (!lane || cmd->m_name.empty()) return cmd;
        const Track* track = lane->trackId != 0 ? tm.getTrack(lane->trackId) : nullptr;
        const PlaylistLaneID head = track && !track->laneIds.empty() ? track->laneIds.front() : rowLane;
        const auto* headLane = playlist.getLane(head);
        const std::string old = headLane ? headLane->name : lane->name;
        if (old == cmd->m_name) return cmd;
        cmd->m_edits.push_back({Edit::Lane, 0, head, 0, old});
        if (track) {
            cmd->m_edits.push_back({Edit::TrackName, track->trackId, {}, 0, track->name});
            if (const auto* ch = tm.getChannelById(track->channelId); ch && track->channelId != 0 &&
                                                                      (ch->getName() == old || ch->getName() == track->name))
                cmd->m_edits.push_back({Edit::Channel, 0, {}, track->channelId, ch->getName()});
        }
        return cmd;
    }

    /// Rename mixer channel @p channelId; tracks routed to it that carried its name follow.
    static std::shared_ptr<RenameCommand> forChannel(TrackManager& tm, uint32_t channelId, const std::string& newName) {
        auto cmd = std::shared_ptr<RenameCommand>(new RenameCommand(tm, cleaned(newName), "Rename Channel"));
        const auto* ch = tm.getChannelById(channelId);
        if (!ch || channelId == 0 || cmd->m_name.empty() || ch->getName() == cmd->m_name) return cmd;
        const std::string old = ch->getName();
        cmd->m_edits.push_back({Edit::Channel, 0, {}, channelId, old});
        for (const Track* track : tm.getTracks()) {
            if (!track || track->channelId != channelId || track->laneIds.empty()) continue;
            const auto* head = tm.getPlaylistModel().getLane(track->laneIds.front());
            if (!head || (head->name != old && track->name != old)) continue;
            cmd->m_edits.push_back({Edit::Lane, 0, track->laneIds.front(), 0, head->name});
            cmd->m_edits.push_back({Edit::TrackName, track->trackId, {}, 0, track->name});
        }
        return cmd;
    }

    bool empty() const { return m_edits.empty(); }
    void execute() override { apply(true); }
    void undo() override { apply(false); }
    void redo() override { apply(true); }
    std::string getName() const override { return m_label; }
    bool changesProjectState() const override { return !m_edits.empty(); }

private:
    struct Edit {
        enum Kind { Lane, TrackName, Channel } kind;
        uint64_t trackId;
        PlaylistLaneID laneId;
        uint32_t channelId;
        std::string before;
    };

    RenameCommand(TrackManager& tm, std::string name, std::string label)
        : m_tm(tm), m_name(std::move(name)), m_label(std::move(label)) {}

    void apply(bool forward) {
        for (const auto& e : m_edits) {
            const std::string& to = forward ? m_name : e.before;
            switch (e.kind) {
            case Edit::Lane:
                if (auto* lane = m_tm.getPlaylistModel().getLane(e.laneId)) lane->name = to;
                break;
            case Edit::TrackName:
                if (auto* track = m_tm.getTrack(e.trackId)) track->name = to;
                break;
            case Edit::Channel:
                if (auto* ch = m_tm.getChannelById(e.channelId)) ch->setName(to);
                break;
            }
        }
    }

    TrackManager& m_tm;
    std::string m_name;
    std::string m_label;
    std::vector<Edit> m_edits;
};

} // namespace Audio
} // namespace Aestra
