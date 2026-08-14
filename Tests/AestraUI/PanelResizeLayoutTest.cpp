// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// PanelResizeLayoutTest — headless geometry regression for the resize/layout
// bugs (triage 2026-08-14):
//   * Audio Clip editor: the waveform must never paint over the controls card
//     (it used to be forced to >= 64px even when the panel was too short),
//     sliders must never get negative widths, and the waveform must be visible
//     and usable once the panel is tall enough.
//   * Arsenal: a unit row's name label must stay inside its control block at
//     narrow widths (the old 56px floor pushed it past the block edge).
//
// Render-time fixes (header text clip, chip truncation, step-grid cell clamp)
// are verified by build + code review; the assertions here pin the bounds
// math that the render-time fixes depend on.

#include "AudioClipEditorPanel.h"
#include "ArsenalPanel.h"
#include "Models/TrackManager.h"
#include "Models/UnitManager.h"
#include "NUIComponent.h"
#include "NUISlider.h"
#include "SampleEditorPanel.h"
#include "UnitRow.h"
#include "UnitNameLabel.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

using namespace AestraUI;
using namespace Aestra::Audio;

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        ++g_failures;
    }
}

// Find the waveform child of the audio clip editor's surface (the surface is
// the parent of the waveform, so bounds are surface-local).
NUIRect waveformBounds(const AudioClipEditorPanel& panel) {
    NUIRect empty;
    for (const auto& child : panel.getChildren()) {
        for (const auto& grand : child->getChildren()) {
            if (dynamic_cast<WaveformDisplayComponent*>(grand.get())) {
                return grand->getBounds();
            }
        }
    }
    return empty;
}

NUIRect surfaceBounds(const AudioClipEditorPanel& panel) {
    for (const auto& child : panel.getChildren()) {
        if (child->getChildren().size() > 0) {
            return child->getBounds();
        }
    }
    return NUIRect();
}

void testAudioClipEditorLayout() {
    std::cout << "[PanelResizeLayoutTest] audio clip editor layout...\n";
    auto tm = std::make_shared<Aestra::Audio::TrackManager>();

    // The old default open size (430px tall) sat below the 458px threshold
    // where the waveform region collides with the controls card. The waveform
    // must now be hidden instead of painted over the card.
    {
        AudioClipEditorPanel panel(tm);
        panel.setBounds({0.0f, 0.0f, 700.0f, 430.0f});
        const NUIRect surf = surfaceBounds(panel);
        const float controlsCardTop = surf.bottom() - 242.0f - 8.0f;
        const NUIRect wf = waveformBounds(panel);
        const bool hiddenOrAbove = wf.isEmpty() || wf.bottom() <= controlsCardTop;
        expect(hiddenOrAbove, "waveform overlaps the controls card at 430px height");
        if (!hiddenOrAbove) {
            std::cout << "  at 700x430: waveform bottom " << wf.bottom() << " vs card top " << controlsCardTop << "\n";
        }
    }

    // At 500px tall the waveform must be visible and at least 64px tall.
    {
        AudioClipEditorPanel panel(tm);
        panel.setBounds({0.0f, 0.0f, 700.0f, 500.0f});
        const NUIRect wf = waveformBounds(panel);
        expect(!wf.isEmpty(), "waveform hidden at 500px height");
        expect(wf.height >= 64.0f, "waveform shorter than 64px at 500px height");
    }

    // No slider may ever get a negative width, even far below the panel's
    // declared minimum (clampRectToAllowed can force panels to 100px).
    {
        AudioClipEditorPanel panel(tm);
        panel.setBounds({0.0f, 0.0f, 400.0f, 460.0f});
        for (const auto& child : panel.getChildren()) {
            for (const auto& grand : child->getChildren()) {
                if (auto* slider = dynamic_cast<NUISlider*>(grand.get())) {
                    expect(slider->getBounds().width >= 0.0f, "slider with negative width");
                }
            }
        }
    }
}

void testArsenalRowLabelLayout() {
    std::cout << "[PanelResizeLayoutTest] arsenal row label layout...\n";
    auto tm = std::make_shared<Aestra::Audio::TrackManager>();
    auto& units = tm->getUnitManager();
    const auto unitId = units.createUnit("Long Instrument Name", Aestra::Audio::UnitGroup::Synth);
    units.setUnitEnabled(unitId, true);

    ArsenalPanel panel(tm);
    panel.refreshUnits();
    panel.setBounds({0.0f, 0.0f, 520.0f, 300.0f});

    // Rows are created inside the list container; walk down to UnitRows.
    size_t rowCount = 0;
    for (const auto& child : panel.getChildren()) {
        for (const auto& row : child->getChildren()) {
            auto* unitRow = dynamic_cast<UnitRow*>(row.get());
            if (!unitRow) {
                continue;
            }
            ++rowCount;
            const float rowWidth = row->getBounds().width;
            const float controlWidth = std::clamp(rowWidth * 0.38f, 220.0f, 312.0f);
            const float blockRight = 42.0f + std::max(0.0f, controlWidth - 48.0f);
            for (const auto& label : row->getChildren()) {
                if (auto* nameLabel = dynamic_cast<UnitNameLabel*>(label.get())) {
                    const NUIRect lb = nameLabel->getBounds();
                    expect(lb.width >= 0.0f, "name label with negative width");
                    // The label must stay inside the row's control block.
                    expect(lb.right() <= blockRight + 1.0f, "name label extends past the control block");
                    if (lb.right() > blockRight + 1.0f) {
                        std::cout << "  row width " << rowWidth << ": label right " << lb.right() << " vs block right "
                                  << blockRight << "\n";
                    }
                }
            }
        }
    }
    expect(rowCount >= 1, "no unit rows created for the Arsenal test");
}

} // namespace

int main() {
    std::cout << "=== PanelResizeLayoutTest (resize/layout triage 2026-08-14) ===\n";
    testAudioClipEditorLayout();
    testArsenalRowLabelLayout();

    if (g_failures == 0) {
        std::cout << "=== PanelResizeLayoutTest: all checks passed ===\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "=== PanelResizeLayoutTest: " << g_failures << " failure(s) ===\n";
    return EXIT_FAILURE;
}
