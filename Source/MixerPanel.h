// © 2025 Nomad Studios – All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "WindowPanel.h"
#include "../NomadAudio/include/TrackManager.h"
#include <memory>

namespace Nomad {
class MixerViewModel;
}

namespace NomadUI {
class UIMixerPanel;
}

namespace Nomad {
namespace Audio {

/**
 * @brief Mixer Panel - Multi-track audio mixer
 * 
 * Wraps either the legacy MixerView or the new UIMixerPanel in a WindowPanel.
 */
class MixerPanel : public WindowPanel {
public:
    MixerPanel(std::shared_ptr<TrackManager> trackManager);
    ~MixerPanel() override = default;

    // Mixer operations
    void refreshChannels();
    std::shared_ptr<MixerViewModel> getViewModel() const { return m_viewModel; }

private:
    std::shared_ptr<TrackManager> m_trackManager;

    // New mixer implementation (meters-only for now)
    std::shared_ptr<Nomad::MixerViewModel> m_viewModel;
    std::shared_ptr<NomadUI::UIMixerPanel> m_newMixer;
};

} // namespace Audio
} // namespace Nomad
