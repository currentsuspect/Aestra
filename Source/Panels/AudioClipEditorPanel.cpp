// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioClipEditorPanel.h"

#include "ChannelDisplayName.h"
#include "Commands/MakeAudioClipUniqueCommand.h"
#include "Commands/RenderAudioClipCommand.h"
#include "Commands/SetAudioPatternMixerChannelCommand.h"
#include "Commands/SetClipEditsCommand.h"
#include "Commands/TrimClipCommand.h"
#include "Commands/CommandTransaction.h"
#include "Models/ClipFit.h"
#include "Models/StretchTargets.h"
#include "Models/ClipRenderService.h"
#include "NUIButton.h"
#include "NUILabel.h"
#include "NUIRenderer.h"
#include "NUISlider.h"
#include "Widgets/FlatSlider.h"
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
// Minimum height that keeps the waveform region usable. The layout runs on
// the surface (panel height minus the 28px WindowPanel title bar); the
// controls card starts at surfaceBottom - kControlsCardHeight - 8 - 14 and
// the waveform top is at surfaceY + 130, so a 64px waveform needs a surface
// of 458px, i.e. a panel of 486px. The card has since grown 14px: the minimum is 520, which leaves ~96px of waveform and
// guarantees the card can never overlap the waveform (triage 2026-08-14).
constexpr float kMinPanelHeight = 520.0f;
constexpr size_t kWaveformBuckets = 768;
constexpr float kNormalizeTargetLinear = 0.89125094f; // -1 dBFS peak
constexpr float kControlsCardHeight = 188.0f; // the two titled columns
constexpr float kHeaderHeight = 62.0f;        // name, facts, output chip
constexpr float kActionRowHeight = 42.0f;     // Reverse, Commit, Normalize, Mute, Reset

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

using ClipRangeSlider = FlatSlider; // the shared sharp slider; a bipolar one fills from zero

class AudioClipEditorSurface final : public NUIComponent {
public:
    void onRender(NUIRenderer& renderer) override {
        if (!isVisible())
            return;
        const auto bounds = getBounds();
        // One panel colour. What you read (waveform, value wells) sits on the darker bed; there are no cards inside
        // the panel, only one-pixel rules between the regions and under each section title.
        renderer.fillRect(bounds, surface1Color());

        const NUIColor rule = NUIThemeManager::getInstance().getColor("borderSubtle");
        const float controlsTop = bounds.bottom() - kControlsCardHeight;
        renderer.fillRect({bounds.x, controlsTop, bounds.width, 1.0f}, rule);
        const float splitX = bounds.x + bounds.width * 0.5f;
        renderer.fillRect({splitX, controlsTop, 1.0f, kControlsCardHeight}, rule);
        const float pad = 16.0f;
        const float titleRuleY = controlsTop + 30.0f;
        renderer.fillRect({bounds.x + pad, titleRuleY, bounds.width * 0.5f - pad * 2.0f, 1.0f}, rule);
        renderer.fillRect({splitX + pad, titleRuleY, bounds.width * 0.5f - pad * 2.0f, 1.0f}, rule);
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
        slider->setSliderRadius(3.0f);
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
        button->setCornerRadius(2.0f);
        button->setGlowEnabled(false);
        button->setBackgroundColor(surface2Color());
        button->setHoverColor(surface3Color());
        button->setPressedColor(surface3Color());
        button->setTextColor(secondaryTextColor());
        button->setBorderEnabled(false); // flat keys, no outline
    };

