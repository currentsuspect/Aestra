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
        m_channel.getEffectChain().insertPlugin(m_slotIndex, m_plugin);
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
};

class RemovePluginCommand : public ICommand {
public:
    RemovePluginCommand(MixerChannel& channel, size_t slotIndex)
        : m_channel(channel), m_slotIndex(slotIndex) {}

    void execute() override {
        // Remove and store the plugin so we can undo
        m_removedPlugin = m_channel.getEffectChain().removePlugin(m_slotIndex);
    }
    void undo() override {
        if (m_removedPlugin) {
            m_channel.getEffectChain().insertPlugin(m_slotIndex, m_removedPlugin);
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
};

} // namespace Audio
} // namespace Aestra
