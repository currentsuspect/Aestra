// © 2025 Aestra Studios – All Rights Reserved. Licensed for personal & educational use only.
#include "PianoRollPanel.h"
#include "AudioEngine.h"
#include "PatternManager.h"
#include "../AestraCore/include/AestraLog.h"
#include <cmath>
#include <unordered_map>
#include <random>

using namespace Aestra::Audio;

PianoRollPanel::PianoRollPanel(std::shared_ptr<TrackManager> trackManager)
    : WindowPanel("PIANO ROLL")
    , m_trackManager(trackManager)
    , m_currentPatternId(0)  // Initialize as invalid
{
    // Create piano roll view
    m_pianoRoll = std::make_shared<AestraUI::PianoRollView>();
    m_pianoRoll->setLocalMinimapVisible(false);
    m_pianoRoll->setBeatsPerBar(4);
    m_pianoRoll->setPixelsPerBeat(50.0f);
    
    // Start with empty notes (will load when pattern is opened)
    m_pianoRoll->setNotes({});
    m_pianoRoll->setOnNotesChanged([this](const std::vector<AestraUI::MidiNote>&) {
        savePattern();
    });
    
    // Set as content of WindowPanel
    setContent(m_pianoRoll);

    m_timelineMinimap = std::make_shared<AestraUI::TimelineMinimapBar>();
    m_timelineMinimap->setShowModeToggles(false);
    m_timelineMinimap->setLeadingInset(0.0f);
    m_timelineMinimap->onRequestCenterView = [this](double centerBeat) {
        if (!m_pianoRoll) return;
        const double duration = m_pianoRoll->getViewDurationBeats();
        m_pianoRoll->setViewWindow(std::max(0.0, centerBeat - duration * 0.5), duration);
    };
    m_timelineMinimap->onRequestSetViewStart = [this](double viewStartBeat, bool) {
        if (!m_pianoRoll) return;
        m_pianoRoll->setViewWindow(std::max(0.0, viewStartBeat), m_pianoRoll->getViewDurationBeats());
    };
    m_timelineMinimap->onRequestResizeViewEdge =
        [this](AestraUI::TimelineMinimapResizeEdge edge, double anchorBeat, double edgeBeat, bool) {
            if (!m_pianoRoll) return;
            const double currentStart = m_pianoRoll->getViewStartBeat();
            const double currentEnd = currentStart + m_pianoRoll->getViewDurationBeats();
            double newStart = currentStart;
            double newEnd = currentEnd;
            if (edge == AestraUI::TimelineMinimapResizeEdge::Left) {
                newStart = std::min(edgeBeat, anchorBeat - 0.25);
                newEnd = std::max(anchorBeat, newStart + 0.25);
            } else {
                newStart = std::min(anchorBeat, edgeBeat - 0.25);
                newEnd = std::max(edgeBeat, newStart + 0.25);
            }
            m_pianoRoll->setViewWindow(std::max(0.0, newStart), std::max(0.25, newEnd - newStart));
        };
    m_timelineMinimap->onRequestZoomAround = [this](double anchorBeat, float zoomMultiplier) {
        if (!m_pianoRoll) return;
        const double currentDuration = m_pianoRoll->getViewDurationBeats();
        const double newDuration = std::clamp(currentDuration / std::max(0.1f, zoomMultiplier), 0.5, 256.0);
        m_pianoRoll->setViewWindow(std::max(0.0, anchorBeat - newDuration * 0.5), newDuration);
    };
    addChild(m_timelineMinimap);
    layoutTimelineMinimap();
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

void PianoRollPanel::onResize(int width, int height) {
    WindowPanel::onResize(width, height);
    layoutTimelineMinimap();
}

void PianoRollPanel::loadPattern(PatternID patternId) {
    if (!m_trackManager || !patternId.isValid()) return;
    
    auto& pm = m_trackManager->getPatternManager();
    auto pattern = pm.getPattern(patternId);
    
    if (pattern && pattern->isMidi()) {
        m_currentPatternId = patternId;
        
        // Convert backend notes to UI notes
        const auto& midiPayload = std::get<MidiPayload>(pattern->payload);
        std::vector<AestraUI::MidiNote> uiNotes;
        
        for (const auto& vn : midiPayload.notes) {
            AestraUI::MidiNote uiNote;
            uiNote.pitch = vn.pitch;
            uiNote.startBeat = vn.startBeat;
            uiNote.durationBeats = vn.durationBeats;
            uiNote.velocity = vn.velocity;
            uiNote.unitId = vn.unitId;
            uiNote.selected = false;
            uiNote.isDeleted = false;
            uiNotes.push_back(uiNote);
        }
        
        std::string sourceLabel = pattern->name;
        if (m_editingUnitId != 0) {
            if (const auto* unit = m_trackManager->getUnitManager().getUnit(m_editingUnitId)) {
                if (!unit->name.empty()) {
                    sourceLabel += " • " + unit->name;
                }
            }
        }

        m_pianoRoll->setDefaultUnitId(m_editingUnitId);
        m_pianoRoll->setNotes(uiNotes);
        double longestBeat = pattern->lengthBeats;
        for (const auto& note : midiPayload.notes) {
            longestBeat = std::max(longestBeat, note.startBeat + note.durationBeats);
        }
        m_patternDurationBeats = std::max(4.0, longestBeat + 4.0);
        m_pianoRoll->setTotalDurationBeats(m_patternDurationBeats);
        m_pianoRoll->setPatternName(sourceLabel);
        setTitle("PIANO ROLL - " + pattern->name);
        rebuildTimelineMinimap();
        
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
    pm.applyPatch(m_currentPatternId, [this, &uiNotes](PatternSource& pattern) {
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
                backendNote.velocity = uiNote.velocity;
                backendNote.unitId = uiNote.unitId != 0 ? uiNote.unitId : m_editingUnitId;
                midiPayload.notes.push_back(backendNote);
            }
        }
    });

    if (m_onPatternEdited) {
        m_onPatternEdited(m_currentPatternId);
    }

    rebuildTimelineMinimap();
    
    Log::info("[PianoRollPanel] Saved pattern " + std::to_string(m_currentPatternId.value) + 
              " with " + std::to_string(uiNotes.size()) + " notes");
}

