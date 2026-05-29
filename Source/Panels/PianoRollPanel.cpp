// © 2025 Aestra Studios – All Rights Reserved. Licensed for personal & educational use only.
#include "PianoRollPanel.h"
#include "../AestraUI/Platform/NUIPlatformBridge.h"
#include "AudioEngine.h"
#include "AudioCommandQueue.h"
#include "PatternManager.h"
#include "Commands/AddNoteCommand.h"
#include "Commands/RemoveNoteCommand.h"
#include "Commands/MoveNoteCommand.h"
#include "Commands/ResizeNoteCommand.h"
#include "Commands/NoteDiff.h"
#include "Commands/CommandHistory.h"
#include "../AestraCore/include/AestraLog.h"
#include "Music/ScaleContext.h"
#include <cmath>
#include <unordered_map>
#include <random>
#include <algorithm>

using namespace Aestra::Audio;

namespace {
double quantizePatternLengthBeats(double contentEndBeat) {
    constexpr double kBeatsPerBar = 4.0;
    constexpr double kBarsPerPatternBlock = 2.0;
    constexpr double kPatternBlockBeats = kBeatsPerBar * kBarsPerPatternBlock; // 8 beats = 2 bars

    const double safeContentEnd = std::max(0.0, contentEndBeat);
    const double blocksNeeded = std::max(1.0, std::ceil(safeContentEnd / kPatternBlockBeats));
    return blocksNeeded * kPatternBlockBeats;
}
} // namespace

PianoRollPanel::PianoRollPanel(std::shared_ptr<TrackManager> trackManager)
    : WindowPanel("PIANO ROLL")
    , m_trackManager(trackManager)
    , m_currentPatternId(0)  // Initialize as invalid
{
    // Create piano roll view
    m_pianoRoll = std::make_shared<AestraUI::PianoRollView>();
    m_pianoRoll->setLocalMinimapVisible(true);
    m_pianoRoll->setBeatsPerBar(4);
    m_pianoRoll->setPixelsPerBeat(50.0f);
    
    // Start with empty notes (will load when pattern is opened)
    m_pianoRoll->setNotes({});
    m_pianoRoll->setOnNotesChanged([this](const std::vector<AestraUI::MidiNote>&) {
        savePattern();
    });
    m_pianoRoll->setPatternLengthBeats(m_patternDurationBeats);
    m_pianoRoll->setOnAdjustPatternLength([this](int barsDelta) {
        adjustPatternLengthBars(barsDelta);
    });
    m_pianoRoll->setOnPatternChoiceSelected([this](int patternValue) {
        PatternID patternId(static_cast<uint64_t>(std::max(0, patternValue)));
        if (!patternId.isValid() || patternId == m_currentPatternId) {
            return;
        }
        savePattern();
        loadPattern(patternId);
    });

    // Setup playback state check and note preview
    m_pianoRoll->setIsPlayingCallback([this]() {
        return m_trackManager && m_trackManager->isPlaying();
    });
    m_pianoRoll->setOnPreviewNote([this](int pitch, int velocity) {
        if (m_trackManager && m_trackManager->isPlaying()) return;
        if (m_audioEngine && m_editingUnitId != 0) {
            AudioQueueCommand cmd;
            cmd.type = AudioQueueCommandType::AuditionUnit;
            cmd.trackIndex = static_cast<uint32_t>(m_editingUnitId);
            cmd.value1 = static_cast<float>(pitch);
            cmd.value2 = static_cast<float>(velocity);
            cmd.samplePos = 0;
            m_audioEngine->commandQueue().push(cmd);
        }
    });

    // Wire CommandHistory state-changed callback to reload pattern into UI on undo/redo
    if (m_trackManager) {
        m_trackManager->getCommandHistory().addOnStateChanged([this]() {
            if (!m_currentPatternId.isValid()) return;
            m_applyingUndoRedo = true;
            loadPattern(m_currentPatternId);
            m_applyingUndoRedo = false;
        });
    }

    setContent(m_pianoRoll);
}

void PianoRollPanel::setPlatformBridge(AestraUI::NUIPlatformBridge* bridge) {
    if (m_pianoRoll) m_pianoRoll->setPlatformBridge(bridge);
}

bool PianoRollPanel::handleKeyEvent(const AestraUI::NUIKeyEvent& event) {
    if (!m_pianoRoll) return false;
    return m_pianoRoll->onKeyEvent(event);
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
}

