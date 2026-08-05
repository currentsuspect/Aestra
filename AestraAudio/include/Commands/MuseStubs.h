#pragma once

#include "Commands/ICommand.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"

#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

class SetBpmCommand : public ICommand {
public:
    SetBpmCommand(AudioEngine& engine, float bpm);
    void execute() override;
    void undo() override;
    void redo() override;
    std::string getName() const override;
    std::string type() const override { return "set_bpm"; }
    bool changesProjectState() const override { return true; }
private:
    AudioEngine& m_engine;
    float m_bpm;
    float m_previousBpm = 120.0f;
    bool m_executed = false;
};

class PlayCommand : public ICommand {
public:
    explicit PlayCommand(AudioEngine& engine);
    void execute() override;
    void undo() override;
    void redo() override;
    std::string getName() const override;
    std::string type() const override { return "play"; }
private:
    AudioEngine& m_engine;
    bool m_wasPlaying = false;
    bool m_executed = false;
};

class StopCommand : public ICommand {
public:
    explicit StopCommand(AudioEngine& engine);
    void execute() override;
    void undo() override;
    void redo() override;
    std::string getName() const override;
    std::string type() const override { return "stop"; }
private:
    AudioEngine& m_engine;
    bool m_wasPlaying = false;
    bool m_executed = false;
};

class DeleteTrackCommand : public ICommand {
public:
    DeleteTrackCommand(TrackManager& manager, int trackIndex);
    void execute() override;
    void undo() override;
    void redo() override;
    std::string getName() const override;
    std::string type() const override { return "delete_track"; }
    bool changesProjectState() const override { return true; }
private:
    TrackManager& m_manager;
    int m_trackIndex;
    std::string m_deletedName;
    uint32_t m_deletedId = 0;
    std::vector<UnitID> m_routedUnits;
    std::vector<PatternID> m_routedAudioPatterns;
    struct MixerRouteSnapshot {
        uint32_t sourceChannelId{0};
        uint32_t mainOutputId{0xFFFFFFFFu};
        std::vector<AudioRoute> sends;
    };
    std::vector<MixerRouteSnapshot> m_mixerRoutes;
    bool m_routesCaptured = false;
    bool m_executed = false;
    /**
     * The removed channel itself, held alive while the delete stands.
     *
     * Undo must restore this object, not an equivalent one: its channel id is
     * what routing points at, its volume/pan/mute/solo and effect chain live
     * inside it, and older commands in the undo history hold `MixerChannel&`
     * to this exact address. Re-creating it lost all three and left those
     * references dangling (#611).
     */
    std::unique_ptr<MixerChannel> m_detached;
    size_t m_detachedIndex = 0;
};

class RenameTrackCommand : public ICommand {
public:
    RenameTrackCommand(TrackManager& manager, int trackIndex, const std::string& name);
    void execute() override;
    void undo() override;
    void redo() override;
    std::string getName() const override;
    std::string type() const override { return "rename_track"; }
    bool changesProjectState() const override { return true; }
private:
    TrackManager& m_manager;
    int m_trackIndex;
    std::string m_newName;
    std::string m_oldName;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
