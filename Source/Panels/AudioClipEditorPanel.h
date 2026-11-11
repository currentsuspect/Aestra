// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ClipInstance.h"
#include "PatternSource.h"
#include "WindowPanel.h"

#include <cstdint>
#include <functional>
#include <array>
#include <memory>
#include <vector>

namespace AestraUI {
class NUIButton;
class UIInsertRoutePicker;
class NUILabel;
class NUISlider;
} // namespace AestraUI

namespace Aestra {
namespace Audio {

class TrackManager;
class WaveformDisplayComponent;

/**
 * Focused editor for one Playlist audio-clip instance and its shared source route.
 *
 * Instance controls affect only the selected clip. The output selector edits the
 * referenced audio pattern, so every linked instance follows the same destination.
 */
class AudioClipEditorPanel final : public WindowPanel {
public:
    explicit AudioClipEditorPanel(std::shared_ptr<TrackManager> trackManager);

    bool openClip(ClipInstanceID clipId);
    ClipInstanceID getClipId() const { return m_clipId; }

    /** @brief Fired after committed clip edits reach the model (slider commit or discrete edit). */
    void setOnClipEditsCommitted(std::function<void()> callback) { m_onClipEditsCommitted = std::move(callback); }
    /// Preview: play the clip's part of its source at the clip's speed, pitch and gain (path, start s, length s, rate,
    /// gain dB). An empty path stops it.
    void setOnPreviewRequested(std::function<void(const std::string&, double, double, float, float)> callback) {
        m_onPreviewRequested = std::move(callback);
    }

    void onRender(AestraUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    void onUpdate(double deltaTime) override;

private:
    std::shared_ptr<TrackManager> m_trackManager;
    ClipInstanceID m_clipId;
    PatternID m_patternId;
    std::function<void()> m_onClipEditsCommitted;
    std::function<void(const std::string&, double, double, float, float)> m_onPreviewRequested;
    std::shared_ptr<AestraUI::NUIButton> m_previewButton;
    double m_previewSecondsLeft = 0.0; ///< > 0 while a preview plays: the key reads Stop
    void togglePreview();
    ClipEdits m_workingEdits;
    ClipEdits m_gestureStartEdits;
    bool m_editGestureActive{false};
    bool m_suppressCallbacks{false};
    uint64_t m_routeFingerprint{0};
    double m_sourceDurationSeconds{0.0};
    float m_sourcePeak{0.0f};
    std::vector<float> m_waveformData;

    std::shared_ptr<AestraUI::NUIComponent> m_surface;
    std::shared_ptr<WaveformDisplayComponent> m_waveform;
    std::shared_ptr<AestraUI::NUILabel> m_sourceNameLabel;
    std::shared_ptr<AestraUI::NUILabel> m_sourceMetaLabel;
    std::shared_ptr<AestraUI::NUILabel> m_routeLabel;
    std::shared_ptr<AestraUI::NUILabel> m_routeHintLabel;
    std::shared_ptr<AestraUI::UIInsertRoutePicker> m_routePicker;
    std::shared_ptr<AestraUI::NUILabel> m_instanceLabel;
    std::shared_ptr<AestraUI::NUILabel> m_waveformTitleLabel;
    std::shared_ptr<AestraUI::NUILabel> m_toneSectionLabel;
    std::shared_ptr<AestraUI::NUILabel> m_timingSectionLabel;
    std::shared_ptr<AestraUI::NUILabel> m_gainLabel;
    std::shared_ptr<AestraUI::NUILabel> m_panLabel;
    std::shared_ptr<AestraUI::NUILabel> m_fadeInLabel;
    std::shared_ptr<AestraUI::NUILabel> m_fadeOutLabel;
    std::shared_ptr<AestraUI::NUILabel> m_pitchLabel;
    std::shared_ptr<AestraUI::NUILabel> m_speedLabel;
    std::shared_ptr<AestraUI::NUILabel> m_sourceStartLabel;
    std::shared_ptr<AestraUI::NUILabel> m_gainValueLabel;
    std::shared_ptr<AestraUI::NUILabel> m_panValueLabel;
    std::shared_ptr<AestraUI::NUILabel> m_fadeInValueLabel;
    std::shared_ptr<AestraUI::NUILabel> m_fadeOutValueLabel;
    std::shared_ptr<AestraUI::NUILabel> m_pitchValueLabel;
    std::shared_ptr<AestraUI::NUILabel> m_speedValueLabel;
    std::shared_ptr<AestraUI::NUILabel> m_sourceStartValueLabel;
    std::shared_ptr<AestraUI::NUILabel> m_waveformHintLabel;
    std::shared_ptr<AestraUI::NUISlider> m_gainSlider;
    std::shared_ptr<AestraUI::NUISlider> m_panSlider;
    std::shared_ptr<AestraUI::NUISlider> m_fadeInSlider;
    std::shared_ptr<AestraUI::NUISlider> m_fadeOutSlider;
    std::shared_ptr<AestraUI::NUISlider> m_pitchSlider;
    std::shared_ptr<AestraUI::NUISlider> m_speedSlider;
    std::shared_ptr<AestraUI::NUISlider> m_sourceStartSlider;
    std::shared_ptr<AestraUI::NUILabel> m_fitLabel;
    // The ranked fit targets for this clip (V8-W1, SPEC-001): the top candidates from the
    // generator/ranker in Models/StretchTargets.h, best first, replacing the fixed 1/2/4/8 row.
    static constexpr size_t kFitTargetCount = 5;
    std::array<std::shared_ptr<AestraUI::NUIButton>, kFitTargetCount> m_fitButtons;
    std::array<int, kFitTargetCount> m_fitBars{};
    double m_fitCachedBpm{0.0};
    std::shared_ptr<AestraUI::NUIButton> m_muteButton;
    std::shared_ptr<AestraUI::NUIButton> m_normalizeButton;
    std::shared_ptr<AestraUI::NUIButton> m_resetButton;
    std::shared_ptr<AestraUI::NUIButton> m_makeUniqueButton;
    std::shared_ptr<AestraUI::NUIButton> m_reverseButton;
    std::shared_ptr<AestraUI::NUIButton> m_commitButton;

    void buildUI();
    bool resolveClip(ClipInstance*& clip, PatternSource*& pattern) const;
    void applyFitToBars(int bars);
    /// Region-aware length of the clip's audible content, in source seconds: what the render consumes.
    bool resolveFitContentSeconds(double& contentSeconds) const;
    /// Recompute the ranked fit buttons from the clip's length and the project tempo.
    void refreshFitTargets();
    void rebuildWaveform();
    void rebuildRoutes(bool force);
    uint64_t calculateRouteFingerprint() const;
    void syncControlsFromModel();
    void updateValueLabels();
    void beginEditGesture();
    void applyWorkingEdits();
    void commitEditGesture();
    void applyDiscreteEdit(const ClipEdits& edits);
    void selectRoute(uint32_t routeId);
};

} // namespace Audio
} // namespace Aestra
