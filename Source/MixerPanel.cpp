// ¶¸ 2025 Nomad Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#include "MixerPanel.h"

#include "MixerViewModel.h"
#include "../NomadUI/Widgets/UIMixerPanel.h"

using namespace Nomad::Audio;

MixerPanel::MixerPanel(std::shared_ptr<TrackManager> trackManager)
    : WindowPanel("Mixer")
    , m_trackManager(std::move(trackManager))
{
    m_viewModel = std::make_shared<Nomad::MixerViewModel>();
    
    // Wire up callbacks to TrackManager
    m_viewModel->setOnGraphDirty([this]() {
        if (m_trackManager) m_trackManager->markGraphDirty();
    });
    m_viewModel->setOnProjectModified([this]() {
        if (m_trackManager) m_trackManager->markModified();
    });

    m_newMixer = std::make_shared<NomadUI::UIMixerPanel>(
        m_viewModel,
        m_trackManager ? m_trackManager->getMeterSnapshots() : nullptr,
        m_trackManager ? m_trackManager->getContinuousParams() : nullptr);
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
