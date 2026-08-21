// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ClipInstance.h"
#include "PatternSource.h"
#include "WindowPanel.h"

#include <cstdint>
#include <functional>
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

    void onRender(AestraUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    void onUpdate(double deltaTime) override;

private:
    std::shared_ptr<TrackManager> m_trackManager;
    ClipInstanceID m_clipId;
    PatternID m_patternId;
    std::function<void()> m_onClipEditsCommitted;
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
    std::shared_ptr<AestraUI::NUIButton> m_muteButton;
    std::shared_ptr<AestraUI::NUIButton> m_normalizeButton;
    std::shared_ptr<AestraUI::NUIButton> m_resetButton;
    std::shared_ptr<AestraUI::NUIButton> m_makeUniqueButton;
    std::shared_ptr<AestraUI::NUIButton> m_reverseButton;
    std::shared_ptr<AestraUI::NUIButton> m_commitButton;

    void buildUI();
    bool resolveClip(ClipInstance*& clip, PatternSource*& pattern) const;
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
