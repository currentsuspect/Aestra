// © 2025 Nomad Studios – All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "WindowPanel.h"
#include "../NomadUI/Widgets/NUIStepSequencer.h"
#include "../NomadAudio/include/TrackManager.h"
#include <memory>

namespace Nomad {
namespace Audio {

/**
 * @brief Step Sequencer Panel - simple drum/pattern grid wrapped in a window.
 */
class StepSequencerPanel : public WindowPanel {
public:
    StepSequencerPanel(std::shared_ptr<TrackManager> trackManager);
    ~StepSequencerPanel() override = default;

    std::shared_ptr<NomadUI::StepSequencerView> getSequencer() const { return m_sequencer; }

private:
    std::shared_ptr<TrackManager> m_trackManager;
    std::shared_ptr<NomadUI::StepSequencerView> m_sequencer;
};

} // namespace Audio
} // namespace Nomad
