// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Models/PatternSource.h"
#include "Models/TrackManager.h"
#include "Music/ScaleContext.h"
#include "PianoRollPanel.h"

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

} // namespace

int main() {
    testHarmonyContextRoundtripThroughPanel();
    if (failures == 0) {
        std::cout << "PianoRollPanelPersistenceTest passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "PianoRollPanelPersistenceTest: " << failures << " failure(s)\n";
    return EXIT_FAILURE;
}
