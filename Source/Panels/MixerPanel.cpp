// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "MixerPanel.h"

#include "MixerViewModel.h"
#include "Events/Connection.h"
#include "../AestraUI/Widgets/UIMixerPanel.h"
#include "../AestraUI/Base/NUISlider.h"
#include "../App/ServiceLocator.h"
#include "AudioDeviceManager.h"
#include "../../AestraCore/include/AestraJSON.h"
#include "../../AestraCore/include/AestraLog.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

using namespace AestraUI;
using namespace Aestra::Audio;

namespace {

/**
 * @brief Path for mixer UI preferences.
 *
 * These are *application* preferences, not project data: switching projects
 * must not change how the mixer is laid out, so this deliberately does not go
 * through ProjectSerializer. Mirrors browser_settings.json, which already
 * persists FileBrowser layout the same way.
 */
std::string mixerSettingsPath()
{
    const char* home = std::getenv("HOME");
    if (!home || !*home) {
        return {};
    }
    std::string dir = std::string(home) + "/.config/aestra";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return {};
    }
    return dir + "/mixer_settings.json";
}

} // namespace

MixerPanel::MixerPanel(std::shared_ptr<TrackManager> trackManager)
    : WindowPanel("MIXER")
    , m_trackManager(std::move(trackManager))
{
    // Create view model and modern mixer
    m_viewModel = std::make_shared<Aestra::MixerViewModel>();
    if (m_trackManager) {
        // Scoped subscriptions for graph dirty / project modified callbacks
        m_connections.add(m_viewModel->graphDirty.subscribe([trackManager = m_trackManager]() {
            if (trackManager) {
                trackManager->requestAudioGraphRebuild(
                    TrackManager::GraphDirtyReason::EffectChainChanged);
            }
        }));
        m_connections.add(m_viewModel->projectModified.subscribe([trackManager = m_trackManager]() {
            if (trackManager) {
                trackManager->markModified();
            }
        }));
        m_viewModel->setCommandHistory(&m_trackManager->getCommandHistory());
    }
    m_newMixer = std::make_shared<UIMixerPanel>(m_viewModel, m_trackManager);
    m_newMixer->setId("UIMixerPanel_Inner");

    // Set as content of WindowPanel
    setContent(m_newMixer);

    loadUIPreferences();
    m_newMixer->onInspectorPreferenceChanged = [this](bool) { saveUIPreferences(); };

    refreshChannels();
}

void MixerPanel::loadUIPreferences()
{
    const std::string path = mixerSettingsPath();
    if (path.empty() || !m_newMixer) return;

    std::ifstream in(path);
    if (!in) return;  // First run: the panel's own default (expanded) stands.

    const std::string contents((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    if (contents.empty()) return;

    bool consumedAll = false;
    const Aestra::JSON root = Aestra::JSON::parseStrict(contents, consumedAll);
    if (!consumedAll) {
        AESTRA_LOG_WARNING("mixer_settings.json is unreadable; keeping mixer defaults");
        return;
    }

    // Absent must mean "leave the default alone", never "apply false".
    if (root.has("inspectorExpanded")) {
        m_newMixer->setInspectorExpandedPreference(root["inspectorExpanded"].asBool());
    }
}

void MixerPanel::saveUIPreferences() const
{
    const std::string path = mixerSettingsPath();
    if (path.empty() || !m_newMixer) return;

    Aestra::JSON root = Aestra::JSON::object();
    root.set("version", Aestra::JSON(1.0));
    // Only the explicit preference is written. The width-derived collapse is
    // recomputed per layout and must never reach disk, or a session that
    // happened to end in a narrow window would silently rewrite the choice.
    root.set("inspectorExpanded", Aestra::JSON(m_newMixer->getInspectorExpandedPreference()));

    std::ofstream out(path, std::ios::trunc);
    if (!out) return;
    out << root.toString(2);
}


void MixerPanel::refreshChannels()
{
    if (!m_trackManager) return;

    if (!m_viewModel || !m_newMixer) return;

    auto slotMap = m_trackManager->getChannelSlotMapSnapshot();
    
    // Refresh inputs from device manager
    if (auto* deviceManager = Aestra::ServiceLocator::get<Aestra::Audio::AudioDeviceManager>()) {
        m_viewModel->refreshInputs(*deviceManager);
    }
    
    m_viewModel->syncFromEngine(*m_trackManager, slotMap);
    
    // Direct sync: read volume from MixerChannel and update ViewModel faderGainDb
    // This ensures undo/redo reflects immediately without waiting for audio thread
    for (size_t i = 0; i < m_trackManager->getChannelCount(); ++i) {
        auto* channel = m_trackManager->getChannel(i);
        if (!channel) continue;
        auto* vmChannel = m_viewModel->getChannelById(channel->getChannelId());
        if (vmChannel) {
            float linearGain = channel->getVolume();
            float db = (linearGain > 0.0001f) ? 20.0f * std::log10(linearGain) : -90.0f;
            vmChannel->faderGainDb = db;
            vmChannel->muted = channel->isMuted();
            vmChannel->soloed = channel->isSoloed();
            vmChannel->pan = channel->getPan();
        }
    }
    
    m_newMixer->refreshChannels();
}

void MixerPanel::setPlatformBridge(AestraUI::NUIPlatformBridge* bridge)
{
    if (m_newMixer) m_newMixer->setPlatformBridge(bridge);
}
