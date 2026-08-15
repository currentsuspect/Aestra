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

/**
 * @brief Undoable reorder of a plugin within a chain (Automation Identity
 * Contract I7).
 *
 * The instance identity travels with the plugin (movePlugin/swapPlugins carry
 * it), so a reorder is a rearrangement, never a retarget: automation addressed
 * by deviceInstanceId is untouched. Undo restores the exact original ordering
 * and identities; redo reapplies. Nothing is minted, regenerated, or swapped
 * as a side effect. Invalid moves throw, leaving both the chain and the
 * command history untouched.
 */
class MovePluginCommand : public ICommand {
public:
    MovePluginCommand(TrackManager& trackManager, MixerChannel& channel, size_t fromSlot, size_t toSlot)
        : m_trackManager(trackManager), m_channel(channel), m_fromSlot(fromSlot), m_toSlot(toSlot) {}

    void execute() override {
        auto& chain = m_channel.getEffectChain();
        if (m_fromSlot >= EffectChain::MAX_SLOTS || m_toSlot >= EffectChain::MAX_SLOTS) {
            throw std::runtime_error("MovePlugin: slot out of range");
        }
        if (m_fromSlot == m_toSlot) {
            throw std::runtime_error("MovePlugin: source and destination are the same slot");
        }
        if (chain.getSlot(m_fromSlot) == nullptr || chain.getSlot(m_fromSlot)->isEmpty()) {
            throw std::runtime_error("MovePlugin: source slot is empty");
        }
        const auto* target = chain.getSlot(m_toSlot);
        const bool targetEmpty = !target || target->isEmpty();
        if (targetEmpty) {
            if (!chain.movePlugin(m_fromSlot, m_toSlot)) {
                throw std::runtime_error("MovePlugin: move refused by the chain");
            }
            m_usedSwap = false;
        } else {
            if (!chain.swapPlugins(m_fromSlot, m_toSlot)) {
                throw std::runtime_error("MovePlugin: swap refused by the chain");
            }
            m_usedSwap = true;
        }
        m_executed = true;
        m_trackManager.requestAudioGraphRebuild(GraphDirtyReason::EffectChainChanged);
    }
    void undo() override {
        if (!m_executed) {
            return;
        }
        auto& chain = m_channel.getEffectChain();
        // A refused revert must not leave the history believing the reorder
        // was undone while the chain still holds the moved layout.
        const bool reverted = m_usedSwap ? chain.swapPlugins(m_toSlot, m_fromSlot)
                                         : chain.movePlugin(m_toSlot, m_fromSlot);
        if (!reverted) {
            throw std::runtime_error("MovePlugin: undo refused by the chain");
        }
        m_executed = false;
        m_trackManager.requestAudioGraphRebuild(GraphDirtyReason::EffectChainChanged);
    }
    void redo() override { execute(); }

    std::string getName() const override { return "Move Plugin"; }
    bool changesProjectState() const override { return true; }
    std::string type() const override { return "move_plugin"; }

private:
    TrackManager& m_trackManager;
    MixerChannel& m_channel;
    size_t m_fromSlot;
    size_t m_toSlot;
    bool m_usedSwap = false;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