void PianoRollPanel::setEditingUnit(UnitID unitId) {
    m_editingUnitId = unitId;
    if (m_pianoRoll) {
        m_pianoRoll->setDefaultUnitId(unitId);
    }
}

void PianoRollPanel::onUpdate(double deltaTime) {
    WindowPanel::onUpdate(deltaTime);
    if (isVisible()) {
        updateGhostChannels();
        if (m_trackManager && m_pianoRoll) {
            double playheadBeat = 0.0;
            bool follow = false;

            if (m_currentPatternId.isValid()) {
                if (auto* pattern = m_trackManager->getPatternManager().getPattern(m_currentPatternId)) {
                    const double bpm = m_trackManager->getTimelineClock().getCurrentTempo();
                    const double patternLength = std::max(0.25, pattern->lengthBeats);
                    const double positionSeconds = m_audioEngine ? m_audioEngine->getPositionSeconds()
                                                                 : m_trackManager->getUIPosition();
                    const double currentBeat = positionSeconds * (bpm / 60.0);

                    if (m_trackManager->isPatternMode()) {
                        playheadBeat = std::fmod(currentBeat, patternLength);
                        if (playheadBeat < 0.0) playheadBeat += patternLength;
                        follow = m_trackManager->isPlaying();
                    } else {
                        playheadBeat = std::max(0.0, currentBeat);
                    }
                }
            }

            m_pianoRoll->setPlayheadBeat(playheadBeat, follow);
            m_pianoRoll->repaint();

            if (m_timelineMinimap) {
                m_timelineSummarySnapshot = m_timelineSummaryCache.getSnapshot();
                AestraUI::TimelineMinimapModel model;
                model.summary = &m_timelineSummarySnapshot;
                model.view.start = m_pianoRoll->getViewStartBeat();
                model.view.end = model.view.start + m_pianoRoll->getViewDurationBeats();
                model.playheadBeat = playheadBeat;
                model.mode = AestraUI::TimelineMinimapMode::Clips;
                model.aggregation = AestraUI::TimelineMinimapAggregation::MaxPresence;
                model.beatsPerBar = 4;
                model.showSelection = false;
                model.showLoop = false;
                model.showMarkers = false;
                model.showDiagnostics = false;
                model.showPlayhead = true;
                m_timelineMinimap->setModel(model);
                m_timelineMinimap->repaint();
            }
        }
    }
}

