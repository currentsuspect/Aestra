// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/ClipInstance.h"
#include "Models/FrozenTrackState.h"
#include "Models/PatternSource.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Aestra {
namespace Audio {

class TrackManager;

/**
 * @brief Thaw a frozen track (V83-E11): the reverse of BounceInPlaceCommand's freeze mode.
 *
 * Reads the FrozenTrackState saved on the channel, never the undo stack, so it works on a project that was saved
 * and reopened. Removes the rendered clip, unmutes the source clips, brings back the inserts and their automation,
 * and clears the state. Only what freeze switched is switched back. One undo step; undo freezes again with the
 * same rendered file.
 */
class ThawTrackCommand final : public ICommand {
public:
    ThawTrackCommand(TrackManager& manager, uint32_t mixerChannelId);

    void execute() override;
    void undo() override;
    void redo() override { execute(); }

    std::string getName() const override { return "Unfreeze Track"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }
    bool isUndoable() const override { return m_executed; }

private:
    TrackManager& m_manager;
    uint32_t m_channelId;
    FrozenTrackState m_state;
    ClipInstance m_frozenClip;
    PlaylistLaneID m_frozenLane;
    bool m_hadFrozenClip{false};
    std::unique_ptr<PatternSource> m_detachedPattern;
    bool m_executed{false};
};

} // namespace Audio
} // namespace Aestra
