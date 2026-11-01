// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// The preset bar's link to the project (FD-72): which track a plugin is on, so Save can suggest "Lead Vox Plate"
// and the browser can put vocal presets first on a vocal track, and where its undo steps go.

#include "AestraContent.h"

#include "../AestraUI/Widgets/PluginUIController.h"

#include "Models/TrackManager.h"

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
            // "This project only" arrives with project presets in the project file (V83-S2 slice 3).
            return context;
        });
}

