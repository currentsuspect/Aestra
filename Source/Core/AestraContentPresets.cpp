// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// The preset bar's link to the project (FD-72): which track a plugin is on, so Save can suggest "Lead Vox Plate"
// and the browser can put vocal presets first on a vocal track, and where its undo steps go.

#include "AestraContent.h"

#include "../AestraUI/Widgets/PluginUIController.h"

#include "Models/TrackManager.h"
#include "ProjectPresetsJson.h"

#include <algorithm>

using namespace Aestra;

namespace {

/// The name of what the plugin is on: a mixer channel's insert, the master's, or an instrument unit.
std::string trackNameFor(const Audio::TrackManager& tm, const Audio::IPluginInstance* plugin) {
    auto inChain = [plugin](const Audio::MixerChannel* ch) {
        if (!ch) return false;
        const auto& chain = ch->getEffectChain();
        for (size_t i = 0; i < chain.slotCount(); ++i)
            if (chain.getPlugin(i).get() == plugin) return true;
        return false;
    };
    for (size_t i = 0; i < tm.getChannelCount(); ++i)
        if (const auto* ch = tm.getChannel(i); inChain(ch)) return ch->getName();
    if (inChain(tm.getMasterChannel())) return "Master";
    const auto& units = tm.getUnitManager();
    for (const auto id : units.getAllUnitIDs())
        if (units.getUnitPlugin(id).get() == plugin)
            if (const auto* unit = units.getUnit(id)) return unit->name;
    return {};
}

} // namespace

void AestraContent::wirePluginPresets() {
    if (!m_pluginController) return;
    m_pluginController->setPresetContextProvider(
        [this](const std::shared_ptr<Audio::IPluginInstance>& plugin)
            -> std::optional<AestraUI::PluginUIController::PresetContext> {
            if (!plugin || !m_trackManager) return std::nullopt;
            AestraUI::PluginUIController::PresetContext context;
            context.trackName = trackNameFor(*m_trackManager, plugin.get());
            std::weak_ptr<Audio::TrackManager> weakTm = m_trackManager;
            context.record = [weakTm](std::shared_ptr<Audio::ICommand> command) {
                if (auto tm = weakTm.lock()) tm->getCommandHistory().pushExecuted(std::move(command));
            };
            // "This project only": kept on the project model and written into the project file (FD-72).
            context.projectPresets = [weakTm](const std::string& pluginId) {
                std::vector<Audio::Presets::Preset> out;
                if (auto tm = weakTm.lock())
                    for (const auto& p : tm->projectPresets())
                        if (p.pluginId == pluginId) out.push_back(p);
                return out;
            };
            context.saveToProject = [weakTm](const Audio::Presets::Preset& preset) {
                auto tm = weakTm.lock();
                if (!tm) return false;
                auto& kept = tm->projectPresets();
                // The same name for the same plugin is the same preset: saving again updates it in place.
                auto same = std::find_if(kept.begin(), kept.end(), [&](const Audio::Presets::Preset& p) {
                    return p.pluginId == preset.pluginId && p.name == preset.name;
                });
                if (same != kept.end()) *same = preset;
                else if (kept.size() < kMaxProjectPresets) kept.push_back(preset);
                else return false;
                tm->markModified(); // it lives in the project, so the project has changed
                return true;
            };
            return context;
        });
}

