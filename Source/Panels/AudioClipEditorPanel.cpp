// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioClipEditorPanel.h"

#include "Commands/MakeAudioClipUniqueCommand.h"
#include "Commands/RenderAudioClipCommand.h"
#include "Commands/SetAudioPatternMixerChannelCommand.h"
#include "Commands/SetClipEditsCommand.h"
#include "ChannelDisplayName.h"
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
constexpr float kMinPanelHeight = 500.0f;
constexpr size_t kWaveformBuckets = 768;
constexpr float kNormalizeTargetLinear = 0.89125094f; // -1 dBFS peak
constexpr float kControlsCardHeight = 194.0f;

class AudioClipEditorSurface final : public NUIComponent {
public:
    void onRender(NUIRenderer& renderer) override {
        if (!isVisible())
            return;
        const auto bounds = getBounds();
        auto& theme = NUIThemeManager::getInstance();
        renderer.fillRect(bounds, theme.getColor("workspaceBackground"));

        const auto card = theme.getColor("surfaceTertiary").withAlpha(0.46f);
        const auto stroke = theme.getColor("borderSubtle").withAlpha(0.68f);
        const NUIRect sourceCard{bounds.x + 8.0f, bounds.y + 8.0f, bounds.width - 16.0f, 78.0f};
        const NUIRect controlsCard{bounds.x + 8.0f, bounds.bottom() - kControlsCardHeight - 8.0f, bounds.width - 16.0f,
                                   kControlsCardHeight};
        renderer.fillRoundedRect(sourceCard, 7.0f, card);
        renderer.strokeRoundedRect(sourceCard, 7.0f, 1.0f, stroke);
        renderer.fillRoundedRect(controlsCard, 7.0f, card.withAlpha(0.38f));
        renderer.strokeRoundedRect(controlsCard, 7.0f, 1.0f, stroke);
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
           a.sourceStart == b.sourceStart;
}

} // namespace

AudioClipEditorPanel::AudioClipEditorPanel(std::shared_ptr<TrackManager> trackManager)
    : WindowPanel("AUDIO CLIP"), m_trackManager(std::move(trackManager)) {
    setMinimumPanelSize(kMinPanelWidth, kMinPanelHeight);
    buildUI();
}

