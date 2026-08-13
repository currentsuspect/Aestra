// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Core/MixerChannel.h"
#include "Models/TrackManager.h"

#include <stdexcept>
#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Undoable reroute of a track's main output (Routing Contract D5/D1).
 *
 * Validates the destination through TrackManager::canRouteTo before applying:
 * self-routes, audible cycles, and dangling destinations are rejected by
 * throwing, which CommandHistory catches and does not record.
 */
class SetMainOutputCommand : public ICommand {
public:
    SetMainOutputCommand(TrackManager& manager, uint32_t channelId, uint32_t targetId)
        : m_manager(manager), m_channelId(channelId), m_targetId(targetId) {}

    void execute() override {
        MixerChannel* channel = m_manager.getChannelById(m_channelId);
        if (!channel) {
            throw std::runtime_error("SetMainOutput: channel no longer exists");
        }
        if (!m_manager.canRouteTo(m_channelId, m_targetId)) {
            throw std::runtime_error("SetMainOutput: routing loop or illegal destination");
        }
        m_previousId = channel->getMainOutputId();
        channel->setMainOutputId(m_targetId);
        m_manager.requestAudioGraphRebuild(GraphDirtyReason::RoutingChanged);
    }
    void undo() override {
        if (m_previousId == 0u) {
            return;
        }
        if (auto* channel = m_manager.getChannelById(m_channelId)) {
            channel->setMainOutputId(m_previousId);
            m_manager.requestAudioGraphRebuild(GraphDirtyReason::RoutingChanged);
        }
    }
    void redo() override { execute(); }

    std::string getName() const override { return "Reroute Track"; }
    bool changesProjectState() const override { return true; }
    std::string type() const override { return "set_main_output"; }

private:
    TrackManager& m_manager;
    uint32_t m_channelId;
    uint32_t m_targetId;
    uint32_t m_previousId{0u};
};

/**
 * @brief Undoable add of a send route (Routing Contract D5/D1).
 */
class AddSendCommand : public ICommand {
public:
    AddSendCommand(TrackManager& manager, uint32_t channelId, const AudioRoute& route)
        : m_manager(manager), m_channelId(channelId), m_route(route) {}

    void execute() override {
        MixerChannel* channel = m_manager.getChannelById(m_channelId);
        if (!channel) {
            throw std::runtime_error("AddSend: channel no longer exists");
        }
        if (!m_manager.canRouteTo(m_channelId, m_route.targetChannelId)) {
            throw std::runtime_error("AddSend: routing loop or illegal destination");
        }
        m_sendIndex = static_cast<int>(channel->getSends().size());
        channel->addSend(m_route);
        m_manager.requestAudioGraphRebuild(GraphDirtyReason::RoutingChanged);
    }
    void undo() override {
        if (m_sendIndex < 0) {
            return;
        }
        if (auto* channel = m_manager.getChannelById(m_channelId)) {
            channel->removeSend(m_sendIndex);
            m_manager.requestAudioGraphRebuild(GraphDirtyReason::RoutingChanged);
        }
    }
    void redo() override { execute(); }

    std::string getName() const override { return "Add Send"; }
    bool changesProjectState() const override { return true; }
    std::string type() const override { return "add_send"; }

private:
    TrackManager& m_manager;
    uint32_t m_channelId;
    AudioRoute m_route;
    int m_sendIndex{-1};
};

/**
 * @brief Undoable removal of a send route (Routing Contract D5).
 *
 * Captures the removed route so undo restores the exact send at its
 * original index.
 */
class RemoveSendCommand : public ICommand {
public:
    RemoveSendCommand(TrackManager& manager, uint32_t channelId, int sendIndex)
        : m_manager(manager), m_channelId(channelId), m_sendIndex(sendIndex) {}

    void execute() override {
        MixerChannel* channel = m_manager.getChannelById(m_channelId);
        if (!channel) {
            throw std::runtime_error("RemoveSend: channel no longer exists");
        }
        const auto sends = channel->getSends();
        if (m_sendIndex < 0 || m_sendIndex >= static_cast<int>(sends.size())) {
            throw std::runtime_error("RemoveSend: send index out of range");
        }
        m_removedRoute = sends[static_cast<size_t>(m_sendIndex)];
        channel->removeSend(m_sendIndex);
        m_manager.requestAudioGraphRebuild(GraphDirtyReason::RoutingChanged);
    }
    void undo() override {
        if (auto* channel = m_manager.getChannelById(m_channelId)) {
            channel->insertSend(m_sendIndex, m_removedRoute);
            m_manager.requestAudioGraphRebuild(GraphDirtyReason::RoutingChanged);
        }
    }
    void redo() override { execute(); }

    std::string getName() const override { return "Remove Send"; }
    bool changesProjectState() const override { return true; }
    std::string type() const override { return "remove_send"; }

private:
    TrackManager& m_manager;
    uint32_t m_channelId;
    int m_sendIndex;
    AudioRoute m_removedRoute;
};

/**
 * @brief Undoable edit of one send route (Routing Contract D5/D1).
 *
 * Applies the full replacement route. Destination changes are validated
 * through TrackManager::canRouteTo; level/pan/tap/mute/sidechain edits
 * cannot create topology, so they skip the walk.
 */
class EditSendCommand : public ICommand {
public:
    EditSendCommand(TrackManager& manager, uint32_t channelId, int sendIndex, const AudioRoute& newRoute)
        : m_manager(manager), m_channelId(channelId), m_sendIndex(sendIndex), m_newRoute(newRoute) {}

    void execute() override {
        MixerChannel* channel = m_manager.getChannelById(m_channelId);
        if (!channel) {
            throw std::runtime_error("EditSend: channel no longer exists");
        }
        const auto sends = channel->getSends();
        if (m_sendIndex < 0 || m_sendIndex >= static_cast<int>(sends.size())) {
            throw std::runtime_error("EditSend: send index out of range");
        }
        m_previousRoute = sends[static_cast<size_t>(m_sendIndex)];
        if (m_previousRoute.targetChannelId != m_newRoute.targetChannelId &&
            !m_manager.canRouteTo(m_channelId, m_newRoute.targetChannelId)) {
            throw std::runtime_error("EditSend: routing loop or illegal destination");
        }
        channel->setSend(m_sendIndex, m_newRoute);
        if (m_previousRoute.targetChannelId != m_newRoute.targetChannelId) {
            m_manager.requestAudioGraphRebuild(GraphDirtyReason::RoutingChanged);
        }
    }
    void undo() override {
        if (auto* channel = m_manager.getChannelById(m_channelId)) {
            channel->setSend(m_sendIndex, m_previousRoute);
            if (m_previousRoute.targetChannelId != m_newRoute.targetChannelId) {
                m_manager.requestAudioGraphRebuild(GraphDirtyReason::RoutingChanged);
            }
        }
    }
    void redo() override { execute(); }

    std::string getName() const override { return "Edit Send"; }
    bool changesProjectState() const override { return true; }
    std::string type() const override { return "edit_send"; }

private:
    TrackManager& m_manager;
    uint32_t m_channelId;
    int m_sendIndex;
    AudioRoute m_newRoute;
    AudioRoute m_previousRoute;
};

} // namespace Audio
} // namespace Aestra
