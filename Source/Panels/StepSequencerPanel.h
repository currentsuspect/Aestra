// © 2025 Aestra Studios – All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "WindowPanel.h"
#include "../AestraUI/Widgets/NUIStepSequencer.h"
#include "TrackManager.h"
#include <memory>

namespace Aestra {
namespace Audio {

/**
 * @brief Step Sequencer Panel - simple drum/pattern grid wrapped in a window.
 */
class StepSequencerPanel : public WindowPanel {
public:
    StepSequencerPanel(std::shared_ptr<TrackManager> trackManager);
    ~StepSequencerPanel() override = default;

    std::shared_ptr<AestraUI::StepSequencerView> getSequencer() const { return m_sequencer; }

private:
    std::shared_ptr<TrackManager> m_trackManager;
    std::shared_ptr<AestraUI::StepSequencerView> m_sequencer;
};

} // namespace Audio
} // namespace Aestra
