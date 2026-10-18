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
#include "GoldenAudio/GoldenAudioHarness.h"
#include "NUIButton.h"
#include "NUIComponent.h"
#include "NUISlider.h"
#include "SampleEditorPanel.h"
#include "UnitRow.h"
#include "UnitNameLabel.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
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
        const float controlsCardTop = surf.bottom() - 188.0f; // the controls region (columns under titled rules)
        const NUIRect wf = waveformBounds(panel);
        const bool hiddenOrAbove = wf.isEmpty() || wf.bottom() <= controlsCardTop;
        expect(hiddenOrAbove, "waveform overlaps the controls card at 430px height");
        if (!hiddenOrAbove) {
            std::cout << "  at 700x430: waveform bottom " << wf.bottom() << " vs card top " << controlsCardTop << "\n";
        }
    }

    // At the 520px minimum (surface = panel - 28px title bar) the waveform
    // must be visible and at least 64px tall.
    {
        AudioClipEditorPanel panel(tm);
        panel.setBounds({0.0f, 0.0f, 700.0f, 520.0f});
        const NUIRect wf = waveformBounds(panel);
        expect(!wf.isEmpty(), "waveform hidden at the 520px minimum height");
        expect(wf.height >= 64.0f, "waveform shorter than 64px at the 520px minimum");
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


// V8-W1: the fit row offers the five best bar counts for the clip's length,
// best first, described in varispeed vocabulary; clicking one applies it.
void testStretchToTargetFitRow() {
    std::cout << "[PanelResizeLayoutTest] stretch-to-target fit row...\n";
    using namespace GoldenAudio;
    SessionConfig cfg;
    auto tm = std::make_shared<Aestra::Audio::TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(cfg.sampleRate));
    tm->getPlaylistModel().setBPM(120.0);
    // 28 s at 120 BPM = 14 bars.
    const uint32_t frames = static_cast<uint32_t>(cfg.sampleRate) * 28;
    std::vector<float> pcm(static_cast<size_t>(frames) * cfg.channels, 0.1f);
    addAudioTrack(*tm, "FitRow", pcm, frames, cfg);

    auto& model = tm->getPlaylistModel();
    const auto* lane = model.getLane(model.getLaneId(0));
    expect(lane && !lane->clips.empty(), "fit row test: no clip on lane 0");
    if (!lane || lane->clips.empty())
        return;
    const ClipInstanceID clipId = lane->clips.front().id;

    AudioClipEditorPanel panel(tm);
    panel.setBounds({0.0f, 0.0f, 700.0f, 560.0f});
    expect(panel.openClip(clipId), "fit row test: openClip failed");

    std::vector<NUIButton*> fit;
    const std::function<void(const std::shared_ptr<NUIComponent>&)> collect =
        [&](const std::shared_ptr<NUIComponent>& node) {
            if (auto* b = dynamic_cast<NUIButton*>(node.get())) {
                const std::string text = b->getText();
                if (!text.empty() && std::all_of(text.begin(), text.end(), ::isdigit))
                    fit.push_back(b);
            }
            for (const auto& c : node->getChildren())
                collect(c);
        };
    for (const auto& child : panel.getChildren())
        collect(child);
    expect(fit.size() == 5, "expected five numeric fit buttons, found " + std::to_string(fit.size()));
    if (fit.size() != 5)
        return;

    const char* expected[5] = {"16", "12", "10", "8", "20"};
    for (size_t i = 0; i < 5; ++i) {
        expect(fit[i]->getText() == expected[i], std::string("fit button ") + std::to_string(i) + " is " + fit[i]->getText() +
                                                     ", expected " + expected[i]);
        expect(fit[i]->isVisible(), "fit button hidden");
        if (i > 0) {
            const NUIRect a = fit[i - 1]->getBounds(), b = fit[i]->getBounds();
            expect(a.right() <= b.x + 0.01f, "fit buttons overlap");
        }
    }
    expect(fit[0]->getTooltip().rfind("Best fit: 16 bars", 0) == 0, "best-fit tooltip: " + fit[0]->getTooltip());
    expect(fit[0]->getTooltip().find("pitch follows") != std::string::npos, "tooltip lacks varispeed wording");
    expect(fit[0]->getTooltip().find("tretch") == std::string::npos, "tooltip uses the word stretch");

    const NUIRect r = fit[0]->getBounds();
    NUIMouseEvent down;
    down.type = NUIMouseEventType::Down;
    down.position = {r.x + r.width * 0.5f, r.y + r.height * 0.5f};
    down.button = NUIMouseButton::Left;
    down.pressed = true;
    NUIMouseEvent up = down;
    up.type = NUIMouseEventType::Up;
    up.pressed = false;
    up.released = true;
    fit[0]->onMouseEvent(down);
    fit[0]->onMouseEvent(up);

    const auto* clip = model.getClip(clipId);
    expect(clip != nullptr, "clip vanished after fit");
    if (clip) {
        const double beats = clip->durationBeats;
        expect(std::abs(beats - 64.0) < 1e-6, "fit to 16 bars should span 64 beats, got " + std::to_string(beats));
        expect(clip->edits.playbackRate < 1.0f, "fit to 16 bars from 14 should slow the clip");
    }
}

} // namespace

int main() {
    std::cout << "=== PanelResizeLayoutTest (resize/layout triage 2026-08-14) ===\n";
    testAudioClipEditorLayout();
    testArsenalRowLabelLayout();
    testStretchToTargetFitRow();

    if (g_failures == 0) {
        std::cout << "=== PanelResizeLayoutTest: all checks passed ===\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "=== PanelResizeLayoutTest: " << g_failures << " failure(s) ===\n";
    return EXIT_FAILURE;
}