void AudioClipEditorPanel::buildUI() {
    m_surface = std::make_shared<AudioClipEditorSurface>();
    auto& theme = NUIThemeManager::getInstance();

    const auto makeLabel = [&theme](const std::string& text, float size = 11.0f) {
        auto label = std::make_shared<NUILabel>(text);
        label->setFontSize(size);
        label->setTextColor(theme.getColor("textSecondary").withAlpha(0.88f));
        label->setEllipsize(true);
        return label;
    };
    const auto makeSlider = [](const std::string& name, double minimum, double maximum, double initial) {
        auto slider = std::make_shared<NUISlider>(name);
        slider->setOrientation(NUISlider::Orientation::Horizontal);
        slider->setTextBoxVisible(false);
        slider->setSliderRadius(6.0f);
        slider->setSliderThickness(5.0f);
        slider->setRange(minimum, maximum);
        slider->setValue(initial);
        return slider;
    };
    const auto styleButton = [&theme](const std::shared_ptr<NUIButton>& button) {
        button->setStyle(NUIButton::Style::Text);
        button->setFontSize(11.0f);
        button->setCornerRadius(5.0f);
        button->setGlowEnabled(false);
        button->setBackgroundColor(theme.getColor("surfaceRaised").withAlpha(0.72f));
        button->setHoverColor(theme.getColor("secondary").withAlpha(0.18f));
        button->setPressedColor(theme.getColor("secondary").withAlpha(0.28f));
        button->setTextColor(theme.getColor("textPrimary").withAlpha(0.88f));
        button->setBorderEnabled(true);
        button->setBorderWidth(1.0f);
        button->setBorderColor(theme.getColor("borderSubtle").withAlpha(0.72f));
    };

    m_sourceNameLabel = makeLabel("No audio clip selected", 14.0f);
    m_sourceNameLabel->setTextColor(theme.getColor("textPrimary"));
    m_sourceMetaLabel = makeLabel("");
    m_routeLabel = makeLabel("OUTPUT CHANNEL", 10.0f);
    m_routeHintLabel = makeLabel("Source route • Alt-drag this clip onto a channel", 10.0f);
    m_routeHintLabel->setAlignment(NUILabel::Alignment::Right);
    m_routePicker = std::make_shared<UIInsertRoutePicker>();
    m_routePicker->setOnRouteSelected([this](uint32_t routeId) { selectRoute(routeId); });

    m_waveform = std::make_shared<WaveformDisplayComponent>();
    m_instanceLabel = makeLabel("THIS CLIP", 10.0f);
    m_gainLabel = makeLabel("Gain");
    m_panLabel = makeLabel("Pan");
    m_fadeInLabel = makeLabel("Fade in");
    m_fadeOutLabel = makeLabel("Fade out");
    m_speedLabel = makeLabel("Speed");
    m_sourceStartLabel = makeLabel("Start");
    m_gainValueLabel = makeLabel("-5.0 dB", 10.0f);
    m_panValueLabel = makeLabel("Center", 10.0f);
    m_fadeInValueLabel = makeLabel("0.00 beats", 10.0f);
    m_fadeOutValueLabel = makeLabel("0.00 beats", 10.0f);
    m_speedValueLabel = makeLabel("1.00x", 10.0f);
    m_sourceStartValueLabel = makeLabel("0.000 s", 10.0f);
    m_waveformHintLabel = makeLabel("Scroll to zoom • edits are non-destructive", 10.0f);
    m_waveformHintLabel->setAlignment(NUILabel::Alignment::Right);
    for (const auto& value : {m_gainValueLabel, m_panValueLabel, m_fadeInValueLabel, m_fadeOutValueLabel,
                              m_speedValueLabel, m_sourceStartValueLabel}) {
        value->setAlignment(NUILabel::Alignment::Right);
    }

    m_gainSlider = makeSlider("Clip gain", 0.0, 2.0, DEFAULT_AUDIO_CLIP_GAIN_LINEAR);
    m_panSlider = makeSlider("Clip pan", -1.0, 1.0, 0.0);
    m_fadeInSlider = makeSlider("Fade in", 0.0, 4.0, 0.0);
    m_fadeOutSlider = makeSlider("Fade out", 0.0, 4.0, 0.0);
    m_speedSlider = makeSlider("Playback speed", 0.25, 4.0, 1.0);
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

    const auto wireSlider = [this](const std::shared_ptr<NUISlider>& slider, auto update) {
        slider->setOnDragStart([this]() { beginEditGesture(); });
        slider->setOnValueChange([this, slider, update](double value) {
            if (m_suppressCallbacks)
                return;
            if (!m_editGestureActive)
                beginEditGesture();
            update(m_workingEdits, value);
            applyWorkingEdits();
            // Double-click resets and other non-drag changes do not receive a
            // drag-end callback, so commit them immediately.
            if (!slider->isDragging())
                commitEditGesture();
        });
        slider->setOnDragEnd([this]() { commitEditGesture(); });
    };
    wireSlider(m_gainSlider, [](ClipEdits& edits, double value) {
        edits.gainLinear = static_cast<float>(value);
    });
    wireSlider(m_panSlider, [](ClipEdits& edits, double value) { edits.pan = static_cast<float>(value); });
    wireSlider(m_fadeInSlider, [](ClipEdits& edits, double value) { edits.fadeInBeats = static_cast<float>(value); });
    wireSlider(m_fadeOutSlider, [](ClipEdits& edits, double value) { edits.fadeOutBeats = static_cast<float>(value); });
    wireSlider(m_speedSlider, [](ClipEdits& edits, double value) { edits.playbackRate = static_cast<float>(value); });
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
            m_workingEdits.fadeOutBeats == 0.0f && m_workingEdits.sourceStart == 0.0)
            return;
        auto command = std::make_shared<CommitAudioClipEditsCommand>(*m_trackManager, m_clipId);
        m_trackManager->getCommandHistory().pushAndExecute(command);
        openClip(m_clipId);
    });

    const std::vector<std::shared_ptr<NUIComponent>> children{
        m_sourceNameLabel,   m_sourceMetaLabel,   m_routeLabel,        m_routeHintLabel,   m_routePicker,
        m_waveform,          m_waveformHintLabel, m_instanceLabel,     m_gainLabel,        m_panLabel,
        m_fadeInLabel,       m_fadeOutLabel,      m_speedLabel,        m_sourceStartLabel, m_gainValueLabel,
        m_panValueLabel,     m_fadeInValueLabel,  m_fadeOutValueLabel, m_speedValueLabel,  m_sourceStartValueLabel,
        m_gainSlider,        m_panSlider,         m_fadeInSlider,      m_fadeOutSlider,    m_speedSlider,
        m_sourceStartSlider, m_muteButton,        m_normalizeButton,   m_resetButton,      m_makeUniqueButton,
        m_reverseButton,     m_commitButton};
    for (const auto& child : children) {
        m_surface->addChild(child);
    }
    setContent(m_surface);
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
    setTitle(clip->name.empty() ? "AUDIO CLIP" : "AUDIO CLIP  •  " + clip->name);
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
    m_sourceMetaLabel->setText(formatFixed(buffer->durationSeconds(), 2) + " s  •  " +
                               std::to_string(buffer->sampleRate) + " Hz  •  " + channelText + "  •  " +
                               (linkedInstances > 1
                                    ? "Shared source • " + std::to_string(linkedInstances) + " instances"
                                    : "Unique source • 1 instance"));
    m_makeUniqueButton->setVisible(linkedInstances > 1);
    m_makeUniqueButton->setText(linkedInstances > 1 ? "Make unique" : "Unique");
    m_routeHintLabel->setText(linkedInstances > 1 ? "Shared source route • changes " + std::to_string(linkedInstances) +
                                                        " clips • Alt-drag to a channel"
                                                  : "Unique source route • Alt-drag this clip onto a channel");

    const size_t frameCount = static_cast<size_t>(buffer->numFrames);
    const size_t channels = static_cast<size_t>(buffer->numChannels);
    const size_t framesPerBucket = std::max<size_t>(1, (frameCount + kWaveformBuckets - 1) / kWaveformBuckets);
    m_waveformData.clear();
    m_waveformData.reserve(kWaveformBuckets * 2);
    for (size_t bucket = 0; bucket < kWaveformBuckets; ++bucket) {
        const size_t begin = bucket * framesPerBucket;
        const size_t end = std::min(frameCount, begin + framesPerBucket);
        float minimum = 0.0f;
        float maximum = 0.0f;
        for (size_t frame = begin; frame < end; ++frame) {
            float mono = 0.0f;
            for (size_t channel = 0; channel < channels; ++channel) {
                mono += buffer->interleavedData[frame * channels + channel];
            }
            mono /= static_cast<float>(channels);
            minimum = std::min(minimum, mono);
            maximum = std::max(maximum, mono);
        }
        m_waveformData.push_back(maximum);
        m_waveformData.push_back(minimum);
    }
    m_waveform->setWaveformData(m_waveformData);
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
    m_speedSlider->setValue(std::clamp<double>(m_workingEdits.playbackRate, 0.25, 4.0));
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
    const float routeWidth = std::min(270.0f, contentWidth * 0.36f);
    m_sourceNameLabel->setBounds({bounds.x + pad, bounds.y + 14.0f, contentWidth - routeWidth - 18.0f, 20.0f});
    m_sourceMetaLabel->setBounds({bounds.x + pad, bounds.y + 39.0f, contentWidth - routeWidth - 18.0f, 16.0f});
    m_routeLabel->setBounds({bounds.right() - pad - routeWidth, bounds.y + 14.0f, routeWidth, 14.0f});
    m_routePicker->setTriggerBounds({bounds.right() - pad - routeWidth, bounds.y + 34.0f, routeWidth, 30.0f});
    m_routeHintLabel->setBounds({bounds.x + pad, bounds.y + 61.0f, contentWidth, 14.0f});

    const float controlsTop = bounds.bottom() - kControlsCardHeight - 8.0f;
    const float waveformTop = bounds.y + 96.0f;
    m_waveform->setBounds(
        {bounds.x + 8.0f, waveformTop, bounds.width - 16.0f, std::max(100.0f, controlsTop - waveformTop - 8.0f)});
    m_waveformHintLabel->setBounds({bounds.x + pad, controlsTop - 23.0f, contentWidth - 4.0f, 14.0f});

    m_instanceLabel->setBounds({bounds.x + pad, controlsTop + 10.0f, contentWidth, 14.0f});
    // Right-aligned strip, laid out right-to-left so the row stays anchored to
    // the panel edge as buttons are added. Destructive operations (Reverse,
    // Commit) sit left of the reversible ones so a misclick is less likely to
    // land on the one that writes a file.
    const float buttonGap = 6.0f;
    const std::shared_ptr<NUIButton> buttonRow[] = {m_resetButton,  m_muteButton,    m_normalizeButton,
                                                    m_commitButton, m_reverseButton, m_makeUniqueButton};
    constexpr size_t buttonCount = sizeof(buttonRow) / sizeof(buttonRow[0]);
    const float availableWidth = contentWidth - buttonGap * static_cast<float>(buttonCount - 1);
    const float buttonWidth = std::clamp(availableWidth / static_cast<float>(buttonCount), 64.0f, 104.0f);

    float buttonRight = bounds.right() - pad;
    for (const auto& button : buttonRow) {
        button->setBounds({buttonRight - buttonWidth, controlsTop + 8.0f, buttonWidth, 24.0f});
        buttonRight -= buttonWidth + buttonGap;
    }

    const float columnGap = 20.0f;
    const float columnWidth = (contentWidth - columnGap) * 0.5f;
    const float labelWidth = 58.0f;
    const float valueWidth = 72.0f;
    const float sliderWidth = columnWidth - labelWidth - valueWidth - 10.0f;
    const auto layoutControl = [&](float x, float y, const auto& label, const auto& slider, const auto& value) {
        label->setBounds({x, y + 5.0f, labelWidth, 16.0f});
        slider->setBounds({x + labelWidth, y, sliderWidth, 24.0f});
        value->setBounds({x + labelWidth + sliderWidth + 8.0f, y + 5.0f, valueWidth, 16.0f});
    };
    const float left = bounds.x + pad;
    const float right = left + columnWidth + columnGap;
    layoutControl(left, controlsTop + 42.0f, m_gainLabel, m_gainSlider, m_gainValueLabel);
    layoutControl(right, controlsTop + 42.0f, m_panLabel, m_panSlider, m_panValueLabel);
    layoutControl(left, controlsTop + 88.0f, m_fadeInLabel, m_fadeInSlider, m_fadeInValueLabel);
    layoutControl(right, controlsTop + 88.0f, m_fadeOutLabel, m_fadeOutSlider, m_fadeOutValueLabel);
    layoutControl(left, controlsTop + 134.0f, m_speedLabel, m_speedSlider, m_speedValueLabel);
    layoutControl(right, controlsTop + 134.0f, m_sourceStartLabel, m_sourceStartSlider, m_sourceStartValueLabel);
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
