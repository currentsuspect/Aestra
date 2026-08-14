// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Core/MixerChannel.h"
#include "Models/TrackManager.h"

#include <stdexcept>
#include <string>

namespace Aestra {
namespace Audio {

// Engine-space master constant (Contract: 0 = model space, 0xFFFFFFFF = engine
// space; both name the same terminal sink).
constexpr uint32_t kMasterId = 0xFFFFFFFFu;

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
        if (m_route.targetChannelId == kMasterId) {
            throw std::runtime_error("AddSend: sends to master are illegal (Contract D4)");
        }
        if (!m_manager.canRouteTo(m_channelId, m_route.targetChannelId)) {
            throw std::runtime_error("AddSend: routing loop or illegal destination");
        }
        channel->addSend(m_route);
        // Capture the minted stable id so undo removes the right send even
        // after other sends were added/removed around it, and redo restores
        // the SAME id (round trip, not a new identity wearing the old slot).
        const auto sends = channel->getSends();
        m_sendId = sends.empty() ? 0u : sends.back().sendId;
        m_route.sendId = m_sendId;
        m_manager.requestAudioGraphRebuild(GraphDirtyReason::RoutingChanged);
    }
    void undo() override {
        if (m_sendId == 0u) {
            return;
        }
        if (auto* channel = m_manager.getChannelById(m_channelId)) {
            channel->removeSend(m_sendId);
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
    uint64_t m_sendId{0};
};

/**
 * @brief Undoable removal of a send route (Routing Contract D5/D2).
 *
 * Targets the stable sendId (not an index), captures the removed route, and
 * restores it at its original position with the SAME sendId on undo.
 */
class RemoveSendCommand : public ICommand {
public:
    RemoveSendCommand(TrackManager& manager, uint32_t channelId, uint64_t sendId)
        : m_manager(manager), m_channelId(channelId), m_sendId(sendId) {}

    void execute() override {
        MixerChannel* channel = m_manager.getChannelById(m_channelId);
        if (!channel) {
            throw std::runtime_error("RemoveSend: channel no longer exists");
        }
        m_sendIndex = channel->findSendIndex(m_sendId);
        if (m_sendIndex < 0) {
            throw std::runtime_error("RemoveSend: send no longer exists");
        }
        m_removedRoute = channel->getSends()[static_cast<size_t>(m_sendIndex)];
        channel->removeSend(m_sendId);
        m_manager.requestAudioGraphRebuild(GraphDirtyReason::RoutingChanged);
    }
    void undo() override {
        if (m_sendIndex < 0) {
            return;
        }
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
    uint64_t m_sendId;
    int m_sendIndex{-1};
    AudioRoute m_removedRoute;
};

/**
 * @brief Undoable edit of one send route (Routing Contract D5/D1/D2).
 *
 * Targets the stable sendId so edits survive index shifts caused by other
 * send removals. Destination changes are validated through
 * TrackManager::canRouteTo; level/pan/tap/mute/sidechain edits cannot create
 * topology, so they skip the walk.
 */
class EditSendCommand : public ICommand {
public:
    EditSendCommand(TrackManager& manager, uint32_t channelId, uint64_t sendId, const AudioRoute& newRoute)
        : m_manager(manager), m_channelId(channelId), m_sendId(sendId), m_newRoute(newRoute) {}

    void execute() override {
        MixerChannel* channel = m_manager.getChannelById(m_channelId);
        if (!channel) {
            throw std::runtime_error("EditSend: channel no longer exists");
        }
        const int sendIndex = channel->findSendIndex(m_sendId);
        if (sendIndex < 0) {
            throw std::runtime_error("EditSend: send no longer exists");
        }
        m_previousRoute = channel->getSends()[static_cast<size_t>(sendIndex)];
        if (m_previousRoute.targetChannelId != m_newRoute.targetChannelId) {
            if (m_newRoute.targetChannelId == kMasterId) {
                throw std::runtime_error("EditSend: sends to master are illegal (Contract D4)");
            }
            if (!m_manager.canRouteTo(m_channelId, m_newRoute.targetChannelId)) {
                throw std::runtime_error("EditSend: routing loop or illegal destination");
            }
        }
        channel->setSend(m_sendId, m_newRoute);
        if (sendTopologyChanged()) {
            // mute/sidechainOnly/postFader/destination all change the compiled
            // topology (edges, sidechain buffers, PDC classification) — not
            // just level/pan. Keep model and graph in step.
            m_manager.requestAudioGraphRebuild(GraphDirtyReason::RoutingChanged);
        }
    }
    void undo() override {
        if (auto* channel = m_manager.getChannelById(m_channelId)) {
            channel->setSend(m_sendId, m_previousRoute);
            if (sendTopologyChanged()) {
                m_manager.requestAudioGraphRebuild(GraphDirtyReason::RoutingChanged);
            }
        }
    }
    void redo() override { execute(); }

    std::string getName() const override { return "Edit Send"; }
    bool changesProjectState() const override { return true; }
    std::string type() const override { return "edit_send"; }

private:
    bool sendTopologyChanged() const {
        return m_previousRoute.targetChannelId != m_newRoute.targetChannelId ||
               m_previousRoute.mute != m_newRoute.mute ||
               m_previousRoute.sidechainOnly != m_newRoute.sidechainOnly ||
               m_previousRoute.postFader != m_newRoute.postFader;
    }

    TrackManager& m_manager;
    uint32_t m_channelId;
    uint64_t m_sendId;
    AudioRoute m_newRoute;
    AudioRoute m_previousRoute;
};

} // namespace Audio
} // namespace Aestra