    m_sourceNameLabel = makeLabel("No audio clip selected", 15.0f);
    m_sourceNameLabel->setTextColor(primaryTextColor());
    m_sourceMetaLabel = makeLabel("");
    m_routeLabel = makeLabel("OUT", 10.0f);
    m_routeLabel->setTextColor(mutedTextColor());
    m_routeLabel->setAlignment(NUILabel::Alignment::Right); m_routeLabel->setEllipsize(false);
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
    m_speedValueLabel = makeLabel("1.00x", 11.5f);
    m_sourceStartValueLabel = makeLabel("0.000 s", 11.5f);
    m_waveformHintLabel = makeLabel("Scroll to zoom • edits are non-destructive", 11.0f);
    m_waveformHintLabel->setAlignment(NUILabel::Alignment::Left);
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
    m_speedSlider = makeSlider("Speed", 0.25, 4.0, 1.0);
    m_sourceStartSlider = makeSlider("Source start", 0.0, 1.0, 0.0);
    m_fitLabel = makeLabel("Fit");
    for (size_t i = 0; i < m_fitButtons.size(); ++i) {
        m_fitButtons[i] = std::make_shared<NUIButton>("");
        styleButton(m_fitButtons[i]);
        m_fitButtons[i]->setVisible(false); // shown by refreshFitTargets() once the clip's length is known
        m_fitButtons[i]->setOnClick([this, i]() {
            if (m_fitBars[i] > 0)
                applyFitToBars(m_fitBars[i]);
        });
    }
    m_muteButton = std::make_shared<NUIButton>("Mute clip");
    m_muteButton->setToggleable(true);
    m_normalizeButton = std::make_shared<NUIButton>("Normalize");
    m_resetButton = std::make_shared<NUIButton>("Reset instance");
    m_makeUniqueButton = std::make_shared<NUIButton>("Make unique");
    m_reverseButton = std::make_shared<NUIButton>("Reverse");
    m_commitButton = std::make_shared<NUIButton>("Commit");
    m_previewButton = std::make_shared<NUIButton>("Preview");
    m_previewButton->setTooltip("Hear this clip's part of the source at its speed, pitch and level");
    styleButton(m_previewButton);
    m_previewButton->setOnClick([this]() { togglePreview(); });
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
    wireSlider(
        m_sourceStartSlider,
        [this](ClipEdits& edits, double value) {
            const double projectRate =
                m_trackManager ? std::max(1.0, m_trackManager->getPlaylistModel().getProjectSampleRate()) : 48000.0;
            edits.sourceStart = value * projectRate;
        },
        true);

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
        m_sourceNameLabel,    m_sourceMetaLabel,    m_routeLabel,        m_routeHintLabel,  m_routePicker,
        m_waveform,           m_waveformTitleLabel, m_waveformHintLabel, m_instanceLabel,   m_toneSectionLabel,
        m_timingSectionLabel, m_gainLabel,          m_panLabel,          m_fadeInLabel,     m_fadeOutLabel,
        m_pitchLabel,         m_speedLabel,         m_sourceStartLabel,  m_gainValueLabel,  m_panValueLabel,
        m_fadeInValueLabel,   m_fadeOutValueLabel,  m_pitchValueLabel,   m_speedValueLabel, m_sourceStartValueLabel,
        m_gainSlider,         m_panSlider,          m_fadeInSlider,      m_fadeOutSlider,   m_pitchSlider,
        m_speedSlider,        m_sourceStartSlider,  m_muteButton,        m_fitLabel,        m_fitButtons[0],
        m_fitButtons[1],      m_fitButtons[2],      m_fitButtons[3],     m_fitButtons[4],   m_normalizeButton,
        m_resetButton,        m_makeUniqueButton,   m_reverseButton,     m_commitButton,    m_previewButton};
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
    commitEditGesture(); // V8-W6: an edit in progress on the previous clip ends as its own undo step
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
    m_waveform->setDurationSeconds(m_sourceDurationSeconds);
    m_sourcePeak = buffer->peakAbs();

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
                const float a = buffer->sampleAt(frame * channels + channel);
                const float b = buffer->sampleAt(nextFrame * channels + channel);
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
    // The waveform wears its channel's colour, the same one as that channel's rows and clips; Master has none, so the
    // signal stays the neutral bone.
    NUIColor signal{0.0f, 0.0f, 0.0f, 0.0f};
    for (const auto& route : routes)
        if (route.id == selectedRoute && route.colorArgb != 0)
            signal = NUIColor::fromARGB(route.colorArgb);
    m_waveform->setColor(signal);
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
    refreshFitTargets();
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
        // The graph rebuild alone doesn't repaint the arrangement: without this
        // the playlist FBO keeps drawing the pre-edit waveform indefinitely.
        if (m_onClipEditsCommitted)
            m_onClipEditsCommitted();
    }
}

