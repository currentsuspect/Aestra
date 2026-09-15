// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "MixerPanel.h"

#include "MixerViewModel.h"
#include "Events/Connection.h"
#include "../AestraUI/Widgets/UIMixerPanel.h"
#include "../AestraUI/Base/NUISlider.h"
#include "../App/ServiceLocator.h"
#include "AudioDeviceManager.h"
#include "../Core/UISurfaceStore.h"
#include "../../AestraCore/include/AestraLog.h"

using namespace AestraUI;
using namespace Aestra::Audio;

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
        m_viewModel->setTrackManager(m_trackManager.get());
    }
    m_newMixer = std::make_shared<UIMixerPanel>(m_viewModel, m_trackManager);
    m_newMixer->setId("UIMixerPanel_Inner");

    // Set as content of WindowPanel
    setContent(m_newMixer);

    // Apply the stored preference BEFORE wiring the callback: setInspectorExpandedPreference
    // fires onInspectorPreferenceChanged, so wiring first would echo the load into a save.
    loadUIPreferences();
    m_newMixer->onInspectorPreferenceChanged = [this](bool) { saveUIPreferences(); };

    refreshChannels();
}

void MixerPanel::loadUIPreferences()
{
    if (!m_newMixer) return;

    const auto* store = Aestra::ServiceLocator::get<Aestra::UISurfaceStoreFile>();
    if (!store) {
        AESTRA_LOG_WARNING("No UI preference store; mixer layout will not persist");
        return;
    }

    // A preference the user never set keeps the panel's own default; nothing is invented.
    if (const auto expanded = store->boolPreference(Aestra::UISurfaceKeys::kMixerInspectorExpanded)) {
        m_newMixer->setInspectorExpandedPreference(*expanded);
    }
}

void MixerPanel::saveUIPreferences() const
{
    if (!m_newMixer) return;

    auto* store = Aestra::ServiceLocator::get<Aestra::UISurfaceStoreFile>();
    if (!store) return;

    // The explicit preference, never the width-constrained effective state.
    if (!store->setBoolPreference(Aestra::UISurfaceKeys::kMixerInspectorExpanded,
                                  m_newMixer->getInspectorExpandedPreference())) {
        AESTRA_LOG_WARNING("Failed to save mixer layout to " + store->path());
    }
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
