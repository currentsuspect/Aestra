// © 2025 Nomad Studios – All Rights Reserved. Licensed for personal & educational use only.
#include "StepSequencerPanel.h"

using namespace Aestra::Audio;

StepSequencerPanel::StepSequencerPanel(std::shared_ptr<TrackManager> trackManager)
    : WindowPanel("Step Sequencer")
    , m_trackManager(std::move(trackManager))
{
    m_sequencer = std::make_shared<AestraUI::StepSequencerView>();
    m_sequencer->setBeatsPerBar(4);

    // Hook for future audio integration
    m_sequencer->setOnPatternChanged([this](const std::vector<std::vector<AestraUI::SequencerStep>>& pattern) {
        (void)pattern;
        // TODO: Connect sequencer pattern to TrackManager when MIDI routing is available.
    });

    setContent(m_sequencer);
}
