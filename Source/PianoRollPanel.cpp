// © 2025 Nomad Studios – All Rights Reserved. Licensed for personal & educational use only.
#include "PianoRollPanel.h"
#include "../NomadAudio/include/PatternManager.h"
#include "../NomadCore/include/NomadLog.h"
#include <random>

using namespace Nomad::Audio;

PianoRollPanel::PianoRollPanel(std::shared_ptr<TrackManager> trackManager)
    : WindowPanel("Piano Roll")
    , m_trackManager(trackManager)
    , m_currentPatternId(0)  // Initialize as invalid
{
    // Create piano roll view
    m_pianoRoll = std::make_shared<NomadUI::PianoRollView>();
    m_pianoRoll->setBeatsPerBar(4);
    m_pianoRoll->setPixelsPerBeat(50.0f);
    
    // Start with empty notes (will load when pattern is opened)
    m_pianoRoll->setNotes({});
    
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

void PianoRollPanel::loadPattern(PatternID patternId) {
    if (!m_trackManager || !patternId.isValid()) return;
    
    auto& pm = m_trackManager->getPatternManager();
    auto pattern = pm.getPattern(patternId);
    
    if (pattern && pattern->isMidi()) {
        m_currentPatternId = patternId;
        
        // Convert backend notes to UI notes
        const auto& midiPayload = std::get<MidiPayload>(pattern->payload);
        std::vector<NomadUI::MidiNote> uiNotes;
        
        for (const auto& vn : midiPayload.notes) {
            NomadUI::MidiNote uiNote;
            uiNote.pitch = vn.pitch;
            uiNote.startBeat = vn.startBeat;
            uiNote.durationBeats = vn.durationBeats;
            uiNote.velocity = vn.velocity / 127.0f;
            uiNote.selected = false;
            uiNote.isDeleted = false;
            // Store unitId in a way the UI can preserve it (we'll add this field or use a map)
            // For now, notes default to unitId=0 (all units)
            uiNotes.push_back(uiNote);
        }
        
        m_pianoRoll->setNotes(uiNotes);
        setTitle("Piano Roll - " + pattern->name);
        
        Log::info("[PianoRollPanel] Loaded pattern " + std::to_string(patternId.value) + 
                  " with " + std::to_string(uiNotes.size()) + " notes");
    }
}

void PianoRollPanel::savePattern() {
    if (!m_trackManager || !m_currentPatternId.isValid()) return;
    
    auto& pm = m_trackManager->getPatternManager();
    
    // Get notes from piano roll
    const auto& uiNotes = m_pianoRoll->getNotes();
    
    // Apply patch to update pattern data
    pm.applyPatch(m_currentPatternId, [&uiNotes](PatternSource& pattern) {
        if (pattern.isMidi()) {
            auto& midiPayload = std::get<MidiPayload>(pattern.payload);
            midiPayload.notes.clear();
            
            // Convert UI notes back to backend notes
            for (const auto& uiNote : uiNotes) {
                if (uiNote.isDeleted) continue;  // Skip deleted notes
                
                MidiNote backendNote;
                backendNote.pitch = uiNote.pitch;
                backendNote.startBeat = uiNote.startBeat;
                backendNote.durationBeats = uiNote.durationBeats;
                backendNote.velocity = static_cast<uint8_t>(uiNote.velocity * 127.0f);
                backendNote.unitId = 0; // Piano Roll notes default to unitId=0 (all units, or let Arsenal assign)
                midiPayload.notes.push_back(backendNote);
            }
        }
    });
    
    Log::info("[PianoRollPanel] Saved pattern " + std::to_string(m_currentPatternId.value) + 
              " with " + std::to_string(uiNotes.size()) + " notes");
}

void PianoRollPanel::onUpdate(double deltaTime) {
    WindowPanel::onUpdate(deltaTime);
    if (isVisible()) {
        updateGhostChannels();
    }
}

void PianoRollPanel::updateGhostChannels() {
    if (!m_trackManager || !m_pianoRoll) return;
    auto& pm = m_trackManager->getPatternManager();
    auto allPatterns = pm.getAllPatterns();

    std::vector<NomadUI::PianoRollNoteLayer::GhostPattern> ghosts;
    
    // Simple RNG for consistent colors
    std::mt19937 rng(12345); 
    
    for (const auto& p : allPatterns) {
        if (!p->isMidi()) continue;
        
        // Skip the current pattern being edited (it's already shown as foreground)
        if (p->id == m_currentPatternId) continue;
        
        NomadUI::PianoRollNoteLayer::GhostPattern gp;
        
        // Generate Color from ID
        uint64_t h = p->id;
        float r = ((h * 1103515245 + 12345) & 0xFF) / 255.0f;
        float g = ((h * 134775813 + 12345) & 0xFF) / 255.0f;
        float b = ((h * 1103515245 + 12345) >> 8 & 0xFF) / 255.0f;
        
        gp.color = NomadUI::NUIColor(r * 0.8f + 0.2f, g * 0.8f + 0.2f, b * 0.8f + 0.2f, 1.0f);

        const auto& midiPayload = std::get<MidiPayload>(p->payload);
        for (const auto& vn : midiPayload.notes) {
            NomadUI::MidiNote uiNote;
            uiNote.pitch = vn.pitch;
            uiNote.startBeat = vn.startBeat;
            uiNote.durationBeats = vn.durationBeats;
            uiNote.velocity = vn.velocity / 127.0f;
            uiNote.selected = false;
            uiNote.isDeleted = false;
            gp.notes.push_back(uiNote);
        }
        ghosts.push_back(gp);
    }
    
    m_pianoRoll->setGhostPatterns(ghosts);
}
