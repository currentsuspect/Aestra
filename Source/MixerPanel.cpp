// © 2025 Nomad Studios – All Rights Reserved. Licensed for personal & educational use only.
#include "MixerPanel.h"

using namespace Nomad::Audio;

MixerPanel::MixerPanel(std::shared_ptr<TrackManager> trackManager)
    : WindowPanel("Mixer")
    , m_trackManager(trackManager)
{
    // Create mixer view
    m_mixer = std::make_shared<MixerView>(trackManager);
    
    // Set as content
    setContent(m_mixer);
}

void MixerPanel::refreshChannels() {
    if (m_mixer) {
        m_mixer->refreshChannels();
    }
}
