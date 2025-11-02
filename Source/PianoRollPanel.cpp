// © 2025 Nomad Studios – All Rights Reserved. Licensed for personal & educational use only.
#include "PianoRollPanel.h"

using namespace Nomad::Audio;

PianoRollPanel::PianoRollPanel(std::shared_ptr<TrackManager> trackManager)
    : WindowPanel("Piano Roll")
    , m_trackManager(trackManager)
{
    // Create piano roll view
    m_pianoRoll = std::make_shared<NomadUI::PianoRollView>();
    m_pianoRoll->setBeatsPerBar(4);
    m_pianoRoll->setPixelsPerBeat(50.0f);
    
    // Set as content
    setContent(m_pianoRoll);
}

void PianoRollPanel::setPixelsPerBeat(float ppb) {
    if (m_pianoRoll) {
        m_pianoRoll->setPixelsPerBeat(ppb);
    }
}

void PianoRollPanel::setBeatsPerBar(int bpb) {
    if (m_pianoRoll) {
        m_pianoRoll->setBeatsPerBar(bpb);
    }
}
