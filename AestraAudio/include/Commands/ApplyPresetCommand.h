// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Plugin/PluginHost.h"
#include "Presets/Preset.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace Aestra {
namespace Audio {

/**
 * @brief One undo step that changes a plugin's whole sound: picking a preset, keeping B in an A/B, or a browse that
 * ended on Enter (FD-72: "one undo either way").
 *
 * It holds the sound before and after as presets captured from the plugin, so it works the same for Aestra's own
 * plugins and for a VST3 or CLAP plugin. Browsing auditions sounds live without commands; only the kept one is
 * recorded, with `pushExecuted`, so undo goes straight back to what the producer had before they opened the list.
 * The plugin is held weakly: if it has been removed, undo and redo do nothing. `onApplied` is told which side was
 * written (true = after), so the bar can show the matching preset name.
 */
class ApplyPresetCommand : public ICommand {
public:
    ApplyPresetCommand(std::weak_ptr<IPluginInstance> plugin, Presets::Preset before, Presets::Preset after,
                       std::string label, std::function<void(bool toAfter)> onApplied = {})
        : m_plugin(std::move(plugin)), m_before(std::move(before)), m_after(std::move(after)),
          m_label(std::move(label)), m_onApplied(std::move(onApplied)) {}

    void execute() override { write(m_after, true); }
    void undo() override { write(m_before, false); }
    void redo() override { write(m_after, true); }

    std::string getName() const override { return m_label.empty() ? "Load Preset" : m_label; }
    size_t getSizeInBytes() const override {
        return sizeof(*this) + m_before.state.size() + m_after.state.size() +
               (m_before.params.size() + m_after.params.size()) * sizeof(Presets::PresetParam);
    }
    bool changesProjectState() const override { return true; }
    std::string type() const override { return "apply_preset"; }

private:
    void write(const Presets::Preset& sound, bool toAfter) {
        auto plugin = m_plugin.lock();
        if (!plugin) return;
        Presets::apply(*plugin, sound);
        if (m_onApplied) m_onApplied(toAfter);
    }

    std::weak_ptr<IPluginInstance> m_plugin;
    Presets::Preset m_before;
    Presets::Preset m_after;
    std::string m_label;
    std::function<void(bool)> m_onApplied;
};

} // namespace Audio
} // namespace Aestra