void PianoRollPanel::loadPattern(PatternID patternId) {
    if (!m_trackManager || !patternId.isValid()) return;
    
    auto& pm = m_trackManager->getPatternManager();
    auto pattern = pm.getPattern(patternId);
    
    if (pattern && pattern->isMidi()) {
        m_currentPatternId = patternId;

        // Load scale context if present
        if (pattern->scaleOverride.has_value()) {
            const auto& ctx = pattern->scaleOverride.value();
            m_pianoRoll->setScale(ctx.rootKey, static_cast<AestraUI::ScaleType>(static_cast<int>(ctx.scaleKind)));
            m_pianoRoll->setSnapToScale(ctx.snapToScale);
        } else {
            m_pianoRoll->setScale(0, AestraUI::ScaleType::Chromatic);
            m_pianoRoll->setSnapToScale(false);
        }

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
        const double quantizedLengthBeats = quantizePatternLengthBeats(longestBeat);
        const double resolvedLengthBeats = std::max(8.0, std::max(pattern->lengthBeats, quantizedLengthBeats));
        if (std::abs(resolvedLengthBeats - pattern->lengthBeats) > 0.001) {
            pm.applyPatch(patternId, [resolvedLengthBeats](PatternSource& p) {
                p.lengthBeats = resolvedLengthBeats;
            });
            m_patternDurationBeats = resolvedLengthBeats;
        } else {
            m_patternDurationBeats = resolvedLengthBeats;
        }
        m_pianoRoll->setPatternLengthBeats(m_patternDurationBeats);
        m_pianoRoll->setTotalDurationBeats(m_patternDurationBeats);
        m_pianoRoll->setPatternName(sourceLabel);
        rebuildPatternSwitcher();
        setTitle("PIANO ROLL - " + pattern->name);

        // Capture note state for undo/redo diff detection
        m_notesBeforeEdit = midiPayload.notes;

        Log::info("[PianoRollPanel] Loaded pattern " + std::to_string(patternId.value) +
                  " with " + std::to_string(uiNotes.size()) + " notes");
    }
}

void PianoRollPanel::savePattern() {
    if (!m_trackManager || !m_currentPatternId.isValid()) return;

    // Guard: skip if we're applying undo/redo (reload triggered by CommandHistory callback)
    if (m_applyingUndoRedo) return;

    auto& pm = m_trackManager->getPatternManager();
    auto& history = m_trackManager->getCommandHistory();

    // Get current UI notes and convert to backend MidiNotes
    const auto& uiNotes = m_pianoRoll->getNotes();
    std::vector<MidiNote> currentNotes;
    for (const auto& uiNote : uiNotes) {
        if (uiNote.isDeleted) continue;
        MidiNote backendNote;
        backendNote.pitch = uiNote.pitch;
        backendNote.startBeat = uiNote.startBeat;
        backendNote.durationBeats = uiNote.durationBeats;
        backendNote.velocity = uiNote.velocity;
        backendNote.unitId = uiNote.unitId != 0 ? uiNote.unitId : m_editingUnitId;
        currentNotes.push_back(backendNote);
    }

    // Diff the before/after states using the dedicated diff function.
    // diffNotes handles same-position/different-duration disambiguation via
    // a two-pass algorithm: exact full-field matching first, then position
    // grouping for move/resize inference.
    NoteDiffResult diff = diffNotes(m_notesBeforeEdit, currentNotes);

    if (!diff.empty()) {
        // Guard against CommandHistory OnStateChanged reloading the UI mid-save
        m_applyingUndoRedo = true;

        // Revert pattern data to the "before" state so commands execute cleanly.
        // The UI already shows the correct "after" state (user edited it).
        pm.applyPatch(m_currentPatternId, [this](PatternSource& pattern) {
            if (pattern.isMidi()) {
                auto& midiPayload = std::get<MidiPayload>(pattern.payload);
                midiPayload.notes = m_notesBeforeEdit;
            }
        });

        // Execute commands in a safe order:
        // 1. Remove first — removes notes that no longer exist
        // 2. Move/Resize — operates on notes that still exist in the pattern
        // 3. Add last — adds new notes
        // This ordering ensures commands never interfere with each other.
        for (const auto& note : diff.removed) {
            auto cmd = std::make_shared<RemoveNoteCommand>(pm, m_currentPatternId, note);
            history.pushAndExecute(cmd);
        }
        for (const auto& [oldNote, newNote] : diff.moved) {
            auto cmd = std::make_shared<MoveNoteCommand>(
                pm, m_currentPatternId, oldNote, newNote.startBeat, newNote.pitch);
            history.pushAndExecute(cmd);
        }
        for (const auto& [oldNote, newNote] : diff.resized) {
            auto cmd = std::make_shared<ResizeNoteCommand>(
                pm, m_currentPatternId, oldNote, newNote.durationBeats);
            history.pushAndExecute(cmd);
        }
        for (const auto& note : diff.added) {
            auto cmd = std::make_shared<AddNoteCommand>(pm, m_currentPatternId, note);
            history.pushAndExecute(cmd);
        }

        Log::info("[PianoRollPanel] Saved pattern " + std::to_string(m_currentPatternId.value) +
                  " with " + std::to_string(currentNotes.size()) + " notes" +
                  " (added:" + std::to_string(diff.added.size()) +
                  " removed:" + std::to_string(diff.removed.size()) +
                  " moved:" + std::to_string(diff.moved.size()) +
                  " resized:" + std::to_string(diff.resized.size()) + ")");

        m_applyingUndoRedo = false;
    }

    // Update captured state for next edit
    m_notesBeforeEdit = currentNotes;

    if (m_onPatternEdited) {
        m_onPatternEdited(m_currentPatternId);
    }

    double longestBeat = 0.0;
    for (const auto& note : currentNotes) {
        longestBeat = std::max(longestBeat, note.startBeat + note.durationBeats);
    }
    // Keep patterns musical in 4-bar blocks and let note content drive the
    // default loop size on add/delete.
    const double newLengthBeats = quantizePatternLengthBeats(longestBeat);
    m_patternDurationBeats = newLengthBeats;
    m_pianoRoll->setPatternLengthBeats(m_patternDurationBeats);
    m_pianoRoll->setTotalDurationBeats(m_patternDurationBeats);

    // Persist the updated length back to the PatternManager so playback uses it
    pm.applyPatch(m_currentPatternId, [newLengthBeats](PatternSource& pattern) {
        pattern.lengthBeats = newLengthBeats;
    });

    // If we're in Arsenal pattern mode, update the audio engine's loop length immediately
    // so the next playback restart uses the correct boundary without requiring a focus switch.
    if (m_trackManager && m_trackManager->isPatternMode() && m_audioEngine) {
        m_audioEngine->setPatternPlaybackMode(true, newLengthBeats);
    }
}