bool AudioClipEditorPanel::resolveFitContentSeconds(double& contentSeconds) const {
    if (!m_trackManager || !m_clipId.isValid())
        return false;
    ClipInstance* clip = nullptr;
    PatternSource* pattern = nullptr;
    if (!resolveClip(clip, pattern))
        return false;
    const auto* payload = std::get_if<AudioSlicePayload>(&pattern->payload);
    if (!payload)
        return false;
    const auto* source = m_trackManager->getSourceManager().getSource(payload->audioSourceId);
    const auto* buffer = source ? source->getRawBuffer() : nullptr;
    if (!source || !buffer || !buffer->isValid())
        return false;

    // Region-aware content seconds: what the render path actually consumes
    // after trim/offset — same authority rebuildWaveform draws from.
    const double sampleRate = std::max(1.0, m_trackManager->getPlaylistModel().getProjectSampleRate());
    const auto region =
        ClipRenderService(m_trackManager->getSourceManager(), m_trackManager->getPatternManager())
            .resolveClipRegion(*clip, sampleRate);
    if (!region.isValid())
        return false;
    contentSeconds = static_cast<double>(region.frameCount) / static_cast<double>(buffer->sampleRate);
    return true;
}

void AudioClipEditorPanel::refreshFitTargets() {
    double contentSeconds = 0.0;
    const double bpm = m_trackManager ? m_trackManager->getPlaylistModel().getBPM() : 0.0;
    m_fitCachedBpm = bpm;
    Audio::StretchTargetList list;
    if (bpm > 0.0 && resolveFitContentSeconds(contentSeconds)) {
        // L, in bars: the clip's audible length at the project tempo (4/4 today, as ClipFit.h).
        const double lengthBars = contentSeconds * bpm / (60.0 * Audio::FIT_BARS_BEATS_PER_BAR);
        list = Audio::rankStretchTargets(lengthBars);
    }
    for (size_t i = 0; i < m_fitButtons.size(); ++i) {
        if (i < list.ranked.size()) {
            const auto& target = list.ranked[i];
            m_fitBars[i] = target.bars;
            m_fitButtons[i]->setText(std::to_string(target.bars));
            // The interface describes what the engine does: varispeed, pitch follows (never "stretch").
            m_fitButtons[i]->setTooltip((i == 0 ? "Best fit: " : "Fit: ") + Audio::formatStretchTarget(target));
            m_fitButtons[i]->setVisible(true);
        } else {
            m_fitBars[i] = 0;
            m_fitButtons[i]->setVisible(false);
        }
    }
}

void AudioClipEditorPanel::applyFitToBars(int bars) {
    double contentSeconds = 0.0;
    if (!resolveFitContentSeconds(contentSeconds))
        return;
    ClipInstance* clip = nullptr;
    PatternSource* pattern = nullptr;
    if (!resolveClip(clip, pattern))
        return;

    const auto fit = Audio::computeFitToBars(contentSeconds, m_trackManager->getPlaylistModel().getBPM(), bars,
                                             clip->edits.pitchSemitones);
    if (!fit)
        return;

    // Span and rate land as ONE undoable step. SetClipEditsCommand is pushed
    // FIRST on purpose: TrimClipCommand derives the canonical durationSeconds
    // from the clip's varispeed live at ITS execute, so the trim must see the
    // post-fit rate — otherwise durationSeconds keeps the pre-fit varispeed
    // and every durationSeconds-fed path (serializer, region preview, render
    // extraction) reads a stale source window.
    ClipEdits edits = clip->edits;
    edits.playbackRate = fit->playbackRate;
    auto& history = m_trackManager->getCommandHistory();
    history.beginTransaction(std::make_shared<CommandTransaction>("Fit clip to bars"));
    history.pushAndExecute(std::make_shared<Audio::SetClipEditsCommand>(
        m_trackManager->getPlaylistModel(), m_clipId, edits));
    history.pushAndExecute(std::make_shared<Audio::TrimClipCommand>(
        m_trackManager->getPlaylistModel(), m_clipId, -1.0, clip->startBeat + fit->durationBeats));
    history.commitTransaction();

    m_trackManager->markModified();
    syncControlsFromModel();
    if (m_onClipEditsCommitted)
        m_onClipEditsCommitted();
}

