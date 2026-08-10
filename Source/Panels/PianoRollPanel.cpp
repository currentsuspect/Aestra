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
#include "Commands/UpdateNoteCommand.h"
#include "Commands/NoteDiff.h"
#include "Commands/CommandHistory.h"
#include "../AestraCore/include/AestraLog.h"
#include "Music/ScaleContext.h"
#include <cmath>
#include <unordered_map>
#include <random>
#include <algorithm>
#include <limits>

using namespace Aestra::Audio;

namespace {
// Round pattern length up to whole bars of the CURRENT time signature,
// minimum one bar. (Was hardcoded 2 bars of 4/4 = 8 beats, which both locked
// non-4/4 signatures out and forced a 2-bar minimum.)
double quantizePatternLengthBeats(double contentEndBeat, int beatsPerBar) {
    const double barBeats = static_cast<double>(std::max(1, beatsPerBar));
    const double safeContentEnd = std::max(0.0, contentEndBeat);
    const double barsNeeded = std::max(1.0, std::ceil(safeContentEnd / barBeats));
    return barsNeeded * barBeats;
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
    m_pianoRoll->setOnHarmonyContextChanged([this](int rootKey, AestraUI::ScaleType scaleType, bool snapToScale) {
        if (!m_trackManager || !m_currentPatternId.isValid()) return;

        ScaleContext context;
        context.rootKey = clampRootKey(rootKey);
        context.scaleKind = static_cast<ScaleKind>(static_cast<int>(scaleType));
        context.snapToScale = snapToScale;

        auto& patternManager = m_trackManager->getPatternManager();
        const auto* pattern = patternManager.getPattern(m_currentPatternId);
        if (!pattern || !pattern->isMidi()) return;

        std::optional<ScaleContext> desired;
        assignScaleContextOverride(desired, context);
        const bool unchanged = (!desired && !pattern->scaleOverride) ||
                               (desired && pattern->scaleOverride &&
                                desired->rootKey == pattern->scaleOverride->rootKey &&
                                desired->scaleKind == pattern->scaleOverride->scaleKind &&
                                desired->snapToScale == pattern->scaleOverride->snapToScale);
        if (unchanged) return;

        patternManager.applyPatch(m_currentPatternId, [desired](PatternSource& mutablePattern) {
            mutablePattern.scaleOverride = desired;
        });
        if (m_onPatternEdited) {
            m_onPatternEdited(m_currentPatternId);
        }
    });
    m_pianoRoll->setPatternLengthBeats(m_patternDurationBeats);
    m_pianoRoll->setOnAdjustPatternLength([this](int barsDelta) {
        adjustPatternLengthBars(barsDelta);
    });
    m_pianoRoll->setOnPatternChoiceSelected([this](int patternValue) {
        if (patternValue < 0) {
            return;
        }
        PatternID patternId(static_cast<uint64_t>(patternValue));
        if (!patternId.isValid() || patternId == m_currentPatternId) {
            return;
        }
        savePattern();
        UnitID resolvedUnitId = 0;
        if (const auto* pattern = m_trackManager->getPatternManager().getPattern(patternId);
            pattern && pattern->isMidi()) {
            const auto& midiPayload = std::get<MidiPayload>(pattern->payload);
            const auto noteIt = std::find_if(midiPayload.notes.begin(), midiPayload.notes.end(),
                                             [](const MidiNote& note) { return note.unitId != 0; });
            if (noteIt != midiPayload.notes.end()) {
                resolvedUnitId = noteIt->unitId;
            }
        }
        if (resolvedUnitId == 0) {
            for (const auto unitId : m_trackManager->getUnitManager().getAllUnitIDs()) {
                const auto* unit = m_trackManager->getUnitManager().getUnit(unitId);
                if (unit && unit->defaultPatternId == patternId) {
                    resolvedUnitId = unitId;
                    break;
                }
            }
        }
        if (resolvedUnitId != 0 && resolvedUnitId != m_editingUnitId) {
            setEditingUnit(resolvedUnitId);
        }
        loadPattern(patternId);
    });

    m_pianoRoll->setOnUnitChoiceSelected([this](int unitValue) {
        if (unitValue <= 0 || !m_trackManager) {
            return;
        }
        const UnitID unitId = static_cast<UnitID>(unitValue);
        if (unitId == m_editingUnitId) {
            return;
        }
        if (!m_trackManager->getUnitManager().getUnit(unitId)) {
            return;
        }
        setEditingUnit(unitId);
        // Let the rest of the app follow the switch (Arsenal selection, hardware
        // MIDI + musical-typing target) via the same choke point Arsenal uses.
        if (m_onEditingUnitChanged) {
            m_onEditingUnitChanged(unitId);
        }
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
            // The engine expects value2 normalized 0..1 (it scales by 127 and
            // clamps to [1,127]); raw MIDI velocity here pinned every audition
            // to full velocity.
            cmd.value2 = static_cast<float>(velocity) / 127.0f;
            cmd.samplePos = 0;
            m_audioEngine->commandQueue().push(cmd);
        }
    });
    m_pianoRoll->setOnPlayheadScrubbed([this](double beat, bool active) {
        if (!m_trackManager) return;

        m_trackManager->setUserScrubbing(active);
        double positionSeconds = 0.0;
        if (m_trackManager->isPatternMode()) {
            const double bpm = std::max(1.0, m_trackManager->getTimelineClock().getCurrentTempo());
            positionSeconds = beat * (60.0 / bpm);
        } else {
            positionSeconds = m_trackManager->getPlaylistModel().beatToSeconds(beat);
        }
        positionSeconds = std::max(0.0, positionSeconds);

        m_trackManager->setPosition(positionSeconds);
        m_trackManager->setPlayStartPosition(positionSeconds);
        if (m_audioEngine) {
            const uint32_t sampleRate = m_audioEngine->getSampleRate();
            const uint64_t samplePosition = static_cast<uint64_t>(positionSeconds * sampleRate);
            m_audioEngine->setGlobalSamplePos(samplePosition);
        }
    });

