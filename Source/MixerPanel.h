// © 2025 Nomad Studios – All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "WindowPanel.h"
#include "MixerView.h"
#include "../NomadAudio/include/TrackManager.h"
#include <memory>

namespace Nomad {
namespace Audio {

/**
 * @brief Mixer Panel - Multi-track audio mixer
 * 
 * Wraps MixerView in a WindowPanel for docking/maximizing
 */
class MixerPanel : public WindowPanel {
public:
    MixerPanel(std::shared_ptr<TrackManager> trackManager);
    ~MixerPanel() override = default;

    // Mixer operations
    void refreshChannels();
    
    std::shared_ptr<MixerView> getMixer() const { return m_mixer; }

private:
    std::shared_ptr<TrackManager> m_trackManager;
    std::shared_ptr<MixerView> m_mixer;
};

} // namespace Audio
} // namespace Nomad