void AudioClipEditorPanel::applyDiscreteEdit(const ClipEdits& edits) {
    if (!m_trackManager || !m_clipId.isValid())
        return;
    auto command = std::make_shared<SetClipEditsCommand>(m_trackManager->getPlaylistModel(), m_clipId, edits);
    m_trackManager->getCommandHistory().pushAndExecute(command);
    m_trackManager->markModified();
    syncControlsFromModel();
    if (m_onClipEditsCommitted)
        m_onClipEditsCommitted();
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

    // Header: the clip's name and facts at the left, the output as a small chip at the right.
    const float chipWidth = std::min(150.0f, contentWidth * 0.3f);
    const float chipX = bounds.right() - pad - chipWidth;
    m_sourceNameLabel->setBounds({bounds.x + pad, bounds.y + 12.0f, chipX - bounds.x - pad - 52.0f, 20.0f});
    m_sourceMetaLabel->setBounds({bounds.x + pad, bounds.y + 36.0f, chipX - bounds.x - pad - 52.0f, 16.0f});
    m_routeLabel->setBounds({chipX - 44.0f, bounds.y + 19.0f, 38.0f, 14.0f});
    m_routePicker->setTriggerBounds({chipX, bounds.y + 14.0f, chipWidth, 26.0f});
    m_routeHintLabel->setBounds({});
    m_waveformTitleLabel->setBounds({});
    m_instanceLabel->setBounds({});

    const float controlsTop = bounds.bottom() - kControlsCardHeight;
    const float actionY = controlsTop - kActionRowHeight + 8.0f;
    const float waveformTop = bounds.y + kHeaderHeight;
    const float waveformBottom = actionY - 26.0f; // the hint line sits between the waveform and the actions
    const float waveformHeight = std::max(0.0f, waveformBottom - waveformTop);
    const bool waveformUsable = waveformHeight >= 64.0f;
    m_waveform->setVisible(waveformUsable);
    if (waveformUsable) {
        m_waveform->setBounds({bounds.x + pad, waveformTop, contentWidth, waveformHeight});
    } else {
        m_waveform->setBounds({});
    }
    m_waveformHintLabel->setBounds({bounds.x + pad, waveformBottom + 6.0f, contentWidth, 14.0f});

    // Actions: flat keys at the left, Reset at the right, shrinking together when the panel is narrow.
    const float buttonGap = 6.0f;
    const float actionH = 24.0f;
    const float resetWidth = 104.0f;
    float actionX = bounds.x + pad;
    const float totalRequired = buttonGap * 4.0f + 78.0f + 78.0f + 78.0f + 88.0f + 88.0f +
                                (m_makeUniqueButton->isVisible() ? buttonGap + 100.0f : 0.0f);
    const float available = (bounds.right() - pad - resetWidth) - actionX - buttonGap;
    const float scale = totalRequired > 0.0f ? std::clamp(available / totalRequired, 0.0f, 1.0f) : 1.0f;
    const auto placeAction = [&](const std::shared_ptr<NUIButton>& button, float width) {
        const float w = width * scale;
        button->setBounds({actionX, actionY, w, actionH});
        actionX += w + buttonGap * scale;
    };
    placeAction(m_previewButton, 78.0f);
    placeAction(m_reverseButton, 78.0f);
    placeAction(m_commitButton, 78.0f);
    placeAction(m_normalizeButton, 88.0f);
    placeAction(m_muteButton, 88.0f);
    if (m_makeUniqueButton->isVisible())
        placeAction(m_makeUniqueButton, 100.0f);
    m_resetButton->setBounds({bounds.right() - pad - resetWidth, actionY, resetWidth, actionH});
    m_instanceLabel->setBounds({});

    // Two columns of four rows under titled rules.
    const float columnWidth = bounds.width * 0.5f - pad * 2.0f;
    const float labelWidth = 60.0f;
    const float valueWidth = 78.0f;
    const float sliderWidth = std::max(0.0f, columnWidth - labelWidth - valueWidth - 12.0f);
    const auto layoutControl = [&](float x, float y, const auto& label, const auto& slider, const auto& value) {
        label->setBounds({x, y + 4.0f, labelWidth, 16.0f});
        slider->setBounds({x + labelWidth, y, sliderWidth, 24.0f});
        value->setBounds({x + labelWidth + sliderWidth + 12.0f, y + 4.0f, valueWidth, 16.0f});
    };
    const float left = bounds.x + pad;
    const float right = bounds.x + bounds.width * 0.5f + pad;
    m_toneSectionLabel->setBounds({left, controlsTop + 10.0f, columnWidth, 14.0f});
    m_timingSectionLabel->setBounds({right, controlsTop + 10.0f, columnWidth, 14.0f});
    const float rowTop = controlsTop + 40.0f;
    const float rowStep = 32.0f;
    layoutControl(left, rowTop, m_gainLabel, m_gainSlider, m_gainValueLabel);
    layoutControl(left, rowTop + rowStep, m_panLabel, m_panSlider, m_panValueLabel);
    layoutControl(left, rowTop + rowStep * 2.0f, m_pitchLabel, m_pitchSlider, m_pitchValueLabel);
    layoutControl(left, rowTop + rowStep * 3.0f, m_speedLabel, m_speedSlider, m_speedValueLabel);
    layoutControl(right, rowTop, m_fadeInLabel, m_fadeInSlider, m_fadeInValueLabel);
    layoutControl(right, rowTop + rowStep, m_fadeOutLabel, m_fadeOutSlider, m_fadeOutValueLabel);
    layoutControl(right, rowTop + rowStep * 2.0f, m_sourceStartLabel, m_sourceStartSlider, m_sourceStartValueLabel);

    // Fit-to-bars (#747): the fourth row of the right column, five keys that end inside it.
    const float fitY = rowTop + rowStep * 3.0f;
    m_fitLabel->setBounds({right, fitY + 4.0f, labelWidth, 16.0f});
    {
        const float fitGap = 1.0f;
        float fitX = right + labelWidth;
        const float fitW = std::max(0.0f, (columnWidth - labelWidth - 4.0f * fitGap) / 5.0f);
        for (const auto& fitButton : m_fitButtons) {
            fitButton->setBounds({fitX, fitY, fitW, 24.0f});
            fitX += fitW + fitGap;
        }
    }
}

