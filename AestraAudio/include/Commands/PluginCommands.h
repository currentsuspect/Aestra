#pragma once

#include "ICommand.h"
#include "../Plugin/PluginHost.h"
#include "../Core/MixerChannel.h"

namespace Aestra {
namespace Audio {

class AddPluginCommand : public ICommand {
public:
    AddPluginCommand(MixerChannel& channel, size_t slotIndex, PluginInstancePtr plugin)
        : m_channel(channel), m_slotIndex(slotIndex), m_plugin(std::move(plugin)) {}

    void execute() override {
        // The first execute mints; a redo restores what that mint produced, so
        // undo/redo of an add is a round trip rather than a new plugin wearing
        // the old one's slot (#667). m_instanceId is 0 on the first pass, which
        // is exactly the "mint one" signal.
        m_channel.getEffectChain().insertPlugin(m_slotIndex, m_plugin, m_instanceId);
        m_instanceId = m_channel.getEffectChain().getSlotInstanceId(m_slotIndex);
    }
    void undo() override {
        m_channel.getEffectChain().removePlugin(m_slotIndex);
    }
    void redo() override {
        execute();
    }

    std::string getName() const override { return "Add Plugin"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }
    std::string type() const override { return "add_plugin"; }

private:
    MixerChannel& m_channel;
    size_t m_slotIndex;
    PluginInstancePtr m_plugin;
    uint64_t m_instanceId{0};
};

class RemovePluginCommand : public ICommand {
public:
    RemovePluginCommand(MixerChannel& channel, size_t slotIndex)
        : m_channel(channel), m_slotIndex(slotIndex) {}

    void execute() override {
        // Capture the identity before removing, so undo can put the SAME plugin
        // back rather than a new one that merely looks the same (#667). Without
        // this, insertPlugin mints a fresh id on undo and every automation curve
        // addressed to this plugin is orphaned by a Ctrl+Z the user reads as
        // "put it back".
        m_removedInstanceId = m_channel.getEffectChain().getSlotInstanceId(m_slotIndex);
        // Remove and store the plugin so we can undo
        m_removedPlugin = m_channel.getEffectChain().removePlugin(m_slotIndex);
    }
    void undo() override {
        if (m_removedPlugin) {
            m_channel.getEffectChain().insertPlugin(m_slotIndex, m_removedPlugin, m_removedInstanceId);
        }
    }
    void redo() override {
        execute();
    }

    std::string getName() const override { return "Remove Plugin"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }
    std::string type() const override { return "remove_plugin"; }

private:
    MixerChannel& m_channel;
    size_t m_slotIndex;
    PluginInstancePtr m_removedPlugin;
    uint64_t m_removedInstanceId{0};
};

} // namespace Audio
} // namespace Aestra
