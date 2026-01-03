// ¶¸ 2025 Nomad Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#include "MixerPanel.h"

#include "MixerViewModel.h"
#include "../NomadUI/Widgets/UIMixerPanel.h"

using namespace NomadUI;
using namespace Nomad::Audio;

MixerPanel::MixerPanel(std::shared_ptr<TrackManager> trackManager)
    : WindowPanel("MIXER")
    , m_trackManager(std::move(trackManager))
{
    // Create view model and modern mixer
    m_viewModel = std::make_shared<Nomad::MixerViewModel>();
    m_newMixer = std::make_shared<UIMixerPanel>(m_viewModel, 
                                                m_trackManager->getMeterSnapshots(),
                                                m_trackManager->getContinuousParams());
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
    m_viewModel->syncFromEngine(*m_trackManager, slotMap);
    m_newMixer->refreshChannels();
}
