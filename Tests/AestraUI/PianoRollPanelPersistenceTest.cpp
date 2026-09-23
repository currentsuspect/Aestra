// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Models/PatternSource.h"
#include "Models/TrackManager.h"
#include "Music/ScaleContext.h"
#include "PianoRollPanel.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

using namespace Aestra::Audio;

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void testHarmonyContextRoundtripThroughPanel() {
    auto trackManager = std::make_shared<TrackManager>();
    auto& patternManager = trackManager->getPatternManager();
    const PatternID patternId = patternManager.createMidiPattern("Harmony Roundtrip", 4.0, MidiPayload{});

    PianoRollPanel editor(trackManager);
    editor.loadPattern(patternId);
    editor.applyHarmonyContextEdit(9, AestraUI::ScaleType::Minor, true);

    const PatternSource* stored = patternManager.getPattern(patternId);
    expect(stored != nullptr, "edited pattern should remain available");
    expect(stored && stored->scaleOverride.has_value(), "panel edit should persist a scale override");
    if (stored && stored->scaleOverride) {
        expect(stored->scaleOverride->rootKey == 9, "stored root should match the view edit");
        expect(stored->scaleOverride->scaleKind == ScaleKind::Minor, "stored scale should match the view edit");
        expect(stored->scaleOverride->snapToScale, "stored snap state should match the view edit");
    }

    PianoRollPanel reloadedEditor(trackManager);
    reloadedEditor.loadPattern(patternId);
    const ScaleContext restored = reloadedEditor.getHarmonyContext();
    expect(restored.rootKey == 9, "loadPattern should restore the edited root");
    expect(restored.scaleKind == ScaleKind::Minor, "loadPattern should restore the edited scale");
    expect(restored.snapToScale, "loadPattern should restore the edited snap state");
}

void testSelectionSurvivesCommittedEditRoundTrip() {
    auto trackManager = std::make_shared<TrackManager>();
    auto& patternManager = trackManager->getPatternManager();

    MidiPayload payload;
    Aestra::Audio::MidiNote n1;
    n1.pitch = 60; n1.startBeat = 0.0; n1.durationBeats = 1.0; n1.velocity = 0.8f;
    Aestra::Audio::MidiNote n2;
    n2.pitch = 64; n2.startBeat = 2.0; n2.durationBeats = 1.0; n2.velocity = 0.8f;
    Aestra::Audio::MidiNote n3;
    n3.pitch = 67; n3.startBeat = 4.0; n3.durationBeats = 1.0; n3.velocity = 0.8f;
    payload.notes = {n1, n2, n3};
    const PatternID patternId = patternManager.createMidiPattern("Selection Roundtrip", 8.0, payload);

    PianoRollPanel editor(trackManager);
    editor.loadPattern(patternId);

    // Ctrl+A selects every note.
    AestraUI::NUIKeyEvent ctrlA;
    ctrlA.keyCode = AestraUI::NUIKeyCode::A;
    ctrlA.modifiers = AestraUI::NUIModifiers::Ctrl;
    ctrlA.pressed = true;
    editor.handleKeyEvent(ctrlA);

    const auto& selected = editor.getNotes();
    expect(selected.size() == 3, "Ctrl+A should select all three notes");
    expect(selected.size() == 3 && selected[0].selected && selected[1].selected && selected[2].selected,
           "all notes should be selected after Ctrl+A");

    // Nudge right: a committed edit that runs savePattern -> CommandHistory ->
    // OnStateChanged. The selection must survive that round trip.
    AestraUI::NUIKeyEvent right;
    right.keyCode = AestraUI::NUIKeyCode::Right;
    right.pressed = true;
    editor.handleKeyEvent(right);

    const auto& afterMove = editor.getNotes();
    expect(afterMove.size() == 3, "committed move should not change note count");
    expect(afterMove.size() == 3 && afterMove[0].selected && afterMove[1].selected && afterMove[2].selected,
           "selection must survive the save/reload round trip after a committed edit");

    // The edit itself must still persist (default snap = Beat, +1.0 beat).
    const PatternSource* stored = patternManager.getPattern(patternId);
    expect(stored != nullptr && stored->isMidi(), "edited pattern should remain available");
    if (stored && stored->isMidi()) {
        const auto& storedNotes = std::get<MidiPayload>(stored->payload).notes;
        expect(storedNotes.size() == 3, "saved pattern should keep all notes");
        expect(storedNotes.size() == 3 && std::abs(storedNotes[0].startBeat - 1.0) < 0.001,
               "nudge should persist +1 beat to the pattern manager");
    }

    // A second committed edit must not wipe the selection either.
    editor.handleKeyEvent(right);
    const auto& afterSecond = editor.getNotes();
    expect(afterSecond.size() == 3 && afterSecond[0].selected && afterSecond[1].selected && afterSecond[2].selected,
           "selection must survive repeated committed edits");
}

} // namespace

// SPEC 3 §2.1 at the caller (review, #961): the helper tests prove the mapping, this proves
// onUpdate() actually puts it on screen. While the timeline plays through a clip of the loaded
// pattern, the editor's playhead sits at the clip-local beat, not parked at 0.
void testTimelinePlaybackMovesThePanelPlayhead() {
    auto trackManager = std::make_shared<TrackManager>();
    auto& patternManager = trackManager->getPatternManager();
    const PatternID patternId = patternManager.createMidiPattern("Timeline Playhead", 8.0, MidiPayload{});
    auto& playlist = trackManager->getPlaylistModel();
    const PlaylistLaneID lane = playlist.createLane("A");
    ClipInstance clip;
    clip.patternId = patternId;
    clip.sourceId = patternId.value;
    clip.startBeat = 8.0;
    clip.durationBeats = 8.0;
    playlist.addClip(lane, clip);

    PianoRollPanel editor(trackManager);
    editor.setVisible(true);
    editor.loadPattern(patternId);

    const double bpm = std::max(1.0, playlist.getBPM());
    trackManager->setPosition(10.5 * 60.0 / bpm); // arrangement beat 10.5, 2.5 beats into the clip
    trackManager->play();
    editor.onUpdate(1.0 / 60.0);
    expect(std::abs(editor.getPlayheadBeat() - 2.5) < 1e-6,
           "timeline playback through a clip should put the editor playhead at the clip-local beat");
    trackManager->stop();
}

int main() {
    testTimelinePlaybackMovesThePanelPlayhead();
    testHarmonyContextRoundtripThroughPanel();
    testSelectionSurvivesCommittedEditRoundTrip();
    if (failures == 0) {
        std::cout << "PianoRollPanelPersistenceTest passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "PianoRollPanelPersistenceTest: " << failures << " failure(s)\n";
    return EXIT_FAILURE;
}
