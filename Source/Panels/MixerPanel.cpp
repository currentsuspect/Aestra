// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "MixerPanel.h"

#include "MixerViewModel.h"
#include "../AestraUI/Widgets/UIMixerPanel.h"
#include "../App/ServiceLocator.h"
#include "AudioDeviceManager.h"

using namespace AestraUI;
using namespace Aestra::Audio;

/**
 * @brief Creates a MIXER panel tied to a track manager.
 *
 * Initializes the internal MixerViewModel and UI mixer panel, wires view-model callbacks to
 * notify the provided TrackManager when the audio graph or project becomes dirty, sets the
 * UI mixer as the panel content, and refreshes channel state from the engine.
 *
 * @param trackManager Shared pointer to the TrackManager to associate with this panel; may be null.
 */
MixerPanel::MixerPanel(std::shared_ptr<TrackManager> trackManager)
    : WindowPanel("MIXER")
    , m_trackManager(std::move(trackManager))
{
    // Create view model and modern mixer
    m_viewModel = std::make_shared<Aestra::MixerViewModel>();
    if (m_trackManager) {
        m_viewModel->setOnGraphDirty([trackManager = m_trackManager]() {
            if (trackManager) {
                trackManager->markGraphDirty();
            }
        });
        m_viewModel->setOnProjectModified([trackManager = m_trackManager]() {
            if (trackManager) {
                trackManager->markModified();
            }
        });
    }
    m_newMixer = std::make_shared<UIMixerPanel>(m_viewModel, m_trackManager);
    m_newMixer->setId("UIMixerPanel_Inner");
    
    // Set as content of WindowPanel
    setContent(m_newMixer);

    refreshChannels();
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
    m_newMixer->refreshChannels();
}