void PianoRollPanel::adjustPatternLengthBars(int barsDelta) {
    if (!m_trackManager || !m_currentPatternId.isValid() || barsDelta == 0) return;

    auto& pm = m_trackManager->getPatternManager();
    auto* pattern = pm.getPattern(m_currentPatternId);
    if (!pattern || !pattern->isMidi()) return;

    const auto& midiPayload = std::get<MidiPayload>(pattern->payload);
    double contentEndBeat = 0.0;
    for (const auto& note : midiPayload.notes) {
        contentEndBeat = std::max(contentEndBeat, note.startBeat + note.durationBeats);
    }

    const double minLengthBeats = quantizePatternLengthBeats(contentEndBeat);
    const double requestedLengthBeats = m_patternDurationBeats + (static_cast<double>(barsDelta) * 4.0);
    const double newLengthBeats = std::max(8.0, std::max(minLengthBeats, requestedLengthBeats));

    if (std::abs(newLengthBeats - m_patternDurationBeats) <= 0.001) {
        m_pianoRoll->setPatternLengthBeats(m_patternDurationBeats);
        m_pianoRoll->setTotalDurationBeats(m_patternDurationBeats);
        return;
    }

    m_patternDurationBeats = newLengthBeats;
    pm.applyPatch(m_currentPatternId, [newLengthBeats](PatternSource& source) {
        source.lengthBeats = newLengthBeats;
    });

    m_pianoRoll->setPatternLengthBeats(m_patternDurationBeats);
    m_pianoRoll->setTotalDurationBeats(m_patternDurationBeats);

    if (m_trackManager->isPatternMode() && m_audioEngine) {
        m_audioEngine->setPatternPlaybackMode(true, newLengthBeats);
    }

    if (m_onPatternEdited) {
        m_onPatternEdited(m_currentPatternId);
    }
}

void PianoRollPanel::setEditingUnit(UnitID unitId) {
    m_editingUnitId = unitId;
    if (m_pianoRoll) {
        m_pianoRoll->setDefaultUnitId(unitId);
    }
}

void PianoRollPanel::onUpdate(double deltaTime) {
    WindowPanel::onUpdate(deltaTime);
    if (isVisible() && !m_wasVisible) {
        rebuildPatternSwitcher();
    }
    m_wasVisible = isVisible();
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

        }
    }
}

void PianoRollPanel::rebuildPatternSwitcher() {
    if (!m_trackManager || !m_pianoRoll) {
        return;
    }

    std::vector<AestraUI::PianoRollToolbar::PatternChoice> choices;
    auto patterns = m_trackManager->getPatternManager().getAllPatterns();
    choices.reserve(patterns.size());
    for (const auto& pattern : patterns) {
        if (!pattern || !pattern->isMidi()) {
            continue;
        }
        std::string label = pattern->name.empty() ? ("Pattern " + std::to_string(pattern->id.value)) : pattern->name;
        choices.push_back({static_cast<int>(pattern->id.value), std::move(label)});
    }

    std::sort(choices.begin(), choices.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.label != rhs.label) {
            return lhs.label < rhs.label;
        }
        return lhs.value < rhs.value;
    });
    m_pianoRoll->setPatternChoices(choices, static_cast<int>(m_currentPatternId.value));
}

void PianoRollPanel::layoutTimelineMinimap() {
    // PianoRollView manages its own local minimap layout.
}

void PianoRollPanel::rebuildTimelineMinimap() {
    // Dedicated local piano minimap renders directly from note data.
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
