// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/TrackManager.h"
#include "Plugin/EffectChain.h"
#include "Plugin/PluginHost.h"

#include <string>
#include <utility>

namespace Aestra {
namespace Audio {

/**
 * @brief Command to insert an effect plugin into a track's chain (undoable)
 *
 * Wraps the same lifecycle the app uses: the factory creates, initializes
 * and activates the instance; this command inserts it and requests the
 * audio-graph rebuild the playback path needs to pick chain changes up.
 */
class AddEffectCommand : public ICommand {
public:
    AddEffectCommand(TrackManager& trackManager, MixerChannel& channel, size_t slotIndex,
                     PluginInstancePtr plugin, std::string effectName)
        : m_trackManager(trackManager), m_channel(channel), m_slotIndex(slotIndex),
          m_plugin(std::move(plugin)), m_effectName(std::move(effectName)) {}

    void execute() override {
        if (m_executed) return;
        // A redo must restore the identity the first execute minted, not invent a
        // second one for the same plugin (#667) — see AddPluginCommand. 0 on the
        // first pass is exactly the "mint one" signal.
        m_channel.getEffectChain().insertPlugin(m_slotIndex, m_plugin, m_instanceId);
        m_instanceId = m_channel.getEffectChain().getSlotInstanceId(m_slotIndex);
        m_trackManager.requestAudioGraphRebuild(GraphDirtyReason::EffectChainChanged);
        m_executed = true;
    }

    void undo() override {
        if (!m_executed) return;
        m_channel.getEffectChain().removePlugin(m_slotIndex);
        m_trackManager.requestAudioGraphRebuild(GraphDirtyReason::EffectChainChanged);
        m_executed = false;
    }

    void redo() override {
        if (m_executed) return;
        execute();
    }

    std::string getName() const override { return "Add " + m_effectName; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

    size_t getSlotIndex() const { return m_slotIndex; }
    /** True once execute() actually inserted the plugin (see CommandParser). */
    bool wasExecuted() const { return m_executed; }

private:
    TrackManager& m_trackManager;
    MixerChannel& m_channel;
    size_t m_slotIndex;
    PluginInstancePtr m_plugin;
    std::string m_effectName;
    uint64_t m_instanceId{0};
    bool m_executed = false;
};

/**
 * @brief Command to remove an effect plugin from a track's chain (undoable)
 */
class RemoveEffectCommand : public ICommand {
public:
    RemoveEffectCommand(TrackManager& trackManager, MixerChannel& channel, size_t slotIndex)
        : m_trackManager(trackManager), m_channel(channel), m_slotIndex(slotIndex) {}

    void execute() override {
        if (m_executed) return;
        // Capture before removing so undo re-seats the SAME plugin rather than a
        // new one wearing its slot (#667) — see RemovePluginCommand.
        m_removedInstanceId = m_channel.getEffectChain().getSlotInstanceId(m_slotIndex);
        m_removedPlugin = m_channel.getEffectChain().removePlugin(m_slotIndex);
        m_trackManager.requestAudioGraphRebuild(GraphDirtyReason::EffectChainChanged);
        m_executed = true;
    }

    void undo() override {
        if (!m_executed) return;
        if (m_removedPlugin) {
            m_channel.getEffectChain().insertPlugin(m_slotIndex, m_removedPlugin, m_removedInstanceId);
        }
        m_trackManager.requestAudioGraphRebuild(GraphDirtyReason::EffectChainChanged);
        m_executed = false;
    }

    void redo() override {
        if (m_executed) return;
        execute();
    }

    std::string getName() const override { return "Remove Effect"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    TrackManager& m_trackManager;
    MixerChannel& m_channel;
    size_t m_slotIndex;
    PluginInstancePtr m_removedPlugin;
    uint64_t m_removedInstanceId{0};
    bool m_executed = false;
};

/**
 * @brief Command to set an effect parameter (normalized 0..1, undoable)
 */
class SetEffectParamCommand : public ICommand {
public:
    SetEffectParamCommand(PluginInstancePtr plugin, uint32_t paramId, float newValue)
        : m_plugin(std::move(plugin)), m_paramId(paramId), m_newValue(newValue) {}

    void execute() override {
        if (m_executed || !m_plugin) return;
        m_previousValue = m_plugin->getParameter(m_paramId);
        m_plugin->setParameter(m_paramId, m_newValue);
        m_executed = true;
    }

    void undo() override {
        if (!m_executed || !m_plugin) return;
        m_plugin->setParameter(m_paramId, m_previousValue);
        m_executed = false;
    }

    void redo() override {
        if (m_executed) return;
        execute();
    }

    std::string getName() const override { return "Set Effect Parameter"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    PluginInstancePtr m_plugin;
    uint32_t m_paramId;
    float m_newValue;
    float m_previousValue = 0.0f;
    bool m_executed = false;
};

/**
 * @brief Command to bypass/unbypass an effect slot (undoable)
 */
class SetEffectBypassCommand : public ICommand {
public:
    SetEffectBypassCommand(TrackManager& trackManager, MixerChannel& channel, size_t slotIndex,
                           bool bypassed)
        : m_trackManager(trackManager), m_channel(channel), m_slotIndex(slotIndex),
          m_bypassed(bypassed) {}

    void execute() override {
        if (m_executed) return;
        m_previous = m_channel.getEffectChain().isSlotBypassed(m_slotIndex);
        m_channel.getEffectChain().setSlotBypassed(m_slotIndex, m_bypassed);
        m_trackManager.requestAudioGraphRebuild(GraphDirtyReason::EffectChainChanged);
        m_executed = true;
    }

    void undo() override {
        if (!m_executed) return;
        m_channel.getEffectChain().setSlotBypassed(m_slotIndex, m_previous);
        m_trackManager.requestAudioGraphRebuild(GraphDirtyReason::EffectChainChanged);
        m_executed = false;
    }

    void redo() override {
        if (m_executed) return;
        execute();
    }

    std::string getName() const override { return m_bypassed ? "Bypass Effect" : "Enable Effect"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    TrackManager& m_trackManager;
    MixerChannel& m_channel;
    size_t m_slotIndex;
    bool m_bypassed;
    bool m_previous = false;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
