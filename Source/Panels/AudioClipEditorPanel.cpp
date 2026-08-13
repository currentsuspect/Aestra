// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioClipEditorPanel.h"

#include "ChannelDisplayName.h"
#include "Commands/MakeAudioClipUniqueCommand.h"
#include "Commands/RenderAudioClipCommand.h"
#include "Commands/SetAudioPatternMixerChannelCommand.h"
#include "Commands/SetClipEditsCommand.h"
#include "Models/ClipRenderService.h"
#include "NUIButton.h"
#include "NUILabel.h"
#include "NUIRenderer.h"
#include "NUISlider.h"
#include "NUIThemeSystem.h"
#include "SampleEditorPanel.h"
#include "TrackColorPalette.h"
#include "TrackManager.h"
#include "UIInsertRoutePicker.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

using namespace AestraUI;

namespace Aestra {
namespace Audio {

namespace {
constexpr float kMinPanelWidth = 660.0f;
constexpr float kMinPanelHeight = 540.0f;
constexpr size_t kWaveformBuckets = 768;
constexpr float kNormalizeTargetLinear = 0.89125094f; // -1 dBFS peak
constexpr float kControlsCardHeight = 242.0f;
constexpr float kSourceCardHeight = 88.0f;

// All editor colors come from the theme system. The editor uses the standard
// surface hierarchy (canvas < card < sub-card < raised) and the Aestra accent
// for identity, so it follows palette changes instead of carrying its own.
NUIColor canvasColor() {
    return NUIThemeManager::getInstance().getColor("backgroundPrimary");
}

NUIColor surface1Color() {
    return NUIThemeManager::getInstance().getColor("backgroundSecondary");
}

NUIColor surface2Color() {
    return NUIThemeManager::getInstance().getColor("surfaceTertiary");
}

NUIColor surface3Color() {
    return NUIThemeManager::getInstance().getColor("surfaceRaised");
}

NUIColor borderColor() {
    return NUIThemeManager::getInstance().getColor("border");
}

NUIColor primaryTextColor() {
    return NUIThemeManager::getInstance().getColor("textPrimary");
}

NUIColor secondaryTextColor() {
    return NUIThemeManager::getInstance().getColor("textSecondary");
}

NUIColor mutedTextColor() {
    return NUIThemeManager::getInstance().getColor("textMuted");
}

NUIColor clipAccent() {
    return NUIThemeManager::getInstance().getColor("accentPrimary");
}

NUIColor accentStrongColor() {
    return NUIThemeManager::getInstance().getColor("primaryHover");
}

class ClipRangeSlider final : public NUISlider {
public:
    explicit ClipRangeSlider(bool bipolar) : NUISlider(), m_bipolar(bipolar) {}

protected:
    void drawSliderTrack(NUIRenderer& renderer) override {
        const auto bounds = getBounds();
        const float trackY = bounds.y + (bounds.height - getSliderThickness()) * 0.5f;
        const float inset = std::min(getSliderRadius(), bounds.width * 0.5f);
        const NUIRect track{bounds.x + inset, trackY,
                            std::max(0.0f, bounds.width - inset * 2.0f), getSliderThickness()};
        const float radius = getSliderThickness() * 0.5f;
        renderer.fillRoundedRect(track, radius, getTrackColor());

        const float valueX = track.x + track.width * static_cast<float>(valueToProportionOfLength(getValue()));
        if (m_bipolar) {
            const float centerX = track.x + track.width * static_cast<float>(valueToProportionOfLength(0.0));
            const float left = std::min(centerX, valueX);
            const float right = std::max(centerX, valueX);
            if (right > left)
                renderer.fillRoundedRect({left, track.y, right - left, track.height}, radius, getFillColor());
        } else if (valueX > track.x) {
            renderer.fillRoundedRect({track.x, track.y, valueX - track.x, track.height}, radius, getFillColor());
        }
    }

private:
    bool m_bipolar{false};
};

class AudioClipEditorSurface final : public NUIComponent {
public:
    void onRender(NUIRenderer& renderer) override {
        if (!isVisible())
            return;
        const auto bounds = getBounds();
        renderer.fillRect(bounds, canvasColor());

        const auto card = surface1Color();
        const auto stroke = borderColor();
        const NUIRect sourceCard{bounds.x + 8.0f, bounds.y + 8.0f, bounds.width - 16.0f, kSourceCardHeight};
        const NUIRect controlsCard{bounds.x + 8.0f, bounds.bottom() - kControlsCardHeight - 8.0f, bounds.width - 16.0f,
                                   kControlsCardHeight};
        renderer.fillRoundedRect(sourceCard, 9.0f, card);
        renderer.strokeRoundedRect(sourceCard, 10.0f, 1.0f, stroke);
        renderer.fillRoundedRect({sourceCard.x, sourceCard.y + 13.0f, 3.0f, sourceCard.height - 26.0f}, 1.5f,
                                 clipAccent());

        // The waveform is a separate editing surface, not part of the source
        // card. Keep its eyebrow marker in the canvas gap above the card.
        renderer.fillRoundedRect({bounds.x + 16.0f, bounds.y + 110.0f, 2.0f, 10.0f}, 1.0f, clipAccent());

        renderer.fillRoundedRect(controlsCard, 9.0f, card.withAlpha(0.92f));
        renderer.strokeRoundedRect(controlsCard, 10.0f, 1.0f, stroke);

        const float innerPad = 10.0f;
        const float sectionGap = 10.0f;
        const float sectionTop = controlsCard.y + 42.0f;
        const float sectionHeight = controlsCard.height - 52.0f;
        const float sectionWidth = (controlsCard.width - innerPad * 2.0f - sectionGap) * 0.5f;
        const NUIRect toneCard{controlsCard.x + innerPad, sectionTop, sectionWidth, sectionHeight};
        const NUIRect timingCard{toneCard.right() + sectionGap, sectionTop, sectionWidth, sectionHeight};
        renderer.fillRoundedRect(toneCard, 8.0f, surface2Color());
        renderer.strokeRoundedRect(toneCard, 8.0f, 1.0f, borderColor());
        renderer.fillRoundedRect(timingCard, 8.0f, surface2Color());
        renderer.strokeRoundedRect(timingCard, 8.0f, 1.0f, borderColor());
        renderer.fillRoundedRect({toneCard.x + 10.0f, toneCard.y + 29.0f, 2.0f, 10.0f}, 1.0f, clipAccent());
        renderer.fillRoundedRect({timingCard.x + 10.0f, timingCard.y + 29.0f, 2.0f, 10.0f}, 1.0f, clipAccent());
        renderChildren(renderer);
        setDirty(false);
    }
};

std::string formatFixed(double value, int precision) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

std::string formatGainDb(float linear) {
    if (!std::isfinite(linear) || linear <= 0.000001f)
        return "-inf dB";
    const double db = 20.0 * std::log10(static_cast<double>(linear));
    return formatFixed(db, 1) + " dB";
}

bool editsEqual(const ClipEdits& a, const ClipEdits& b) {
    return a.fadeInBeats == b.fadeInBeats && a.fadeOutBeats == b.fadeOutBeats && a.gainLinear == b.gainLinear &&
           a.pan == b.pan && a.muted == b.muted && a.playbackRate == b.playbackRate &&
           a.pitchSemitones == b.pitchSemitones && a.sourceStart == b.sourceStart;
}

} // namespace

AudioClipEditorPanel::AudioClipEditorPanel(std::shared_ptr<TrackManager> trackManager)
    : WindowPanel("AUDIO CLIP"), m_trackManager(std::move(trackManager)) {
    setMinimumPanelSize(kMinPanelWidth, kMinPanelHeight);
    buildUI();
}

void AudioClipEditorPanel::buildUI() {
    m_surface = std::make_shared<AudioClipEditorSurface>();
    const auto makeLabel = [](const std::string& text, float size = 12.5f) {
        auto label = std::make_shared<NUILabel>(text);
        label->setFontSize(size);
        label->setTextColor(secondaryTextColor());
        label->setEllipsize(true);
        return label;
    };
    const auto makeSlider = [](const std::string& name, double minimum, double maximum, double initial,
                               bool bipolar = false) {
        auto slider = std::make_shared<ClipRangeSlider>(bipolar);
        slider->setId(name);
        slider->setOrientation(NUISlider::Orientation::Horizontal);
        slider->setTextBoxVisible(false);
        slider->setSliderRadius(6.5f);
        slider->setSliderThickness(3.0f);
        slider->setRange(minimum, maximum);
        slider->setValue(initial);
        slider->setDefaultValue(initial);
        slider->setDoubleClickReturnValue(true, initial);
        slider->setTrackColor(surface3Color());
        slider->setFillColor(clipAccent());
        slider->setThumbColor(primaryTextColor());
        slider->setThumbHoverColor(accentStrongColor());
        return slider;
    };
    const auto styleButton = [](const std::shared_ptr<NUIButton>& button) {
        button->setStyle(NUIButton::Style::Text);
        button->setFontSize(12.0f);
        button->setCornerRadius(6.0f);
        button->setGlowEnabled(false);
        button->setBackgroundColor(surface2Color());
        button->setHoverColor(surface3Color());
        button->setPressedColor(surface3Color());
        button->setTextColor(secondaryTextColor());
        button->setBorderEnabled(true);
        button->setBorderWidth(1.0f);
        button->setBorderColor(borderColor());
    };

    m_sourceNameLabel = makeLabel("No audio clip selected", 15.0f);
    m_sourceNameLabel->setTextColor(primaryTextColor());
    m_sourceMetaLabel = makeLabel("");
    m_routeLabel = makeLabel("OUTPUT CHANNEL", 10.0f);
    m_routeLabel->setTextColor(mutedTextColor());
    m_routeLabel->setVisible(false);
    m_routeHintLabel = makeLabel("");
    m_routeHintLabel->setVisible(false);
    m_routePicker = std::make_shared<UIInsertRoutePicker>();
    m_routePicker->setCompactChipStyle(true);
    m_routePicker->setOnRouteSelected([this](uint32_t routeId) { selectRoute(routeId); });

    m_waveform = std::make_shared<WaveformDisplayComponent>();
    m_waveformTitleLabel = makeLabel("WAVEFORM", 10.5f);
    m_waveformTitleLabel->setTextColor(secondaryTextColor());
    m_instanceLabel = makeLabel("CLIP ACTIONS", 9.0f);
    m_toneSectionLabel = makeLabel("LEVEL & PITCH", 10.5f);
    m_timingSectionLabel = makeLabel("TIMING & SOURCE", 10.5f);
    m_toneSectionLabel->setTextColor(secondaryTextColor());
    m_timingSectionLabel->setTextColor(secondaryTextColor());
    m_gainLabel = makeLabel("Gain");
    m_panLabel = makeLabel("Pan");
    m_fadeInLabel = makeLabel("Fade in");
    m_fadeOutLabel = makeLabel("Fade out");
    m_pitchLabel = makeLabel("Pitch");
    m_speedLabel = makeLabel("Speed");
    m_sourceStartLabel = makeLabel("Start");
    m_gainValueLabel = makeLabel("-5.0 dB", 11.5f);
    m_panValueLabel = makeLabel("Center", 11.5f);
    m_fadeInValueLabel = makeLabel("0.00 beats", 11.5f);
    m_fadeOutValueLabel = makeLabel("0.00 beats", 11.5f);
    m_pitchValueLabel = makeLabel("0.0 st  •  1.00x", 11.5f);
    m_sourceStartValueLabel = makeLabel("0.000 s", 11.5f);
    m_waveformHintLabel = makeLabel("Scroll to zoom • edits are non-destructive", 11.0f);
    m_waveformHintLabel->setAlignment(NUILabel::Alignment::Right);
    for (const auto& value : {m_gainValueLabel, m_panValueLabel, m_fadeInValueLabel, m_fadeOutValueLabel,
                              m_pitchValueLabel, m_speedValueLabel, m_sourceStartValueLabel}) {
        value->setAlignment(NUILabel::Alignment::Right);
    }

    m_gainSlider = makeSlider("Clip gain", 0.0, 2.0, DEFAULT_AUDIO_CLIP_GAIN_LINEAR);
    m_panSlider = makeSlider("Clip pan", -1.0, 1.0, 0.0, true);
    m_fadeInSlider = makeSlider("Fade in", 0.0, 4.0, 0.0);
    m_fadeOutSlider = makeSlider("Fade out", 0.0, 4.0, 0.0);
    m_pitchSlider = makeSlider("Clip pitch (varispeed)", ClipEdits::kMinPitchSemitones, ClipEdits::kMaxPitchSemitones,
                               0.0, true);
    m_sourceStartSlider = makeSlider("Source start", 0.0, 1.0, 0.0);
    m_muteButton = std::make_shared<NUIButton>("Mute clip");
    m_muteButton->setToggleable(true);
    m_normalizeButton = std::make_shared<NUIButton>("Normalize");
    m_resetButton = std::make_shared<NUIButton>("Reset instance");
    m_makeUniqueButton = std::make_shared<NUIButton>("Make unique");
    m_reverseButton = std::make_shared<NUIButton>("Reverse");
    m_commitButton = std::make_shared<NUIButton>("Commit");
    styleButton(m_muteButton);
    styleButton(m_normalizeButton);
    styleButton(m_resetButton);
    styleButton(m_makeUniqueButton);
    styleButton(m_reverseButton);
    styleButton(m_commitButton);
    m_resetButton->setBackgroundColor(NUIColor::transparent());
    m_resetButton->setHoverColor(NUIThemeManager::getInstance().getColor("error").withAlpha(0.14f));
    m_resetButton->setPressedColor(NUIThemeManager::getInstance().getColor("error").withAlpha(0.14f));
    m_resetButton->setTextColor(mutedTextColor());
    m_resetButton->setBorderEnabled(false);
    m_muteButton->setPressedColor(clipAccent().withAlpha(0.16f));
    m_muteButton->setTextColor(secondaryTextColor());

    const auto wireSlider = [this](const std::shared_ptr<NUISlider>& slider, auto update,
                                   bool updatesWaveform = false) {
        slider->setOnDragStart([this]() { beginEditGesture(); });
        slider->setOnValueChange([this, slider, update, updatesWaveform](double value) {
            if (m_suppressCallbacks)
                return;
            if (!m_editGestureActive)
                beginEditGesture();
            update(m_workingEdits, value);
            applyWorkingEdits();
            // Double-click resets and other non-drag changes do not receive a
            // drag-end callback, so commit them immediately.
            if (!slider->isDragging()) {
                if (updatesWaveform)
                    rebuildWaveform();
                commitEditGesture();
            }
        });
        slider->setOnDragEnd([this, updatesWaveform]() {
            if (updatesWaveform)
                rebuildWaveform();
            commitEditGesture();
        });
    };
    wireSlider(m_gainSlider, [](ClipEdits& edits, double value) {
        edits.gainLinear = static_cast<float>(value);
    });
    wireSlider(m_panSlider, [](ClipEdits& edits, double value) { edits.pan = static_cast<float>(value); });
    wireSlider(m_fadeInSlider, [](ClipEdits& edits, double value) { edits.fadeInBeats = static_cast<float>(value); });
    wireSlider(m_fadeOutSlider, [](ClipEdits& edits, double value) { edits.fadeOutBeats = static_cast<float>(value); });
    wireSlider(
        m_pitchSlider,
        [](ClipEdits& edits, double value) { edits.pitchSemitones = static_cast<float>(value); },
        true);
    wireSlider(
        m_speedSlider,
        [](ClipEdits& edits, double value) { edits.playbackRate = static_cast<float>(value); },
        true);
    wireSlider(m_sourceStartSlider, [this](ClipEdits& edits, double value) {
        const double projectRate =
            m_trackManager ? std::max(1.0, m_trackManager->getPlaylistModel().getProjectSampleRate()) : 48000.0;
        edits.sourceStart = value * projectRate;
    });

    m_muteButton->setOnToggle([this](bool muted) {
        if (m_suppressCallbacks)
            return;
        auto edits = m_workingEdits;
        edits.muted = muted;
        applyDiscreteEdit(edits);
    });
    m_resetButton->setOnClick([this]() { applyDiscreteEdit(ClipEdits::forNewAudioClip()); });
    m_normalizeButton->setOnClick([this]() {
        if (!std::isfinite(m_sourcePeak) || m_sourcePeak <= 0.000001f)
            return;
        auto edits = m_workingEdits;
        const float normalizedGain = std::clamp(kNormalizeTargetLinear / m_sourcePeak, 0.0f, 2.0f);
        edits.gainLinear = normalizedGain;
        applyDiscreteEdit(edits);
    });
    m_makeUniqueButton->setOnClick([this]() {
        if (!m_trackManager || !m_clipId.isValid())
            return;
        auto command = std::make_shared<MakeAudioClipUniqueCommand>(*m_trackManager, m_clipId);
        m_trackManager->getCommandHistory().pushAndExecute(command);
        openClip(m_clipId);
    });
    m_reverseButton->setOnClick([this]() {
        if (!m_trackManager || !m_clipId.isValid())
            return;
        auto command = std::make_shared<ReverseAudioClipCommand>(*m_trackManager, m_clipId);
        m_trackManager->getCommandHistory().pushAndExecute(command);
        // The clip now points at a different source, so re-read it to refresh
        // the waveform and the edit values.
        openClip(m_clipId);
    });
    m_commitButton->setOnClick([this]() {
        if (!m_trackManager || !m_clipId.isValid())
            return;
        // Nothing baked means nothing to commit: rendering here would write a
        // dead file, mint a source and pattern, and add an undo step that
        // changes nothing audible.
        if (m_workingEdits.gainLinear == 1.0f && m_workingEdits.fadeInBeats == 0.0f &&
            m_workingEdits.fadeOutBeats == 0.0f && m_workingEdits.playbackRate == 1.0f &&
            m_workingEdits.pitchSemitones == 0.0f && m_workingEdits.sourceStart == 0.0)
            return;
        auto command = std::make_shared<CommitAudioClipEditsCommand>(*m_trackManager, m_clipId);
        m_trackManager->getCommandHistory().pushAndExecute(command);
        openClip(m_clipId);
    });

    const std::vector<std::shared_ptr<NUIComponent>> children{
        m_sourceNameLabel,    m_sourceMetaLabel,       m_routeLabel,
        m_routeHintLabel,     m_routePicker,           m_waveform,
        m_waveformTitleLabel, m_waveformHintLabel,     m_instanceLabel,
        m_toneSectionLabel,   m_timingSectionLabel,    m_gainLabel,
        m_panLabel,           m_fadeInLabel,           m_fadeOutLabel,
        m_pitchLabel,         m_speedLabel,            m_sourceStartLabel,      m_gainValueLabel,
        m_panValueLabel,      m_fadeInValueLabel,      m_fadeOutValueLabel,     m_pitchValueLabel,
        m_speedValueLabel,    m_sourceStartValueLabel, m_gainSlider,             m_panSlider,
        m_fadeInSlider,       m_fadeOutSlider,         m_pitchSlider,            m_speedSlider,
        m_sourceStartSlider,  m_muteButton,
        m_normalizeButton,    m_resetButton,           m_makeUniqueButton,
        m_reverseButton,      m_commitButton};
    for (const auto& child : children) {
        m_surface->addChild(child);
    }
    setContent(m_surface);
}

void AudioClipEditorPanel::onRender(NUIRenderer& renderer) {
    renderer.drawShadow(getBounds(), 0.0f, 8.0f, 20.0f, NUIColor::black().withAlpha(0.65f));
    WindowPanel::onRender(renderer);
}

bool AudioClipEditorPanel::resolveClip(ClipInstance*& clip, PatternSource*& pattern) const {
    clip = nullptr;
    pattern = nullptr;
    if (!m_trackManager || !m_clipId.isValid())
        return false;
    clip = m_trackManager->getPlaylistModel().getClip(m_clipId);
    if (!clip || !clip->patternId.isValid())
        return false;
    pattern = m_trackManager->getPatternManager().getPattern(clip->patternId);
    return pattern && pattern->isAudio();
}

bool AudioClipEditorPanel::openClip(ClipInstanceID clipId) {
    m_clipId = clipId;
    ClipInstance* clip = nullptr;
    PatternSource* pattern = nullptr;
    if (!resolveClip(clip, pattern)) {
        m_clipId = {};
        return false;
    }
    m_patternId = pattern->id;
    m_workingEdits = clip->edits;
    setTitle("AUDIO CLIP");
    rebuildWaveform();
    syncControlsFromModel();
    rebuildRoutes(true);
    repaint();
    return true;
}

void AudioClipEditorPanel::rebuildWaveform() {
    ClipInstance* clip = nullptr;
    PatternSource* pattern = nullptr;
    if (!resolveClip(clip, pattern))
        return;
    const auto* payload = std::get_if<AudioSlicePayload>(&pattern->payload);
    if (!payload)
        return;
    const auto* source = m_trackManager->getSourceManager().getSource(payload->audioSourceId);
    const auto* buffer = source ? source->getRawBuffer() : nullptr;
    if (!source || !buffer || !buffer->isValid()) {
        m_sourceDurationSeconds = 0.0;
        m_sourcePeak = 0.0f;
        m_sourceNameLabel->setText(clip->name.empty() ? "Audio clip" : clip->name);
        m_sourceMetaLabel->setText("Source unavailable");
        m_waveformData.assign(kWaveformBuckets * 2, 0.0f);
        m_waveform->setWaveformData(m_waveformData);
        return;
    }

    size_t linkedInstances = 0;
    auto& playlist = m_trackManager->getPlaylistModel();
    for (size_t laneIndex = 0; laneIndex < playlist.getLaneCount(); ++laneIndex) {
        const auto* lane = playlist.getLane(playlist.getLaneId(laneIndex));
        if (!lane)
            continue;
        linkedInstances += static_cast<size_t>(
            std::count_if(lane->clips.begin(), lane->clips.end(),
                          [pattern](const ClipInstance& candidate) { return candidate.patternId == pattern->id; }));
    }

    m_sourceDurationSeconds = buffer->durationSeconds();
    m_sourcePeak = 0.0f;
    for (const float sample : buffer->interleavedData) {
        if (std::isfinite(sample))
            m_sourcePeak = std::max(m_sourcePeak, std::abs(sample));
    }

    m_sourceNameLabel->setText(source->getName().empty() ? clip->name : source->getName());
    const char* channelText =
        buffer->numChannels == 1 ? "Mono" : (buffer->numChannels == 2 ? "Stereo" : "Multichannel");
    const float playbackRate =
        std::isfinite(m_workingEdits.playbackRate) ? std::clamp(m_workingEdits.playbackRate, 0.25f, 4.0f) : 1.0f;
    const float pitchSemitones =
        std::isfinite(m_workingEdits.pitchSemitones)
            ? std::clamp(m_workingEdits.pitchSemitones, ClipEdits::kMinPitchSemitones, ClipEdits::kMaxPitchSemitones)
            : 0.0f;
    const double varispeed =
        std::clamp(static_cast<double>(playbackRate) * std::pow(2.0, pitchSemitones / 12.0), 0.25, 4.0);
    const double outputDurationSeconds = buffer->durationSeconds() / varispeed;
    m_sourceMetaLabel->setText(
        formatFixed(buffer->durationSeconds(), 2) + " s source  •  " + formatFixed(outputDurationSeconds, 2) +
        " s output  •  " + std::to_string(buffer->sampleRate) + " Hz  •  " + channelText + "  •  " +
        (linkedInstances > 1 ? "Shared source • " + std::to_string(linkedInstances) + " instances"
                             : "Unique source • 1 instance"));
    m_makeUniqueButton->setVisible(linkedInstances > 1);
    m_makeUniqueButton->setText(linkedInstances > 1 ? "Make unique" : "Unique");
    m_routeHintLabel->setText("");

    ClipRenderService renderService(m_trackManager->getSourceManager(), m_trackManager->getPatternManager());
    const auto region = renderService.resolveClipRegion(
        *clip, std::max(1.0, m_trackManager->getPlaylistModel().getProjectSampleRate()));
    const size_t sourceStartFrame = region.isValid() ? static_cast<size_t>(region.startFrame) : 0;
    const size_t sourceFrameCount =
        region.isValid() ? static_cast<size_t>(region.frameCount) : static_cast<size_t>(buffer->numFrames);
    const size_t sourceEndFrame = sourceStartFrame + sourceFrameCount;
    const size_t channels = static_cast<size_t>(buffer->numChannels);
    // The waveform width represents the clip's fixed timeline duration. Map
    // each output-time bucket through playbackRate: faster rates exhaust the
    // source early and leave visible silence, while slower rates stretch the
    // audible prefix across the clip. Normalizing the pitched output back to
    // the full width would make every rate look unchanged.
    const double clipFrames = clip->durationSeconds * static_cast<double>(buffer->sampleRate);
    const size_t outputFrameCount = std::max<size_t>(
        1, std::isfinite(clipFrames) && clipFrames > 0.0 ? static_cast<size_t>(clipFrames) : sourceFrameCount);
    const size_t framesPerBucket = std::max<size_t>(1, (outputFrameCount + kWaveformBuckets - 1) / kWaveformBuckets);
    m_waveformData.clear();
    m_waveformData.reserve(kWaveformBuckets * 2);
    for (size_t bucket = 0; bucket < kWaveformBuckets; ++bucket) {
        const size_t begin = bucket * framesPerBucket;
        const size_t end = std::min(outputFrameCount, begin + framesPerBucket);
        float minimum = 0.0f;
        float maximum = 0.0f;
        const size_t sampleStride = std::max<size_t>(1, framesPerBucket / 256);
        for (size_t outputFrame = begin; outputFrame < end; outputFrame += sampleStride) {
            const double phase =
                static_cast<double>(sourceStartFrame) + static_cast<double>(outputFrame) * varispeed;
            if (phase >= static_cast<double>(sourceEndFrame))
                continue;
            const size_t frame = static_cast<size_t>(phase);
            const size_t nextFrame = std::min(sourceEndFrame - 1, frame + 1);
            const float fraction = static_cast<float>(phase - static_cast<double>(frame));
            float mono = 0.0f;
            for (size_t channel = 0; channel < channels; ++channel) {
                const float a = buffer->interleavedData[frame * channels + channel];
                const float b = buffer->interleavedData[nextFrame * channels + channel];
                mono += a + (b - a) * fraction;
            }
            mono /= static_cast<float>(channels);
            minimum = std::min(minimum, mono);
            maximum = std::max(maximum, mono);
        }
        m_waveformData.push_back(maximum);
        m_waveformData.push_back(minimum);
    }
    m_waveform->setWaveformData(m_waveformData);
    m_waveformTitleLabel->setText(std::abs(varispeed - 1.0) < 1.0e-5
                                      ? "WAVEFORM"
                                      : "PITCHED WAVEFORM  •  " + formatFixed(outputDurationSeconds, 2) + " s");
}

uint64_t AudioClipEditorPanel::calculateRouteFingerprint() const {
    if (!m_trackManager)
        return 0;
    uint64_t fingerprint = 1469598103934665603ull;
    const auto mix = [&fingerprint](uint64_t value) {
        fingerprint ^= value;
        fingerprint *= 1099511628211ull;
    };
    mix(m_trackManager->getChannelCount());
    for (size_t index = 0; index < m_trackManager->getChannelCount(); ++index) {
        const auto* channel = m_trackManager->getChannel(index);
        if (!channel)
            continue;
        mix(channel->getChannelId());
        for (unsigned char c : channel->getName())
            mix(c);
    }
    if (const auto* pattern = m_trackManager->getPatternManager().getPattern(m_patternId)) {
        mix(pattern->getMixerChannelId());
    }
    return fingerprint;
}

void AudioClipEditorPanel::rebuildRoutes(bool force) {
    const uint64_t fingerprint = calculateRouteFingerprint();
    if (!force && fingerprint == m_routeFingerprint)
        return;
    m_routeFingerprint = fingerprint;
    m_suppressCallbacks = true;
    std::vector<UIInsertRoutePicker::Route> routes;
    routes.push_back({MASTER_MIXER_CHANNEL_ID, 0, "Master", 0});
    for (size_t index = 0; index < m_trackManager->getChannelCount(); ++index) {
        const auto* channel = m_trackManager->getChannel(index);
        if (!channel)
            continue;
        const std::string name =
            AestraUI::channelDisplayName(channel->getChannelId(), channel->getName());
        routes.push_back({channel->getChannelId(), static_cast<int>(index + 1), name,
                          paletteIndexToARGB(channel->getTrackColorIndex())});
    }
    uint32_t selectedRoute = MASTER_MIXER_CHANNEL_ID;
    if (const auto* pattern = m_trackManager->getPatternManager().getPattern(m_patternId)) {
        selectedRoute = pattern->getMixerChannelId();
    }
    m_routePicker->setRoutes(std::move(routes), selectedRoute);
    m_suppressCallbacks = false;
}

void AudioClipEditorPanel::syncControlsFromModel() {
    ClipInstance* clip = nullptr;
    PatternSource* pattern = nullptr;
    if (!resolveClip(clip, pattern))
        return;
    m_workingEdits = clip->edits;
    m_suppressCallbacks = true;
    m_gainSlider->setValue(m_workingEdits.gainLinear);
    m_panSlider->setValue(m_workingEdits.pan);
    const double fadeMaximum = std::max(0.01, clip->durationBeats);
    m_fadeInSlider->setRange(0.0, fadeMaximum);
    m_fadeOutSlider->setRange(0.0, fadeMaximum);
    m_fadeInSlider->setValue(std::min<double>(m_workingEdits.fadeInBeats, fadeMaximum));
    m_fadeOutSlider->setValue(std::min<double>(m_workingEdits.fadeOutBeats, fadeMaximum));
    m_pitchSlider->setValue(m_workingEdits.pitchSemitones);
    m_speedSlider->setValue(m_workingEdits.playbackRate);
    const double projectRate = std::max(1.0, m_trackManager->getPlaylistModel().getProjectSampleRate());
    const double sourceStartSeconds = std::max(0.0, m_workingEdits.sourceStart) / projectRate;
    const double sourceStartMaximum = std::max(0.001, m_sourceDurationSeconds);
    m_sourceStartSlider->setRange(0.0, sourceStartMaximum);
    m_sourceStartSlider->setValue(std::min(sourceStartSeconds, sourceStartMaximum));
    m_muteButton->setToggled(m_workingEdits.muted);
    m_muteButton->setText(m_workingEdits.muted ? "Unmute clip" : "Mute clip");
    m_suppressCallbacks = false;
    updateValueLabels();
}

void AudioClipEditorPanel::updateValueLabels() {
    m_gainValueLabel->setText(formatGainDb(m_workingEdits.gainLinear));
    if (std::abs(m_workingEdits.pan) < 0.005f) {
        m_panValueLabel->setText("Center");
    } else {
        const char side = m_workingEdits.pan < 0.0f ? 'L' : 'R';
        m_panValueLabel->setText(std::to_string(static_cast<int>(std::round(std::abs(m_workingEdits.pan) * 100.0f))) +
                                 side);
    }
    m_fadeInValueLabel->setText(formatFixed(m_workingEdits.fadeInBeats, 2) + " beats");
    m_fadeOutValueLabel->setText(formatFixed(m_workingEdits.fadeOutBeats, 2) + " beats");
    const float pitchSemitones = m_workingEdits.pitchSemitones;
    const std::string pitchSign = pitchSemitones > 0.049f ? "+" : "";
    m_pitchValueLabel->setText(pitchSign + formatFixed(pitchSemitones, 1) + " st");
    m_speedValueLabel->setText(formatFixed(m_workingEdits.playbackRate, 2) + "x");
    const double projectRate =
        m_trackManager ? std::max(1.0, m_trackManager->getPlaylistModel().getProjectSampleRate()) : 48000.0;
    m_sourceStartValueLabel->setText(formatFixed(std::max(0.0, m_workingEdits.sourceStart) / projectRate, 3) + " s");
}

void AudioClipEditorPanel::beginEditGesture() {
    if (m_editGestureActive)
        return;
    m_gestureStartEdits = m_workingEdits;
    m_editGestureActive = true;
}

void AudioClipEditorPanel::applyWorkingEdits() {
    if (!m_trackManager || !m_clipId.isValid())
        return;
    if (!m_editGestureActive)
        beginEditGesture();
    m_trackManager->getPlaylistModel().setClipEdits(m_clipId, m_workingEdits);
    m_trackManager->markModified();
    updateValueLabels();
}

void AudioClipEditorPanel::commitEditGesture() {
    if (!m_editGestureActive || !m_trackManager)
        return;
    m_editGestureActive = false;
    if (!editsEqual(m_gestureStartEdits, m_workingEdits)) {
        auto command = std::make_shared<SetClipEditsCommand>(m_trackManager->getPlaylistModel(), m_clipId,
                                                             m_gestureStartEdits, m_workingEdits, true);
        m_trackManager->getCommandHistory().pushExecuted(command);
    }
}

void AudioClipEditorPanel::applyDiscreteEdit(const ClipEdits& edits) {
    if (!m_trackManager || !m_clipId.isValid())
        return;
    auto command = std::make_shared<SetClipEditsCommand>(m_trackManager->getPlaylistModel(), m_clipId, edits);
    m_trackManager->getCommandHistory().pushAndExecute(command);
    m_trackManager->markModified();
    syncControlsFromModel();
}

void AudioClipEditorPanel::selectRoute(uint32_t routeId) {
    if (m_suppressCallbacks || !m_trackManager)
        return;
    const auto* pattern = m_trackManager->getPatternManager().getPattern(m_patternId);
    if (!pattern || !pattern->isAudio() || pattern->getMixerChannelId() == routeId)
        return;
    auto command = std::make_shared<SetAudioPatternMixerChannelCommand>(*m_trackManager, m_patternId, routeId);
    m_trackManager->getCommandHistory().pushAndExecute(command);
    m_routeFingerprint = 0;
    rebuildRoutes(true);
}

void AudioClipEditorPanel::onResize(int width, int height) {
    WindowPanel::onResize(width, height);
    if (!m_surface)
        return;

    const auto bounds = m_surface->getBounds();
    const float pad = 16.0f;
    const float contentWidth = bounds.width - pad * 2.0f;
    const float routeWidth = std::min(238.0f, contentWidth * 0.34f);
    m_sourceNameLabel->setBounds({bounds.x + pad + 2.0f, bounds.y + 17.0f, contentWidth - routeWidth - 20.0f, 20.0f});
    m_sourceMetaLabel->setBounds({bounds.x + pad + 2.0f, bounds.y + 43.0f, contentWidth - routeWidth - 20.0f, 16.0f});
    m_routeLabel->setBounds({bounds.right() - pad - routeWidth, bounds.y + 17.0f, routeWidth, 14.0f});
    m_routePicker->setTriggerBounds({bounds.right() - pad - routeWidth, bounds.y + 29.0f, routeWidth, 38.0f});
    m_routeHintLabel->setBounds({});

    const float controlsTop = bounds.bottom() - kControlsCardHeight - 8.0f;
    const float waveformSectionTop = bounds.y + 108.0f;
    const float waveformTop = waveformSectionTop + 22.0f;
    const float waveformBottom = controlsTop - 14.0f;
    const float waveformHeight = std::max(64.0f, waveformBottom - waveformTop);
    m_waveform->setBounds({bounds.x + 8.0f, waveformTop, bounds.width - 16.0f, waveformHeight});
    m_waveformTitleLabel->setBounds({bounds.x + pad + 9.0f, waveformSectionTop, 110.0f, 14.0f});
    m_waveformHintLabel->setBounds({bounds.x + pad, waveformSectionTop, contentWidth - 4.0f, 14.0f});

    m_instanceLabel->setBounds({bounds.x + pad, controlsTop + 10.0f, contentWidth, 14.0f});
    const float buttonGap = 6.0f;
    const float actionY = controlsTop + 8.0f;
    const float actionH = 26.0f;
    float actionX = bounds.x + pad;
    const auto placeAction = [&](const std::shared_ptr<NUIButton>& button, float width) {
        button->setBounds({actionX, actionY, width, actionH});
        actionX += width + buttonGap;
    };
    placeAction(m_reverseButton, 78.0f);
    placeAction(m_commitButton, 78.0f);
    placeAction(m_normalizeButton, 78.0f);
    placeAction(m_muteButton, 88.0f);
    if (m_makeUniqueButton->isVisible())
        placeAction(m_makeUniqueButton, 92.0f);
    const float resetWidth = 104.0f;
    m_resetButton->setBounds({bounds.right() - pad - resetWidth, actionY, resetWidth, actionH});

    const float columnGap = 30.0f;
    const float columnWidth = (contentWidth - columnGap) * 0.5f;
    const float labelWidth = 58.0f;
    const float valueWidth = 116.0f;
    const float sliderWidth = columnWidth - labelWidth - valueWidth - 18.0f;
    const auto layoutControl = [&](float x, float y, const auto& label, const auto& slider, const auto& value) {
        label->setBounds({x, y + 5.0f, labelWidth, 16.0f});
        slider->setBounds({x + labelWidth, y, sliderWidth, 24.0f});
        value->setBounds({x + labelWidth + sliderWidth + 8.0f, y + 5.0f, valueWidth, 16.0f});
    };
    const float left = bounds.x + pad + 10.0f;
    const float right = left + columnWidth + columnGap;
    m_toneSectionLabel->setBounds({left + 9.0f, controlsTop + 49.0f, columnWidth - 29.0f, 14.0f});
    m_timingSectionLabel->setBounds({right + 9.0f, controlsTop + 49.0f, columnWidth - 29.0f, 14.0f});
    layoutControl(left, controlsTop + 78.0f, m_gainLabel, m_gainSlider, m_gainValueLabel);
    layoutControl(left, controlsTop + 124.0f, m_panLabel, m_panSlider, m_panValueLabel);
    layoutControl(left, controlsTop + 170.0f, m_pitchLabel, m_pitchSlider, m_pitchValueLabel);
    layoutControl(left, controlsTop + 216.0f, m_speedLabel, m_speedSlider, m_speedValueLabel);
    layoutControl(right, controlsTop + 78.0f, m_fadeInLabel, m_fadeInSlider, m_fadeInValueLabel);
    layoutControl(right, controlsTop + 124.0f, m_fadeOutLabel, m_fadeOutSlider, m_fadeOutValueLabel);
    layoutControl(right, controlsTop + 170.0f, m_sourceStartLabel, m_sourceStartSlider, m_sourceStartValueLabel);
}

void AudioClipEditorPanel::onUpdate(double deltaTime) {
    WindowPanel::onUpdate(deltaTime);
    if (!isVisible())
        return;
    ClipInstance* clip = nullptr;
    PatternSource* pattern = nullptr;
    if (!resolveClip(clip, pattern)) {
        setVisible(false);
        return;
    }
    if (pattern->id != m_patternId) {
        m_patternId = pattern->id;
        rebuildWaveform();
        syncControlsFromModel();
        rebuildRoutes(true);
    } else if (!editsEqual(clip->edits, m_workingEdits) && !m_editGestureActive) {
        syncControlsFromModel();
    }
    rebuildRoutes(false);
}

} // namespace Audio
} // namespace Aestra