void PianoRollPanel::layoutTimelineMinimap() {
    if (!m_timelineMinimap) return;
    const auto bounds = getBounds();
    const float titleBarHeight = getTitleBarHeight();
    const float toolbarH = 40.0f;
    const float keyW = 60.0f;
    const float scrollbarW = 14.0f;
    const float minimapH = 24.0f;
    const float contentW = std::max(0.0f, bounds.width - keyW - scrollbarW);
    m_timelineMinimap->setBounds(AestraUI::NUIAbsolute(bounds, keyW, titleBarHeight + toolbarH, contentW, minimapH));
}

void PianoRollPanel::rebuildTimelineMinimap() {
    if (!m_trackManager || !m_currentPatternId.isValid()) return;

    auto* pattern = m_trackManager->getPatternManager().getPattern(m_currentPatternId);
    if (!pattern || !pattern->isMidi()) return;

    std::vector<AestraUI::TimelineMinimapClipSpan> spans;
    std::unordered_map<uint64_t, uint32_t> laneMap;
    uint32_t nextLane = 0;

    const auto& midiPayload = std::get<MidiPayload>(pattern->payload);
    double domainEnd = std::max(4.0, pattern->lengthBeats);
    for (const auto& note : midiPayload.notes) {
        const double endBeat = std::max(note.startBeat + 0.25, note.startBeat + note.durationBeats);
        domainEnd = std::max(domainEnd, endBeat);

        const uint64_t laneKey = note.unitId != 0 ? note.unitId : static_cast<uint64_t>(note.pitch + 1);
        auto [it, inserted] = laneMap.emplace(laneKey, nextLane);
        if (inserted) ++nextLane;

        AestraUI::TimelineMinimapClipSpan span;
        span.id = (static_cast<uint64_t>(note.pitch & 0x7F) << 32) ^ static_cast<uint64_t>(std::llround(note.startBeat * 1000.0)) ^
                  laneKey;
        span.type = AestraUI::TimelineMinimapClipType::Midi;
        span.startBeat = note.startBeat;
        span.endBeat = endBeat;
        span.trackIndex = std::min<uint32_t>(63, it->second);
        const float velocity = std::clamp(note.velocity <= 1.0f ? note.velocity : note.velocity / 127.0f, 0.0f, 1.0f);
        span.energyApprox = velocity;
        span.peakApprox = velocity;
        spans.push_back(span);
    }

    m_patternDurationBeats = std::max(32.0, domainEnd + 4.0);
    m_timelineSummaryCache.requestRebuild(std::move(spans), 0.0, m_patternDurationBeats);
}

void PianoRollPanel::updateGhostChannels() {
    if (!m_trackManager || !m_pianoRoll) return;
    auto& pm = m_trackManager->getPatternManager();
    auto allPatterns = pm.getAllPatterns();

    std::vector<AestraUI::PianoRollNoteLayer::GhostPattern> ghosts;
    
    // Simple RNG for consistent colors
    std::mt19937 rng(12345); 
    
    for (const auto& p : allPatterns) {
        if (!p->isMidi()) continue;
        
        // Skip the current pattern being edited (it's already shown as foreground)
        if (p->id == m_currentPatternId) continue;
        
        AestraUI::PianoRollNoteLayer::GhostPattern gp;
        
        // Generate Color from ID
        uint64_t h = p->id.value;
        float r = ((h * 1103515245 + 12345) & 0xFF) / 255.0f;
        float g = ((h * 134775813 + 12345) & 0xFF) / 255.0f;
        float b = ((h * 1103515245 + 12345) >> 8 & 0xFF) / 255.0f;
        
        gp.color = AestraUI::NUIColor(r * 0.8f + 0.2f, g * 0.8f + 0.2f, b * 0.8f + 0.2f, 1.0f);

        const auto& midiPayload = std::get<MidiPayload>(p->payload);
        for (const auto& vn : midiPayload.notes) {
            AestraUI::MidiNote uiNote;
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
