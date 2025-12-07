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
    
    // Add some dummy notes for testing/visualization
    std::vector<NomadUI::MidiNote> notes;
    // C Major chord progression
    notes.push_back({60, 0.0, 1.0, 0.8f, false}); // C4
    notes.push_back({64, 0.0, 1.0, 0.7f, false}); // E4
    notes.push_back({67, 0.0, 1.0, 0.7f, false}); // G4
    
    notes.push_back({65, 1.0, 1.0, 0.8f, false}); // F4
    notes.push_back({69, 1.0, 1.0, 0.7f, false}); // A4
    notes.push_back({72, 1.0, 1.0, 0.7f, false}); // C5
    
    notes.push_back({67, 2.0, 1.0, 0.8f, false}); // G4
    notes.push_back({71, 2.0, 1.0, 0.7f, false}); // B4
    notes.push_back({74, 2.0, 1.0, 0.7f, false}); // D5
    
    notes.push_back({60, 3.0, 1.0, 0.8f, false}); // C4
    notes.push_back({64, 3.0, 1.0, 0.7f, false}); // E4
    notes.push_back({67, 3.0, 1.0, 0.7f, false}); // G4
    notes.push_back({72, 3.0, 1.0, 0.9f, false}); // C5
    
    m_pianoRoll->setNotes(notes);
    
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