// Preview plays the clip's part of the source (from its start, for its length) at its varispeed and level, so you can
// hear what the clip holds while editing it. Fades and pan are not applied: this is the source, not the mix.
void AudioClipEditorPanel::togglePreview() {
    if (!m_onPreviewRequested)
        return;
    if (m_previewSecondsLeft > 0.0) {
        m_previewSecondsLeft = 0.0;
        m_onPreviewRequested({}, 0.0, 0.0, 1.0f, 0.0f);
    } else {
        ClipInstance* clip = nullptr;
        PatternSource* pattern = nullptr;
        if (!resolveClip(clip, pattern))
            return;
        const auto* payload = std::get_if<AudioSlicePayload>(&pattern->payload);
        const auto* source = payload ? m_trackManager->getSourceManager().getSource(payload->audioSourceId) : nullptr;
        if (!source || source->getFilePath().empty())
            return;
        const float rate =
            std::max(0.01f, m_workingEdits.playbackRate * std::pow(2.0f, m_workingEdits.pitchSemitones / 12.0f));
        const double length = std::clamp(clip->durationSeconds > 0.0 ? clip->durationSeconds : 4.0, 0.1, 60.0);
        const float gainDb = 20.0f * std::log10(std::max(1e-4f, m_workingEdits.gainLinear));
        m_previewSecondsLeft = length;
        m_onPreviewRequested(source->getFilePath(), m_workingEdits.sourceStart, length, rate, gainDb);
    }
    m_previewButton->setText(m_previewSecondsLeft > 0.0 ? "Stop" : "Preview");
    repaint();
}

void AudioClipEditorPanel::onUpdate(double deltaTime) {
    WindowPanel::onUpdate(deltaTime);
    if (m_previewSecondsLeft > 0.0) {
        m_previewSecondsLeft -= deltaTime;
        if (m_previewSecondsLeft <= 0.0) {
            m_previewSecondsLeft = 0.0;
            m_previewButton->setText("Preview");
            repaint();
        }
    }
    if (!isVisible())
        return;
    ClipInstance* clip = nullptr;
    PatternSource* pattern = nullptr;
    if (!resolveClip(clip, pattern)) {
        m_editGestureActive = false; // the clip is gone: nothing left to commit
        setVisible(false);
        return;
    }
    // The fit targets depend on the tempo: a BPM change re-ranks them without touching the clip.
    if (m_trackManager && std::abs(m_trackManager->getPlaylistModel().getBPM() - m_fitCachedBpm) > 1.0e-6)
        refreshFitTargets();
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