    // Wire CommandHistory state-changed callback to reload pattern into UI on undo/redo.
    // savePattern() sets m_applyingUndoRedo while it pushes its own commands, so a
    // state change with that flag already set is our own save in progress — the UI
    // already reflects the edit, and reloading would rebuild every note with
    // selected=false, wiping the user's selection after the first committed edit.
    // Only reload when the state change came from outside (undo/redo elsewhere).
    if (m_trackManager) {
        m_trackManager->getCommandHistory().addOnStateChanged([this]() {
            if (!m_currentPatternId.isValid()) return;
            if (m_applyingUndoRedo) return;
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

int PianoRollPanel::beatsPerBar() const {
    return m_trackManager ? m_trackManager->getTimelineClock().getBeatsPerBar() : 4;
}

void PianoRollPanel::setBeatsPerBar(int bpb) {
    if (m_pianoRoll) {
        m_pianoRoll->setBeatsPerBar(bpb);
    }
}

void PianoRollPanel::applyHarmonyContextEdit(int rootKey, AestraUI::ScaleType scaleType, bool snapToScale) {
    if (m_pianoRoll) {
        m_pianoRoll->applyHarmonyContextEdit(rootKey, scaleType, snapToScale);
    }
}

ScaleContext PianoRollPanel::getHarmonyContext() const {
    ScaleContext context;
    if (m_pianoRoll) {
        context.rootKey = m_pianoRoll->getRootKey();
        context.scaleKind = static_cast<ScaleKind>(static_cast<int>(m_pianoRoll->getScaleType()));
        context.snapToScale = m_pianoRoll->getSnapToScale();
    }
    return context;
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

        // Convert backend notes to UI notes. Only the editing unit's notes are
        // editable in the roll; other units' notes are stashed and rendered as
        // colored unit ghosts (and merged back untouched on save), so switching
        // the unit dropdown visibly switches what you're editing.
        const auto& midiPayload = std::get<MidiPayload>(pattern->payload);
        std::vector<AestraUI::MidiNote> uiNotes;
        m_otherUnitNotes.clear();
        const bool filterByUnit = m_editingUnitId != 0;

        for (const auto& vn : midiPayload.notes) {
            if (filterByUnit && vn.unitId != 0 && vn.unitId != m_editingUnitId) {
                m_otherUnitNotes.push_back(vn);
                continue;
            }
            AestraUI::MidiNote uiNote;
            uiNote.pitch = vn.pitch;
            uiNote.startBeat = vn.startBeat;
            uiNote.durationBeats = vn.durationBeats;
            uiNote.velocity = vn.velocity;
            uiNote.pan = vn.pan;
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
        const double barBeats = static_cast<double>(beatsPerBar());
        const double quantizedLengthBeats = quantizePatternLengthBeats(longestBeat, beatsPerBar());
        const double resolvedLengthBeats = std::max(barBeats, std::max(pattern->lengthBeats, quantizedLengthBeats));
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
        backendNote.pan = uiNote.pan;
        backendNote.unitId = uiNote.unitId != 0 ? uiNote.unitId : m_editingUnitId;
        currentNotes.push_back(backendNote);
    }

    // Other units' notes are hidden from the editor but still belong to the
    // pattern — merge them back so the diff/revert below can't drop them.
    currentNotes.insert(currentNotes.end(), m_otherUnitNotes.begin(), m_otherUnitNotes.end());

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
        for (const auto& [oldNote, newNote] : diff.modified) {
            auto cmd = std::make_shared<UpdateNoteCommand>(
                pm, m_currentPatternId, oldNote, newNote.velocity, newNote.pan);
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
                  " resized:" + std::to_string(diff.resized.size()) +
                  " modified:" + std::to_string(diff.modified.size()) + ")");

        m_applyingUndoRedo = false;
    }

    // Update captured state for next edit — BEFORE notifying listeners, so a
    // reentrant savePattern (Arsenal refresh echo) diffs as empty and stops.
    m_notesBeforeEdit = currentNotes;

    // Only notify when something actually changed: unconditional notification
    // caused an Arsenal-refresh/refreshTracks storm on every no-op save and
    // fed the setEditingUnit recursion.
    if (!diff.empty() && m_onPatternEdited) {
        m_onPatternEdited(m_currentPatternId);
    }

    // Only a real note edit may drive the pattern length. savePattern() also runs
    // when nothing was edited — setEditingUnit() commits pending edits before every
    // unit switch — and recomputing the length there rewrote the user's pattern:
    // adding or selecting a unit collapsed an empty 2-bar pattern to 1 bar, and an
    // explicitly-sized 4-bar pattern to 1 bar, silently halving the audible loop
    // while the Arsenal grid still displayed the old bar count.
    double newLengthBeats = m_patternDurationBeats;
    if (!diff.empty()) {
        double longestBeat = 0.0;
        for (const auto& note : currentNotes) {
            longestBeat = std::max(longestBeat, note.startBeat + note.durationBeats);
        }
        // Keep patterns musical in whole bars and let note content drive the
        // default loop size on add/delete.
        newLengthBeats = quantizePatternLengthBeats(longestBeat, beatsPerBar());

        // Persist the updated length back to the PatternManager so playback uses it
        pm.applyPatch(m_currentPatternId, [newLengthBeats](PatternSource& pattern) {
            pattern.lengthBeats = newLengthBeats;
        });
    } else if (const auto* storedPattern = pm.getPattern(m_currentPatternId)) {
        // Nothing changed: adopt the stored length instead of rewriting it, so an
        // explicit length set via the bars control survives a unit switch.
        newLengthBeats = std::max(static_cast<double>(beatsPerBar()), storedPattern->lengthBeats);
    }

    m_patternDurationBeats = newLengthBeats;
    m_pianoRoll->setPatternLengthBeats(m_patternDurationBeats);
    m_pianoRoll->setTotalDurationBeats(m_patternDurationBeats);

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

    const double barBeats = static_cast<double>(beatsPerBar());
    const double minLengthBeats = quantizePatternLengthBeats(contentEndBeat, beatsPerBar());
    const double requestedLengthBeats = m_patternDurationBeats + (static_cast<double>(barsDelta) * barBeats);
    const double newLengthBeats = std::max(barBeats, std::max(minLengthBeats, requestedLengthBeats));

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
    // Hard reentrancy guard: savePattern() below fires onPatternEdited, which
    // walks through Arsenal refreshUnits -> onSelectedUnitChanged and back
    // into setEditingUnit while m_editingUnitId is still the old id — without
    // this flag that echo recurses until the stack blows (SIGSEGV in the wild).
    if (m_switchingUnit) {
        return;
    }
    const bool changed = unitId != m_editingUnitId;
    if (changed && m_currentPatternId.isValid()) {
        m_switchingUnit = true;
        savePattern(); // Commit pending edits under the old unit before re-filtering
        m_switchingUnit = false;
    }
    m_editingUnitId = unitId;
    if (m_pianoRoll) {
        m_pianoRoll->setDefaultUnitId(unitId);
    }
    if (changed && m_currentPatternId.isValid()) {
        // Re-split foreground notes vs unit ghosts for the new editing unit.
        loadPattern(m_currentPatternId);
        updateGhostChannels();
    }
    // Keep the toolbar's unit + pattern switchers reflecting the active unit.
    rebuildUnitSwitcher();
    rebuildPatternSwitcher();
}

void PianoRollPanel::rebuildUnitSwitcher() {
    if (!m_trackManager || !m_pianoRoll) {
        return;
    }

    std::vector<AestraUI::PianoRollToolbar::PatternChoice> choices;
    const auto unitIds = m_trackManager->getUnitManager().getAllUnitIDs();
    choices.reserve(unitIds.size());
    for (const auto unitId : unitIds) {
        if (unitId == 0 || unitId > static_cast<UnitID>(std::numeric_limits<int>::max())) {
            continue;
        }
        const auto* unit = m_trackManager->getUnitManager().getUnit(unitId);
        std::string label = (unit && !unit->name.empty()) ? unit->name
                                                          : ("Unit " + std::to_string(unitId));
        choices.push_back({static_cast<int>(unitId), std::move(label)});
    }

    const int selectedValue = (m_editingUnitId != 0 &&
                               m_editingUnitId <= static_cast<UnitID>(std::numeric_limits<int>::max()))
                                  ? static_cast<int>(m_editingUnitId)
                                  : -1;
    m_pianoRoll->setUnitChoices(choices, selectedValue);
}

void PianoRollPanel::onUpdate(double deltaTime) {
    WindowPanel::onUpdate(deltaTime);
    if (isVisible() && !m_wasVisible) {
        rebuildPatternSwitcher();
        rebuildUnitSwitcher();
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
                        // This editor's X axis is PATTERN-LOCAL beats; the transport reports
                        // ARRANGEMENT beats. Feeding one into the other drew a playhead that
                        // drifted across the pattern and off its end — at 8.6s (17.2 beats) it
                        // sat past bar 5 of a 2-bar pattern — asserting a playback position
                        // that does not exist in this editor, then vanishing off the right.
                        //
                        // Timeline playback of a pattern happens through clip instances placed
                        // on lanes, each with its own offset into the source; this panel edits
                        // the pattern itself and resolves no clip, so there is no single honest
                        // pattern-local position to show. Park at the pattern start rather than
                        // display a false one. (Mapping a clip under the playhead back into
                        // pattern-local beats would be the richer behaviour, and needs the clip
                        // lookup this panel deliberately does not carry.)
                        playheadBeat = 0.0;
                    }
                }
            }

            m_pianoRoll->setPlayheadBeat(playheadBeat, follow);
            // Only force a redraw when the playhead is actually moving. Editing
            // gestures (placing/dragging notes, hover) trigger their own
            // repaints, so an unconditional per-frame repaint just pins a core
            // at 100% while the panel sits idle. This was the Piano Roll heat.
            const bool playheadMoved = std::abs(playheadBeat - m_lastPlayheadBeat) > 1e-6;
            if (follow || playheadMoved) {
                m_pianoRoll->repaint();
            }
            m_lastPlayheadBeat = playheadBeat;
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
    const auto* editingUnit = m_editingUnitId != 0 ? m_trackManager->getUnitManager().getUnit(m_editingUnitId) : nullptr;
    for (const auto& pattern : patterns) {
        if (!pattern || !pattern->isMidi()) {
            continue;
        }
        if (m_editingUnitId != 0 && pattern->id != m_currentPatternId &&
            (!editingUnit || editingUnit->defaultPatternId != pattern->id)) {
            const auto& midiPayload = std::get<MidiPayload>(pattern->payload);
            const bool hasEditingUnitNotes = std::any_of(midiPayload.notes.begin(), midiPayload.notes.end(),
                                                         [this](const MidiNote& note) {
                                                             return note.unitId == m_editingUnitId;
                                                         });
            if (!hasEditingUnitNotes) {
                continue;
            }
        }
        if (pattern->id.value > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            Log::warning("[PianoRollPanel] Skipping pattern with ID outside dropdown range: " +
                         std::to_string(pattern->id.value));
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
    const int selectedValue = m_currentPatternId.value <= static_cast<uint64_t>(std::numeric_limits<int>::max())
                                  ? static_cast<int>(m_currentPatternId.value)
                                  : -1;
    m_pianoRoll->setPatternChoices(choices, selectedValue);
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

    // Same-pattern notes belonging to other units: draw them in their unit's
    // Arsenal color, stronger than cross-pattern ghosts, so switching the unit
    // dropdown reads instantly (foreground = editing unit, tinted = the rest).
    if (!m_otherUnitNotes.empty()) {
        auto& unitMgr = m_trackManager->getUnitManager();
        std::unordered_map<UnitID, AestraUI::PianoRollNoteLayer::GhostPattern> unitGhosts;
        for (const auto& vn : m_otherUnitNotes) {
            auto& gp = unitGhosts[vn.unitId];
            if (gp.notes.empty()) {
                uint32_t colorValue = 0x8892a6; // fallback slate
                if (const auto* unit = unitMgr.getUnit(vn.unitId)) {
                    colorValue = unit->color;
                }
                const float scale = 1.0f / 255.0f;
                gp.color = AestraUI::NUIColor(((colorValue >> 16) & 0xff) * scale,
                                              ((colorValue >> 8) & 0xff) * scale,
                                              (colorValue & 0xff) * scale, 1.0f);
                gp.fillAlpha = 0.30f;
                gp.strokeAlpha = 0.55f;
            }
            AestraUI::MidiNote uiNote;
            uiNote.pitch = vn.pitch;
            uiNote.startBeat = vn.startBeat;
            uiNote.durationBeats = vn.durationBeats;
            uiNote.velocity = vn.velocity;
            uiNote.selected = false;
            uiNote.isDeleted = false;
            gp.notes.push_back(uiNote);
        }
        for (auto& [unitId, gp] : unitGhosts) {
            ghosts.push_back(std::move(gp));
        }
    }

    m_pianoRoll->setGhostPatterns(ghosts);
}
