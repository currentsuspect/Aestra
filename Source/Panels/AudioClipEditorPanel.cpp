// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioClipEditorPanel.h"

#include "Commands/MakeAudioClipUniqueCommand.h"
#include "Commands/SetAudioPatternMixerChannelCommand.h"
#include "Commands/SetClipEditsCommand.h"
#include "NUIButton.h"
#include "NUIDropdown.h"
#include "NUILabel.h"
#include "NUIRenderer.h"
#include "NUISlider.h"
#include "NUIThemeSystem.h"
#include "SampleEditorPanel.h"
#include "TrackManager.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

using namespace AestraUI;

namespace Aestra {
namespace Audio {

namespace {
constexpr float kMinPanelWidth = 660.0f;
constexpr float kMinPanelHeight = 430.0f;
constexpr size_t kWaveformBuckets = 768;

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
        const NUIRect controlsCard{bounds.x + 8.0f, bounds.bottom() - 152.0f, bounds.width - 16.0f, 144.0f};
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

bool editsEqual(const ClipEdits& a, const ClipEdits& b) {
    return a.fadeInBeats == b.fadeInBeats && a.fadeOutBeats == b.fadeOutBeats && a.gain == b.gain &&
           a.gainLinear == b.gainLinear && a.pitchSemitones == b.pitchSemitones &&
           a.timeStretchRatio == b.timeStretchRatio && a.pan == b.pan && a.muted == b.muted &&
           a.playbackRate == b.playbackRate && a.sourceStart == b.sourceStart;
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
    m_routeLabel = makeLabel("OUTPUT INSERT", 10.0f);
    m_routeHintLabel = makeLabel("Source route • linked clips follow this destination", 10.0f);
    m_routeHintLabel->setAlignment(NUILabel::Alignment::Right);
    m_routeDropdown = std::make_shared<NUIDropdown>();
    m_routeDropdown->setPlaceholderText("Choose an insert");
    m_routeDropdown->setMaxVisibleItems(9);
    m_routeDropdown->setOnSelectionChanged([this](int index) { selectRouteIndex(index); });

    m_waveform = std::make_shared<WaveformDisplayComponent>();
    m_instanceLabel = makeLabel("THIS CLIP", 10.0f);
    m_gainLabel = makeLabel("Gain");
    m_panLabel = makeLabel("Pan");
    m_fadeInLabel = makeLabel("Fade in");
    m_fadeOutLabel = makeLabel("Fade out");
    m_gainValueLabel = makeLabel("100%", 10.0f);
    m_panValueLabel = makeLabel("Center", 10.0f);
    m_fadeInValueLabel = makeLabel("0.00 beats", 10.0f);
    m_fadeOutValueLabel = makeLabel("0.00 beats", 10.0f);
    for (const auto& value : {m_gainValueLabel, m_panValueLabel, m_fadeInValueLabel, m_fadeOutValueLabel}) {
        value->setAlignment(NUILabel::Alignment::Right);
    }

    m_gainSlider = makeSlider("Clip gain", 0.0, 2.0, 1.0);
    m_panSlider = makeSlider("Clip pan", -1.0, 1.0, 0.0);
    m_fadeInSlider = makeSlider("Fade in", 0.0, 4.0, 0.0);
    m_fadeOutSlider = makeSlider("Fade out", 0.0, 4.0, 0.0);
    m_muteButton = std::make_shared<NUIButton>("Mute clip");
    m_muteButton->setToggleable(true);
    m_resetButton = std::make_shared<NUIButton>("Reset instance");
    m_makeUniqueButton = std::make_shared<NUIButton>("Make unique");
    styleButton(m_muteButton);
    styleButton(m_resetButton);
    styleButton(m_makeUniqueButton);

    const auto wireSlider = [this](const std::shared_ptr<NUISlider>& slider, auto update) {
        slider->setOnDragStart([this]() { beginEditGesture(); });
        slider->setOnValueChange([this, update](double value) {
            if (m_suppressCallbacks)
                return;
            update(m_workingEdits, value);
            applyWorkingEdits();
        });
        slider->setOnDragEnd([this]() { commitEditGesture(); });
    };
    wireSlider(m_gainSlider, [](ClipEdits& edits, double value) {
        edits.gain = static_cast<float>(value);
        edits.gainLinear = static_cast<float>(value);
    });
    wireSlider(m_panSlider, [](ClipEdits& edits, double value) { edits.pan = static_cast<float>(value); });
    wireSlider(m_fadeInSlider, [](ClipEdits& edits, double value) { edits.fadeInBeats = static_cast<float>(value); });
    wireSlider(m_fadeOutSlider, [](ClipEdits& edits, double value) { edits.fadeOutBeats = static_cast<float>(value); });

    m_muteButton->setOnToggle([this](bool muted) {
        if (m_suppressCallbacks)
            return;
        auto edits = m_workingEdits;
        edits.muted = muted;
        applyDiscreteEdit(edits);
    });
    m_resetButton->setOnClick([this]() {
        ClipEdits reset;
        applyDiscreteEdit(reset);
    });
    m_makeUniqueButton->setOnClick([this]() {
        if (!m_trackManager || !m_clipId.isValid())
            return;
        auto command = std::make_shared<MakeAudioClipUniqueCommand>(*m_trackManager, m_clipId);
        m_trackManager->getCommandHistory().pushAndExecute(command);
        openClip(m_clipId);
    });

    const std::vector<std::shared_ptr<NUIComponent>> children{
        m_sourceNameLabel, m_sourceMetaLabel, m_routeLabel,    m_routeHintLabel,   m_routeDropdown,
        m_waveform,        m_instanceLabel,   m_gainLabel,     m_panLabel,         m_fadeInLabel,
        m_fadeOutLabel,    m_gainValueLabel,  m_panValueLabel, m_fadeInValueLabel, m_fadeOutValueLabel,
        m_gainSlider,      m_panSlider,       m_fadeInSlider,  m_fadeOutSlider,    m_muteButton,
        m_resetButton,     m_makeUniqueButton};
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
    const auto& payload = std::get<AudioSlicePayload>(pattern->payload);
    const auto* source = m_trackManager->getSourceManager().getSource(payload.audioSourceId);
    const auto* buffer = source ? source->getRawBuffer() : nullptr;
    if (!source || !buffer || !buffer->isValid()) {
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

    m_sourceNameLabel->setText(source->getName().empty() ? clip->name : source->getName());
    const char* channelText =
        buffer->numChannels == 1 ? "Mono" : (buffer->numChannels == 2 ? "Stereo" : "Multichannel");
    m_sourceMetaLabel->setText(formatFixed(buffer->durationSeconds(), 2) + " s  •  " +
                               std::to_string(buffer->sampleRate) + " Hz  •  " + channelText + "  •  " +
                               std::to_string(linkedInstances) +
                               (linkedInstances == 1 ? " linked clip" : " linked clips"));
    m_makeUniqueButton->setVisible(linkedInstances > 1);

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
    m_routeDropdown->clearItems();
    m_routeIds.clear();
    m_routeIds.push_back(MASTER_MIXER_CHANNEL_ID);
    m_routeDropdown->addItem("Master", 0);
    for (size_t index = 0; index < m_trackManager->getChannelCount(); ++index) {
        const auto* channel = m_trackManager->getChannel(index);
        if (!channel)
            continue;
        m_routeIds.push_back(channel->getChannelId());
        const std::string name =
            channel->getName().empty() ? "Insert " + std::to_string(index + 1) : channel->getName();
        m_routeDropdown->addItem(std::to_string(index + 1) + "  •  " + name, static_cast<int>(index + 1));
    }
    uint32_t selectedRoute = MASTER_MIXER_CHANNEL_ID;
    if (const auto* pattern = m_trackManager->getPatternManager().getPattern(m_patternId)) {
        selectedRoute = pattern->getMixerChannelId();
    }
    const auto selected = std::find(m_routeIds.begin(), m_routeIds.end(), selectedRoute);
    m_routeDropdown->setSelectedIndex(
        selected == m_routeIds.end() ? 0 : static_cast<int>(std::distance(m_routeIds.begin(), selected)));
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
    m_muteButton->setToggled(m_workingEdits.muted);
    m_muteButton->setText(m_workingEdits.muted ? "Unmute clip" : "Mute clip");
    m_suppressCallbacks = false;
    updateValueLabels();
}

void AudioClipEditorPanel::updateValueLabels() {
    m_gainValueLabel->setText(std::to_string(static_cast<int>(std::round(m_workingEdits.gainLinear * 100.0f))) + "%");
    if (std::abs(m_workingEdits.pan) < 0.005f) {
        m_panValueLabel->setText("Center");
    } else {
        const char side = m_workingEdits.pan < 0.0f ? 'L' : 'R';
        m_panValueLabel->setText(std::to_string(static_cast<int>(std::round(std::abs(m_workingEdits.pan) * 100.0f))) +
                                 side);
    }
    m_fadeInValueLabel->setText(formatFixed(m_workingEdits.fadeInBeats, 2) + " beats");
    m_fadeOutValueLabel->setText(formatFixed(m_workingEdits.fadeOutBeats, 2) + " beats");
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

void AudioClipEditorPanel::selectRouteIndex(int index) {
    if (m_suppressCallbacks || !m_trackManager || index < 0 || static_cast<size_t>(index) >= m_routeIds.size())
        return;
    const auto* pattern = m_trackManager->getPatternManager().getPattern(m_patternId);
    if (!pattern || !pattern->isAudio() || pattern->getMixerChannelId() == m_routeIds[static_cast<size_t>(index)])
        return;
    auto command = std::make_shared<SetAudioPatternMixerChannelCommand>(*m_trackManager, m_patternId,
                                                                        m_routeIds[static_cast<size_t>(index)]);
    m_trackManager->getCommandHistory().pushAndExecute(command);
    m_routeFingerprint = 0;
    rebuildRoutes(true);
}

void AudioClipEditorPanel::onResize(int width, int height) {
    const auto panelBounds = getBounds();
    if (panelBounds.width < kMinPanelWidth || panelBounds.height < kMinPanelHeight) {
        setBounds({panelBounds.x, panelBounds.y, std::max(panelBounds.width, kMinPanelWidth),
                   std::max(panelBounds.height, kMinPanelHeight)});
        return;
    }
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
    m_routeDropdown->setBounds({bounds.right() - pad - routeWidth, bounds.y + 34.0f, routeWidth, 30.0f});
    m_routeHintLabel->setBounds({bounds.x + pad, bounds.y + 61.0f, contentWidth, 14.0f});

    const float controlsTop = bounds.bottom() - 152.0f;
    const float waveformTop = bounds.y + 96.0f;
    m_waveform->setBounds(
        {bounds.x + 8.0f, waveformTop, bounds.width - 16.0f, std::max(100.0f, controlsTop - waveformTop - 8.0f)});

    m_instanceLabel->setBounds({bounds.x + pad, controlsTop + 10.0f, contentWidth, 14.0f});
    const float buttonWidth = 104.0f;
    m_makeUniqueButton->setBounds(
        {bounds.right() - pad - buttonWidth * 3.0f - 12.0f, controlsTop + 8.0f, buttonWidth, 24.0f});
    m_muteButton->setBounds({bounds.right() - pad - buttonWidth * 2.0f - 6.0f, controlsTop + 8.0f, buttonWidth, 24.0f});
    m_resetButton->setBounds({bounds.right() - pad - buttonWidth, controlsTop + 8.0f, buttonWidth, 24.0f});

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
    }
    rebuildRoutes(false);
}

} // namespace Audio
} // namespace Aestra
