// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "TrackUIComponent.h"
#include "TrackManagerUI.h"
#include "../AestraUI/Platform/NUIPlatformBridge.h"
#include "MixerChannel.h"
#include "TrackManager.h"
#include "PlaylistModel.h"
#include "PatternManager.h"
#include "WaveformCache.h"
#include "MeterSnapshot.h"
#include "ChannelSlotMap.h"
#include "NUIContextMenu.h"
#include "Commands/SetVolumeCommand.h"
#include "Commands/SetPanCommand.h"
#include "Commands/SetMuteCommand.h"
#include "Commands/SetSoloCommand.h"

#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraCore/include/AestraLog.h"
#include "../AestraCore/include/AestraUnifiedProfiler.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <chrono>
#include <string_view>

namespace Aestra {
namespace Audio {

namespace {

AestraUI::NUIComponent* getRootComponent(AestraUI::NUIComponent* component) {
    AestraUI::NUIComponent* root = component;
    while (root && root->getParent()) {
        root = root->getParent();
    }
    return root;
}

void detachContextMenu(const std::shared_ptr<AestraUI::NUIContextMenu>& menu) {
    if (!menu) return;
    if (auto* parent = menu->getParent()) {
        parent->removeChild(menu);
    }
}

void attachAndShowContextMenu(AestraUI::NUIComponent* owner,
                              const std::shared_ptr<AestraUI::NUIContextMenu>& menu,
                              const AestraUI::NUIPoint& position) {
    if (!owner || !menu) return;
    AestraUI::NUIComponent* root = getRootComponent(owner);
    if (!root) root = owner;
    root->addChild(menu);
    menu->showAt(position);
    root->repaint();
}

bool parseTrailingTrackNumber(const std::string& trackName, uint32_t& trackNumberOut) {
    const size_t numberPos = trackName.find_last_not_of("0123456789");
    if (numberPos == std::string::npos || numberPos >= trackName.length() - 1) return false;
    const std::string_view numberStr(trackName.c_str() + numberPos + 1, trackName.length() - numberPos - 1);
    uint32_t parsed = 0;
    const char* begin = numberStr.data();
    const char* end = begin + numberStr.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed == 0) return false;
    trackNumberOut = parsed;
    return true;
}

std::string truncateClipLabel(const std::string& text, float availableWidth, float charWidth, size_t minChars = 4) {
    if (text.empty() || availableWidth <= 0.0f || charWidth <= 0.0f) return {};
    const size_t maxChars = static_cast<size_t>(availableWidth / charWidth);
    if (maxChars < minChars) return {};
    if (text.length() <= maxChars) return text;
    if (maxChars <= 2) return {};
    return text.substr(0, maxChars - 2) + "..";
}

AestraUI::NUIColor restrainDawColor(const AestraUI::NUIColor& color, float brightnessScale, float saturationScale,
                                    float alpha) {
    const float luma = (0.2126f * color.r) + (0.7152f * color.g) + (0.0722f * color.b);
    const float tonedR = ((color.r - luma) * saturationScale + luma) * brightnessScale;
    const float tonedG = ((color.g - luma) * saturationScale + luma) * brightnessScale;
    const float tonedB = ((color.b - luma) * saturationScale + luma) * brightnessScale;
    return AestraUI::NUIColor(std::clamp(tonedR, 0.0f, 1.0f),
                              std::clamp(tonedG, 0.0f, 1.0f),
                              std::clamp(tonedB, 0.0f, 1.0f),
                              alpha >= 0.0f ? alpha : color.a);
}

} // namespace

// =============================================================================
// SECTION: Construction & Destruction
// =============================================================================

TrackUIComponent::TrackUIComponent(PlaylistLaneID laneId, std::shared_ptr<MixerChannel> channel, TrackManager* trackManager)
    : m_laneId(laneId)
    , m_channel(channel)
    , m_trackManager(trackManager)
{
    // Log::info("TrackUIComponent ctor: " + m_laneId.toString());
    if (!m_channel) {
        Log::error("TrackUIComponent created with null channel");
        return;
    }

    // Create track name label
    m_nameLabel = std::make_shared<AestraUI::NUILabel>();
    
    std::string name = "Lane";
    if (m_trackManager) {
        if (auto lane = m_trackManager->getPlaylistModel().getLane(m_laneId)) {
            name = lane->name;
        }
    }
    m_nameLabel->setText(name.empty() ? m_channel->getName() : name);

    {
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
        // Use large font for track names
        m_nameLabel->setFontSize(themeManager.getFontSize("l"));
    }
    m_nameLabel->setEllipsize(true);
    updateTrackNameColors();
    addChild(m_nameLabel);

    // Volume fader removed from track header to reduce clutter.
    m_volumeFader.reset();

    auto configureFlatTrackButton = [](const std::shared_ptr<AestraUI::NUIButton>& button) {
        button->setStyle(AestraUI::NUIButton::Style::Text);
        button->setBackgroundColor(AestraUI::NUIColor::transparent());
        button->setHoverColor(AestraUI::NUIColor::transparent());
        button->setPressedColor(AestraUI::NUIColor::transparent());
        button->setBorderEnabled(false);
        button->setGlowEnabled(false);
        button->setTextColor(AestraUI::NUIColor::white().withAlpha(0.48f));
        button->setFontSize(11.0f);
        button->setCornerRadius(0.0f);
    };

    // Create mute button
    m_muteButton = std::make_shared<AestraUI::NUIButton>();
    m_muteButton->setText("M");
    configureFlatTrackButton(m_muteButton);
    m_muteButton->setToggleable(true);
    m_muteButton->setOnToggle([this](bool) { onMuteToggled(); });
    m_muteButton->setTooltip("Mute Track (M)");
    addChild(m_muteButton);

    // Create solo button
    m_soloButton = std::make_shared<AestraUI::NUIButton>();
    m_soloButton->setText("S");
    configureFlatTrackButton(m_soloButton);
    m_soloButton->setToggleable(true);
    m_soloButton->setOnToggle([this](bool) { onSoloToggled(); });
    m_soloButton->setTooltip("Solo Track (S)");
    addChild(m_soloButton);

    // Create record button
    m_recordButton = std::make_shared<AestraUI::NUIButton>();
    m_recordButton->setText("O");
    configureFlatTrackButton(m_recordButton);
    m_recordButton->setToggleable(true);
    m_recordButton->setOnToggle([this](bool) { onRecordToggled(); });
    addChild(m_recordButton);

    updateUI();
}


TrackUIComponent::~TrackUIComponent() {
    detachContextMenu(m_recordModeMenu);
    Log::debug("TrackUIComponent destroyed for lane: " + m_laneId.toString());
}

double TrackUIComponent::getSnapGridSizeBeats() const {
    // Delegate to MusicTheory which handles all snap types including Triplet
    return AestraUI::MusicTheory::getSnapDuration(m_snapSetting);
}

double TrackUIComponent::snapBeatToGrid(double beat) const {
    double gridSize = getSnapGridSizeBeats();
    if (gridSize <= 0.0) return beat; // No snap
    return std::round(beat / gridSize) * gridSize;
}

// =============================================================================
// SECTION: UI Callbacks
// =============================================================================

void TrackUIComponent::onVolumeChanged(float volume) {
    if (m_channel && m_trackManager) {
        m_trackManager->getCommandHistory().pushAndExecute(
            std::make_shared<SetVolumeCommand>(*m_channel, volume));
        Log::info("Lane " + m_laneId.toString() + " volume: " + std::to_string(volume));
    }
}

void TrackUIComponent::onPanChanged(float pan) {
    if (m_channel && m_trackManager) {
        m_trackManager->getCommandHistory().pushAndExecute(
            std::make_shared<SetPanCommand>(*m_channel, pan));
        Log::info("Lane " + m_laneId.toString() + " pan: " + std::to_string(pan));
    }
}


void TrackUIComponent::onMuteToggled() {
    if (m_channel && m_trackManager) {
        bool isMuted = m_muteButton->isToggled();
        m_trackManager->getCommandHistory().pushAndExecute(
            std::make_shared<SetMuteCommand>(*m_channel, isMuted));

        // Mutual Exclusivity: If Muting, turn off Solo
        if (isMuted && m_channel->isSoloed()) {
             Log::info("Mutual Exclusivity: Turning OFF Solo because Mute activated.");
             m_trackManager->getCommandHistory().pushAndExecute(
                 std::make_shared<SetSoloCommand>(*m_channel, false));
             if (m_soloButton) m_soloButton->setToggled(false);
        }

        Log::info("Lane " + m_laneId.toString() + " muted: " + (isMuted ? "ON" : "OFF"));
        updateUI();
        repaint();
        if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback();
    }
}


void TrackUIComponent::onSoloToggled() {
    if (m_channel && m_trackManager) {
        bool newSolo = m_soloButton->isToggled(); // Use button state
        m_trackManager->getCommandHistory().pushAndExecute(
            std::make_shared<SetSoloCommand>(*m_channel, newSolo));

        // Mutual Exclusivity: If Soloing, turn off Mute
        if (newSolo && m_channel->isMuted()) {
            Log::info("Mutual Exclusivity: Turning OFF Mute because Solo activated.");
            m_trackManager->getCommandHistory().pushAndExecute(
                std::make_shared<SetMuteCommand>(*m_channel, false));
            if (m_muteButton) m_muteButton->setToggled(false);
        }

        if (newSolo && m_onSoloToggledCallback) {
            m_onSoloToggledCallback(this);
        }

        updateUI();
        repaint();
        if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback();
        Log::info("Lane " + m_laneId.toString() + " solo: " + (newSolo ? "ON" : "OFF"));
    }
}


void TrackUIComponent::onRecordToggled() {
    if (m_channel) {
        const bool armed = m_recordButton && m_recordButton->isToggled();
        m_channel->setArmed(armed);
        if (m_trackManager) {
            m_trackManager->publishInputMonitoringSnapshot();
        }
        Log::info("Lane " + m_laneId.toString() + " armed: " + (armed ? "ON" : "OFF"));
        updateUI();
        repaint();
        if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback();
    }
}

void TrackUIComponent::showRecordModeMenu(const AestraUI::NUIPoint& position) {
    if (!m_channel) {
        return;
    }

    detachContextMenu(m_recordModeMenu);
    m_recordModeMenu = std::make_shared<AestraUI::NUIContextMenu>();
    m_recordModeMenu->setOnHide([this]() { detachContextMenu(m_recordModeMenu); });
    m_recordModeMenu->addRadioItem("Arm Only", "record_mode", !m_channel->isMonitoringEnabled(), [this]() {
        if (!m_channel) return;
        m_channel->setMonitoringEnabled(false);
        if (m_trackManager) {
            m_trackManager->publishInputMonitoringSnapshot();
        }
        updateUI();
        repaint();
    });
    m_recordModeMenu->addRadioItem("Arm + Monitor", "record_mode", m_channel->isMonitoringEnabled(), [this]() {
        if (!m_channel) return;
        m_channel->setMonitoringEnabled(true);
        if (m_trackManager) {
            m_trackManager->publishInputMonitoringSnapshot();
        }
        updateUI();
        repaint();
    });
    attachAndShowContextMenu(this, m_recordModeMenu, position);
}

void TrackUIComponent::updateRecordTooltip() {
    if (!m_recordButton || !m_channel) {
        return;
    }

    const char* modeText = m_channel->isMonitoringEnabled() ? "Arm + Monitor" : "Arm Only";
    m_recordButton->setTooltip(std::string("Arm for Recording (O) • Right-click: ") + modeText);
}


void TrackUIComponent::updateUI() {
    if (!m_channel) return;

    // Invalidate parent cache since button colors are changing
    if (m_onCacheInvalidationCallback) {
        m_onCacheInvalidationCallback();
    }

    // Update track name colors with bright colors based on number
    updateTrackNameColors();

    auto& themeManager = AestraUI::NUIThemeManager::getInstance();

    const AestraUI::NUIColor inactiveBg = AestraUI::NUIColor::transparent();
    const AestraUI::NUIColor inactiveHover = AestraUI::NUIColor::transparent();
    const AestraUI::NUIColor inactiveText = AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.50f);
    const AestraUI::NUIColor activeText = AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.96f);
    const AestraUI::NUIColor muteActive = AestraUI::NUIColor::fromHex(0xe8a838, 0.92f);
    const AestraUI::NUIColor soloActive = AestraUI::NUIColor::fromHex(0x3dbb6e, 0.92f);
    const AestraUI::NUIColor recordActive = AestraUI::NUIColor::fromHex(0xe85454, 0.92f);

    if (m_muteButton) {
        m_muteButton->setToggled(m_channel->isMuted());
        m_muteButton->setGlowEnabled(false);
        
        if (m_channel->isMuted()) {
            m_muteButton->setBackgroundColor(AestraUI::NUIColor::transparent());
            m_muteButton->setTextColor(muteActive);
            m_muteButton->setHoverColor(AestraUI::NUIColor::transparent());
            m_muteButton->setBorderEnabled(false);
        } else {
            m_muteButton->setBackgroundColor(inactiveBg);
            m_muteButton->setTextColor(inactiveText);
            m_muteButton->setHoverColor(inactiveHover);
            m_muteButton->setBorderEnabled(false);
        }
    }

    if (m_soloButton) {
        m_soloButton->setToggled(m_channel->isSoloed());
        m_soloButton->setGlowEnabled(false);
        
        if (m_channel->isSoloed()) {
            m_soloButton->setBackgroundColor(AestraUI::NUIColor::transparent());
            m_soloButton->setTextColor(soloActive);
            m_soloButton->setHoverColor(AestraUI::NUIColor::transparent());
            m_soloButton->setBorderEnabled(false);
        } else {
            m_soloButton->setBackgroundColor(inactiveBg);
            m_soloButton->setTextColor(inactiveText);
            m_soloButton->setHoverColor(inactiveHover);
            m_soloButton->setBorderEnabled(false);
        }
    }

    if (m_recordButton) {
        m_recordButton->setToggled(m_channel->isArmed());
        m_recordButton->setGlowEnabled(false);
        m_recordButton->setBackgroundColor(inactiveBg);
        m_recordButton->setTextColor(inactiveText);
        m_recordButton->setHoverColor(inactiveHover);
        m_recordButton->setBorderEnabled(false);

        if (m_channel->isArmed()) {
            m_recordButton->setBackgroundColor(AestraUI::NUIColor::transparent());
            m_recordButton->setTextColor(recordActive);
            m_recordButton->setHoverColor(AestraUI::NUIColor::transparent());
        }
        updateRecordTooltip();
    }

    if (m_channel) {
        m_volumeKnobValue = std::clamp(m_channel->getVolume(), 0.0f, 2.0f);
    }

    if (m_volumeFader) {
        m_volumeFader->setTrackColor(themeManager.getColor("borderSubtle").withAlpha(0.36f));
        m_volumeFader->setFillColor(themeManager.getColor("accentPrimary").withAlpha(0.72f));
        m_volumeFader->setThumbColor(themeManager.getColor("textPrimary").withAlpha(0.92f));
        m_volumeFader->setThumbHoverColor(themeManager.getColor("textPrimary"));
        m_volumeFader->setValue(m_channel->getVolume());
    }
}


void TrackUIComponent::updateTrackNameColors() {
    if (!m_nameLabel || !m_channel) return;

    std::string trackName = m_channel->getName();

    // Apply bright colors based on track number for "Track X" format
    size_t spacePos = trackName.find(' ');
    if (spacePos != std::string::npos) {
        // Create bright colors for the track based on number
        static const std::vector<AestraUI::NUIColor> brightColors = {
            AestraUI::NUIColor(1.0f, 0.8f, 0.2f, 1.0f),   // Bright yellow/gold
            AestraUI::NUIColor(0.2f, 1.0f, 0.8f, 1.0f),   // Bright cyan
            AestraUI::NUIColor(1.0f, 0.4f, 0.8f, 1.0f),   // Bright pink/magenta
            AestraUI::NUIColor(0.6f, 1.0f, 0.2f, 1.0f),   // Bright lime
            AestraUI::NUIColor(1.0f, 0.6f, 0.2f, 1.0f),   // Bright orange
            AestraUI::NUIColor(0.4f, 0.8f, 1.0f, 1.0f),   // Bright blue
            AestraUI::NUIColor(1.0f, 0.2f, 0.4f, 1.0f),   // Bright red
            AestraUI::NUIColor(0.8f, 0.4f, 1.0f, 1.0f),   // Bright purple
            AestraUI::NUIColor(1.0f, 0.9f, 0.1f, 1.0f),   // Bright yellow
            AestraUI::NUIColor(0.1f, 0.9f, 0.6f, 1.0f)    // Bright teal
        };

        // Extract track number from name for consistent coloring
        // For "Track X" format, use X-1 for 0-based indexing
        size_t numberPos = trackName.find_last_not_of("0123456789");
        if (numberPos != std::string::npos && numberPos < trackName.length() - 1) {
            std::string numberStr = trackName.substr(numberPos + 1);
            try {
                uint32_t trackNumber = std::stoul(numberStr);
                // [SEC-FIX] Guard against underflow when track number is 0.
                size_t colorIndex = (trackNumber == 0) ? 0 : ((trackNumber - 1) % brightColors.size());
                AestraUI::NUIColor autoColor = restrainDawColor(brightColors[colorIndex], 0.72f, 0.30f, 0.78f);
                
                m_nameLabel->setTextColor(autoColor);
                
                return; // Successfully set color, exit
            } catch (const std::exception&) {
                // Fall through
            }
        }

        // Get track index from ID for consistent coloring (fallback)
        uint32_t trackId = m_channel->getChannelId();
        size_t colorIndex = (trackId - 1) % brightColors.size();
        m_nameLabel->setTextColor(restrainDawColor(brightColors[colorIndex], 0.72f, 0.30f, 0.78f));
    } else {
        // Fallback for non-standard track names
        uint32_t color = m_channel->getColor();
        float r = ((color >> 16) & 0xFF) / 255.0f;
        float g = ((color >> 8) & 0xFF) / 255.0f;
        float b = (color & 0xFF) / 255.0f;
        float a = ((color >> 24) & 0xFF) / 255.0f;
        m_nameLabel->setTextColor(restrainDawColor(AestraUI::NUIColor(r, g, b, a), 0.72f, 0.30f, 0.78f));
    }
}

void TrackUIComponent::generateWaveformCache(int, int) {
    // Waveform caching for entire track is deprecated in v3.0 (clips have their own caching)
}

// =============================================================================
// SECTION: Waveform & Clip Drawing
// =============================================================================

// Draw waveform for a specific clip using zoom-aware LOD peaks.
void TrackUIComponent::drawWaveformForClip(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& bounds,
                                          const ClipInstance& clip, float offsetRatio, float visibleRatio) {
    if (!m_trackManager) return;

    auto& patternMgr = m_trackManager->getPatternManager();
    auto& sourceMgr = m_trackManager->getSourceManager();

    auto pattern = patternMgr.getPattern(clip.patternId);
    if (!pattern || !pattern->isAudio()) return;

    auto audioPayload = std::get_if<AudioSlicePayload>(&pattern->payload);
    if (!audioPayload) return;

    auto source = sourceMgr.getSource(audioPayload->audioSourceId);
    if (!source || !source->isReady()) {
        return;
    }

    auto bufferPtr = source->getBuffer();
    if (!bufferPtr || bufferPtr->numFrames == 0) {
        return;
    }

    const auto& audioData = *bufferPtr;

    float width = bounds.width;
    float height = bounds.height;
    if (width <= 0.0f || height <= 0.0f) return;

    size_t numChannels = audioData.numChannels;
    size_t totalFrames = audioData.numFrames;

    // sourceStart conversion (project rate -> source rate)
    SampleIndex sourceOffset = clip.edits.sourceStart;
    double sampleRate = source->getSampleRate();
    double bpm = m_trackManager ? m_trackManager->getPlaylistModel().getBPM() : 120.0;
    double secondsPerBeat = 60.0 / bpm;
    double clipDurationSeconds = clip.durationBeats * secondsPerBeat;
    size_t clipFrames = static_cast<size_t>(clipDurationSeconds * sampleRate);
    if (clipFrames > totalFrames) {
        clipFrames = totalFrames;
    }

    double projectSampleRate = m_trackManager ? m_trackManager->getPlaylistModel().getProjectSampleRate() : 48000.0;
    size_t scaledSourceOffset = static_cast<size_t>(std::round(static_cast<double>(sourceOffset) * (sampleRate / projectSampleRate)));
    if (scaledSourceOffset >= totalFrames) {
        scaledSourceOffset = 0;
    }

    size_t startFrame = scaledSourceOffset + static_cast<size_t>(offsetRatio * clipFrames);
    size_t endFrame = scaledSourceOffset + static_cast<size_t>((offsetRatio + visibleRatio) * clipFrames);
    startFrame = std::min(startFrame, totalFrames);
    endFrame = std::min(endFrame, totalFrames);
    if (startFrame >= endFrame) return;

    size_t visibleFrames = endFrame - startFrame;
    if (visibleFrames == 0) return;

    // Use precomputed waveform peaks only. Do not calculate peaks during render.
    auto waveformCache = source->getWaveformCache();
    if (!waveformCache || !waveformCache->isReady()) {
        // Fallback: faint center line
        float centerY = bounds.y + height * 0.5f;
        renderer.drawLine(
            AestraUI::NUIPoint(bounds.x, centerY),
            AestraUI::NUIPoint(bounds.x + width, centerY),
            1.0f,
            AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.04f));
        return;
    }

    // Determine bar count: roughly one bar per 2 pixels, matching existing density
    const int numBars = std::max(1, static_cast<int>(width / 2));

    // Reusable member buffers avoid per-frame allocations
    m_waveformPeaksL.clear();
    m_waveformPeaksR.clear();

    waveformCache->getPeaksForRange(0, startFrame, endFrame, numBars, m_waveformPeaksL);
    if (numChannels > 1) {
        waveformCache->getPeaksForRange(1, startFrame, endFrame, numBars, m_waveformPeaksR);
    }

    // Stereo split-lane if height allows
    constexpr float kMinSplitHeight = 32.0f;
    bool useStereoSplit = (numChannels >= 2) && (height >= kMinSplitHeight);

    if (useStereoSplit) {
        float halfH = height * 0.5f;
        // Left channel in top half
        drawChannelWaveform(renderer, bounds.x, bounds.y, width, halfH, m_waveformPeaksL);
        // Right channel in bottom half
        drawChannelWaveform(renderer, bounds.x, bounds.y + halfH, width, halfH, m_waveformPeaksR);
    } else {
        // Combined: aggregate channels per bar
        drawCombinedWaveform(renderer, bounds, m_waveformPeaksL, m_waveformPeaksR, numChannels);
    }
}

void TrackUIComponent::drawChannelWaveform(AestraUI::NUIRenderer& renderer, float x, float y, float w, float h,
                                            const std::vector<Aestra::Audio::WaveformPeak>& peaks) {
    if (peaks.empty() || w <= 0.0f || h <= 0.0f) return;

    const float centerY = y + h * 0.5f;
    const float halfDrawH = std::max(1.0f, h * 0.5f - 2.0f);
    const int numBars = static_cast<int>(peaks.size());
    const float barStep = w / static_cast<float>(numBars);

    const AestraUI::NUIColor topColor(1.0f, 1.0f, 1.0f, 0.24f);
    const AestraUI::NUIColor bottomColor(1.0f, 1.0f, 1.0f, 0.10f);
    const AestraUI::NUIColor peakColor(1.0f, 1.0f, 1.0f, 0.42f);
    const AestraUI::NUIColor centerLineColor(1.0f, 1.0f, 1.0f, 0.08f);

    for (int i = 0; i < numBars; ++i) {
        const auto& peak = peaks[i];
        float normMin = std::max(-1.0f, std::min(1.0f, peak.min));
        float normMax = std::max(-1.0f, std::min(1.0f, peak.max));

        float topY = centerY - normMax * halfDrawH;
        float bottomY = centerY - normMin * halfDrawH;

        // Minimum nonzero visual height for quiet audio; true silence stays at center
        float barHeight = bottomY - topY;
        if (barHeight > 0.0f && barHeight < 1.5f) {
            float mid = (topY + bottomY) * 0.5f;
            topY = mid - 0.75f;
            bottomY = mid + 0.75f;
        }

        float barX = x + i * barStep + barStep * 0.5f;

        renderer.drawLine(AestraUI::NUIPoint(barX, centerY), AestraUI::NUIPoint(barX, topY), 1.0f, topColor);
        renderer.drawLine(AestraUI::NUIPoint(barX, centerY), AestraUI::NUIPoint(barX, bottomY), 1.0f, bottomColor);
        renderer.drawLine(AestraUI::NUIPoint(barX, topY), AestraUI::NUIPoint(barX, bottomY), 1.0f, peakColor);
    }

    renderer.drawLine(AestraUI::NUIPoint(x, centerY), AestraUI::NUIPoint(x + w, centerY), 1.0f, centerLineColor);
}

void TrackUIComponent::drawCombinedWaveform(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& bounds,
                                             const std::vector<Aestra::Audio::WaveformPeak>& peaksL,
                                             const std::vector<Aestra::Audio::WaveformPeak>& peaksR,
                                             size_t numChannels) {
    if (peaksL.empty() || bounds.width <= 0.0f || bounds.height <= 0.0f) return;

    const float centerY = bounds.y + bounds.height * 0.5f;
    const float halfDrawH = std::max(1.0f, bounds.height * 0.5f - 3.0f);
    const int numBars = static_cast<int>(peaksL.size());
    const float barStep = bounds.width / static_cast<float>(numBars);

    const AestraUI::NUIColor topColor(1.0f, 1.0f, 1.0f, 0.24f);
    const AestraUI::NUIColor bottomColor(1.0f, 1.0f, 1.0f, 0.10f);
    const AestraUI::NUIColor peakColor(1.0f, 1.0f, 1.0f, 0.42f);
    const AestraUI::NUIColor centerLineColor(1.0f, 1.0f, 1.0f, 0.08f);

    for (int i = 0; i < numBars; ++i) {
        float minVal = peaksL[i].min;
        float maxVal = peaksL[i].max;
        if (numChannels > 1 && i < static_cast<int>(peaksR.size())) {
            minVal = std::min(minVal, peaksR[i].min);
            maxVal = std::max(maxVal, peaksR[i].max);
        }

        float normMin = std::max(-1.0f, std::min(1.0f, minVal));
        float normMax = std::max(-1.0f, std::min(1.0f, maxVal));

        float topY = centerY - normMax * halfDrawH;
        float bottomY = centerY - normMin * halfDrawH;

        float barHeight = bottomY - topY;
        if (barHeight > 0.0f && barHeight < 1.5f) {
            float mid = (topY + bottomY) * 0.5f;
            topY = mid - 0.75f;
            bottomY = mid + 0.75f;
        }

        float barX = bounds.x + i * barStep + barStep * 0.5f;

        renderer.drawLine(AestraUI::NUIPoint(barX, centerY), AestraUI::NUIPoint(barX, topY), 1.0f, topColor);
        renderer.drawLine(AestraUI::NUIPoint(barX, centerY), AestraUI::NUIPoint(barX, bottomY), 1.0f, bottomColor);
        renderer.drawLine(AestraUI::NUIPoint(barX, topY), AestraUI::NUIPoint(barX, bottomY), 1.0f, peakColor);
    }

    renderer.drawLine(AestraUI::NUIPoint(bounds.x, centerY), AestraUI::NUIPoint(bounds.x + bounds.width, centerY),
                      1.0f, centerLineColor);
}

void TrackUIComponent::drawSampleClipForClip(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& clipBounds,
                                            const AestraUI::NUIRect& fullClipBounds, const ClipInstance& clip) {
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    
    const float clipRadius = 4.0f;

    // Get clip color from pattern via PatternManager
    AestraUI::NUIColor clipColor = themeManager.getColor("primary");
    std::string sampleName = "Clip";
    int patternRefCount = 1;  // How many clips share this pattern
    int patternInstanceIndex = 1;  // This clip's instance number

    if (m_trackManager) {
        if (auto pattern = m_trackManager->getPatternManager().getPattern(clip.patternId)) {
            // Override clip color with Track Bright Color to match Strip & Name
            if (m_channel) {
                std::string trackName = m_channel->getName();
                size_t spacePos = trackName.find(' ');
                bool foundBrightColor = false;
                
                // Bright Color Palette
                static const std::vector<AestraUI::NUIColor> brightColors = {
                    AestraUI::NUIColor(1.0f, 0.8f, 0.2f, 1.0f),   
                    AestraUI::NUIColor(0.2f, 1.0f, 0.8f, 1.0f),   
                    AestraUI::NUIColor(1.0f, 0.4f, 0.8f, 1.0f),   
                    AestraUI::NUIColor(0.6f, 1.0f, 0.2f, 1.0f),   
                    AestraUI::NUIColor(1.0f, 0.6f, 0.2f, 1.0f),   
                    AestraUI::NUIColor(0.4f, 0.8f, 1.0f, 1.0f),   
                    AestraUI::NUIColor(1.0f, 0.2f, 0.4f, 1.0f),   
                    AestraUI::NUIColor(0.8f, 0.4f, 1.0f, 1.0f),   
                    AestraUI::NUIColor(1.0f, 0.9f, 0.1f, 1.0f),   
                    AestraUI::NUIColor(0.1f, 0.9f, 0.6f, 1.0f)    
                };

                if (spacePos != std::string::npos) {
                    uint32_t trackNumber = 0;
                    if (parseTrailingTrackNumber(trackName, trackNumber)) {
                        size_t colorIndex = (trackNumber - 1) % brightColors.size();
                        clipColor = brightColors[colorIndex];
                        foundBrightColor = true;
                    }
                    
                    if (!foundBrightColor) {
                       uint32_t trackId = m_channel->getChannelId();
                       size_t colorIndex = (trackId - 1) % brightColors.size();
                       clipColor = brightColors[colorIndex];
                       foundBrightColor = true;
                    }
                }
                
                if (!foundBrightColor) {
                    // Fallback to channel color if not using bright palette
                    uint32_t c = m_channel->getColor();
                    clipColor = AestraUI::NUIColor((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, (c >> 24) & 0xFF) / 255.0f;
                }
            } else {
                uint32_t color = clip.colorRGBA;
                clipColor = AestraUI::NUIColor(
                    (color >> 16) & 0xFF,
                    (color >> 8) & 0xFF,
                    color & 0xFF,
                    (color >> 24) & 0xFF
                ) / 255.0f;
            }
            sampleName = pattern->name;
            
            // Count how many clips reference this pattern across all lanes
            auto& playlist = m_trackManager->getPlaylistModel();
            int count = 0;
            int indexForThisClip = 0;
            for (size_t l = 0; l < playlist.getLaneCount(); ++l) {
                if (auto lane = playlist.getLane(playlist.getLaneId(l))) {
                    for (const auto& c : lane->clips) {
                        if (c.patternId == clip.patternId) {
                            ++count;
                            if (c.id == clip.id) {
                                indexForThisClip = count;
                            }
                        }
                    }
                }
            }
            patternRefCount = count;
            patternInstanceIndex = indexForThisClip;
        }
    }
    
    bool clipSelected = (clip.id == m_activeClipId);
    const AestraUI::NUIColor clipBase = restrainDawColor(clipColor,
                                                         clipSelected ? 1.02f : 0.90f,
                                                         clipSelected ? 0.80f : 0.66f,
                                                         1.0f);
    AestraUI::NUIColor tintFill = clipBase.withAlpha(clipSelected ? 0.78f : 0.60f);
    renderer.fillRoundedRect(clipBounds, clipRadius, themeManager.getColor("backgroundPrimary").withAlpha(0.16f));
    renderer.fillRoundedRect(clipBounds, clipRadius, tintFill);

    AestraUI::NUIColor borderColor = clipBase.lightened(0.10f).withAlpha(clipSelected ? 0.94f : 0.58f);
    float borderWidth = 1.0f;
    
    if (clipSelected) {
        borderColor = clipBase.lightened(0.22f).withAlpha(0.92f);
        borderWidth = 1.35f;
    }
    
    // Ghost instance check
    bool isGhostInstance = (patternRefCount > 1 && patternInstanceIndex > 1);

    if (isGhostInstance) {
        // Ghost instances: Dimmer border
        borderColor = borderColor.withAlpha(0.4f);
        // Dashed border? For now just dim.
    }
    
    renderer.strokeRoundedRect(clipBounds, clipRadius, borderWidth, borderColor);
    // Subtle top inner highlight for depth
    renderer.fillRect({clipBounds.x + 3.0f, clipBounds.y + 1.0f, std::max(0.0f, clipBounds.width - 6.0f), 1.0f},
                      AestraUI::NUIColor::white().withAlpha(0.05f));
    if (clipSelected) {
        renderer.strokeRoundedRect({clipBounds.x - 1.0f, clipBounds.y - 1.0f, clipBounds.width + 2.0f, clipBounds.height + 2.0f},
                                   clipRadius + 1.0f,
                                   1.0f,
                                   themeManager.getColor("accentPrimary").withAlpha(0.52f));
    }

    // 14px clip header bar with filename in clip color
    constexpr float kClipHeaderHeight = 14.0f;
    if (clipBounds.height > kClipHeaderHeight + 4.0f && clipBounds.width > 28.0f) {
        const AestraUI::NUIRect headerRect(
            clipBounds.x + 1.0f,
            clipBounds.y + 1.0f,
            std::max(0.0f, clipBounds.width - 2.0f),
            kClipHeaderHeight
        );
        renderer.fillRoundedRect(headerRect, clipRadius - 1.0f, clipBase.withAlpha(clipSelected ? 0.55f : 0.40f));
        renderer.drawLine(
            AestraUI::NUIPoint(clipBounds.x + 4.0f, clipBounds.y + kClipHeaderHeight + 1.0f),
            AestraUI::NUIPoint(clipBounds.right() - 4.0f, clipBounds.y + kClipHeaderHeight + 1.0f),
            1.0f,
            AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.06f)
        );

        const std::string displayName = truncateClipLabel(sampleName, clipBounds.width - 16.0f, 6.0f);
        if (!displayName.empty()) {
            renderer.drawText(displayName,
                              AestraUI::NUIPoint(clipBounds.x + 6.0f, clipBounds.y + 3.0f),
                              9.0f,
                              AestraUI::NUIColor(1.0f, 1.0f, 1.0f, clipSelected ? 0.92f : 0.78f));
        }
    }
}

void TrackUIComponent::drawSampleClip(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& clipBounds) {
    // Legacy stub - do nothing or implement if needed for backward compatibility
}


// Helper to draw a clip at its calculated position (for multi-clip lane support)
void TrackUIComponent::drawClipAtPosition(AestraUI::NUIRenderer& renderer, const ClipInstance& clip,
                                          const AestraUI::NUIRect& bounds, float controlAreaWidth) {
    
    // Calculate waveform position in timeline space
    double startBeat = clip.startBeat;
    double durationBeats = clip.durationBeats;
    
    // Calculate waveform dimensions in pixel space
    double relStartX = (startBeat * m_pixelsPerBeat) - static_cast<double>(m_timelineScrollOffset);
    float waveformStartX = bounds.x + controlAreaWidth + 5 + static_cast<float>(relStartX);
    float waveformWidthInPixels = static_cast<float>(durationBeats * m_pixelsPerBeat);
    
    // Only draw if waveform is visible in the current viewport
    float gridStartX = bounds.x + controlAreaWidth + 5;
    float gridWidth = bounds.width - controlAreaWidth - 10;
    float gridEndX = gridStartX + gridWidth;
    
    // Culling padding for smooth scrolling
    float cullPaddingLeft = 400.0f;
    float cullPaddingRight = 400.0f;
    
    // Check if waveform intersects with visible area
    if (waveformStartX + waveformWidthInPixels > gridStartX - cullPaddingLeft &&
        waveformStartX < gridEndX + cullPaddingRight) {
        
        // Determine the visible portion
        float visibleStartX = std::max(waveformStartX, gridStartX);
        float visibleEndX = std::min(waveformStartX + waveformWidthInPixels, gridEndX);
        float visibleWidth = visibleEndX - visibleStartX;
        
        if (visibleWidth > 0) {
            // Calculate offset and ratio for visible portion
            float offsetRatio = 0.0f;
            float visibleRatio = 1.0f;
            
            if (waveformStartX < gridStartX) {
                offsetRatio = (gridStartX - waveformStartX) / waveformWidthInPixels;
            }
            
            if (waveformStartX + waveformWidthInPixels > gridEndX) {
                float endRatio = (gridEndX - waveformStartX) / waveformWidthInPixels;
                visibleRatio = endRatio - offsetRatio;
            }
            
            // Clip bounds for drawing
            float clipStartX = std::max(waveformStartX, gridStartX);
            float clipEndX = std::min(waveformStartX + waveformWidthInPixels, gridEndX);
            float clipWidth = clipEndX - clipStartX;
            
            if (clipWidth > 0) {
                const AestraUI::NUIRect fullClipBounds(
                    waveformStartX,
                    bounds.y + 2,
                    waveformWidthInPixels,
                    bounds.height - 4
                );

                // Store FULL clip bounds for hit testing
                m_allClipBounds[clip.id] = fullClipBounds;

                AestraUI::NUIRect clippedClipBounds(
                    clipStartX,
                    bounds.y + 2,
                    clipWidth,
                    bounds.height - 4
                );
                const AestraUI::NUIRect insetFullClipBounds = fullClipBounds;
                const AestraUI::NUIRect insetClippedClipBounds = clippedClipBounds;

                // Check if this is a pattern clip or sample clip
                bool isPattern = false;
                if (m_trackManager && clip.patternId.isValid()) {
                    auto pattern = m_trackManager->getPatternManager().getPattern(clip.patternId);
                    if (pattern && pattern->isMidi()) {
                        isPattern = true;
                    }
                }

                if (isPattern) {
                    drawPatternClipForClip(renderer, insetClippedClipBounds, insetFullClipBounds, clip);
                } else {
                    drawSampleClipForClip(renderer, insetClippedClipBounds, insetFullClipBounds, clip);
                    constexpr float kClipLabelBarHeight = 14.0f;
                    const float waveformPadX = 3.0f;
                    const float waveformPadBottom = 3.0f;
                    const float waveformTopY = insetClippedClipBounds.y + std::min(kClipLabelBarHeight, std::max(0.0f, insetClippedClipBounds.height));
                    const AestraUI::NUIRect waveformInsideClip(
                        insetClippedClipBounds.x + waveformPadX,
                        waveformTopY,
                        std::max(1.0f, insetClippedClipBounds.width - waveformPadX * 2.0f),
                        std::max(1.0f, (insetClippedClipBounds.bottom() - waveformPadBottom) - waveformTopY)
                    );
                    drawWaveformForClip(renderer, waveformInsideClip, clip, offsetRatio, visibleRatio);
                }
            }
        }
    }
}

void TrackUIComponent::drawPatternClipForClip(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& clipBounds,
                                              const AestraUI::NUIRect& fullClipBounds, const ClipInstance& clip) {
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    
    AestraUI::NUIColor baseColor = AestraUI::NUIColor::fromHex(clip.colorRGBA);
    bool isSelected = (clip.id == m_activeClipId);

    if (clip.edits.muted) {
        baseColor = baseColor.withAlpha(0.4f);
    }

    const float clipRadius = 4.0f;
    renderer.fillRoundedRect(clipBounds, clipRadius, AestraUI::NUIColor(0.07f, 0.072f, 0.09f, 0.96f));
    renderer.fillRoundedRect(clipBounds, clipRadius, baseColor.withAlpha(isSelected ? 0.36f : 0.26f));
    renderer.strokeRoundedRect(
        clipBounds,
        clipRadius,
        1.0f,
        isSelected ? baseColor.lightened(0.28f).withAlpha(0.92f) : baseColor.lightened(0.18f).withAlpha(0.66f)
    );

    // Subtle top inner highlight for depth
    renderer.fillRect({clipBounds.x + 3.0f, clipBounds.y + 1.0f, std::max(0.0f, clipBounds.width - 6.0f), 1.0f},
                      AestraUI::NUIColor::white().withAlpha(0.05f));

    const float headerHeight = std::min(18.0f, std::max(14.0f, clipBounds.height * 0.26f));
    const AestraUI::NUIRect headerRect(clipBounds.x + 1.0f, clipBounds.y + 1.0f, std::max(0.0f, clipBounds.width - 2.0f), headerHeight);
    renderer.fillRoundedRect(headerRect, clipRadius - 1.0f, baseColor.withAlpha(isSelected ? 0.42f : 0.34f));
    renderer.fillRoundedRect(
        {clipBounds.x + 1.5f, clipBounds.y + 1.5f, 4.0f, std::max(0.0f, clipBounds.height - 3.0f)},
        2.0f,
        baseColor.lightened(0.20f).withAlpha(0.95f)
    );
    renderer.drawLine(
        AestraUI::NUIPoint(clipBounds.x + 6.0f, clipBounds.y + headerHeight + 1.0f),
        AestraUI::NUIPoint(clipBounds.right() - 6.0f, clipBounds.y + headerHeight + 1.0f),
        1.0f,
        AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.08f)
    );

    std::string clipName = clip.name;
    if (clipName.empty()) {
        if (m_trackManager) {
            auto pattern = m_trackManager->getPatternManager().getPattern(clip.patternId);
            if (pattern) clipName = pattern->name;
        }
    }

    const std::string displayName = truncateClipLabel(clipName, clipBounds.width - 46.0f, 5.9f);
    if (!displayName.empty()) {
        renderer.drawText(displayName, AestraUI::NUIPoint(clipBounds.x + 10.0f, clipBounds.y + 4.0f),
                          9.5f, AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 1.0f));
    }

    if (m_trackManager && clip.patternId.isValid()) {
        auto pattern = m_trackManager->getPatternManager().getPattern(clip.patternId);
        if (pattern && pattern->isMidi()) {
            const auto& midiPayload = std::get<MidiPayload>(pattern->payload);
            
            float noteAreaY = fullClipBounds.y + headerHeight + 2.0f;
            float noteAreaHeight = std::max(8.0f, fullClipBounds.height - headerHeight - 4.0f);

            int minPitch = 127;
            int maxPitch = 0;
            if (midiPayload.notes.empty()) {
                minPitch = 36; maxPitch = 84;
            } else {
                for (const auto& n : midiPayload.notes) {
                    minPitch = std::min(minPitch, (int)n.pitch);
                    maxPitch = std::max(maxPitch, (int)n.pitch);
                }
                // Add some padding
                minPitch = std::max(0, minPitch - 2);
                maxPitch = std::min(127, maxPitch + 2);
            }
            int pitchRange = std::max(12, maxPitch - minPitch);

            const float laneHeight = std::max(1.0f, noteAreaHeight / static_cast<float>(pitchRange));
            const int guideCount = std::min(8, pitchRange);
            for (int i = 1; i < guideCount; ++i) {
                const float y = noteAreaY + (noteAreaHeight / guideCount) * i;
                renderer.drawLine(
                    AestraUI::NUIPoint(clipBounds.x + 7.0f, y),
                    AestraUI::NUIPoint(clipBounds.right() - 4.0f, y),
                    1.0f,
                    AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.06f)
                );
            }

            const int stepCount = std::max(4, static_cast<int>(std::round(clip.durationBeats * 2.0)));
            const float bodyX = clipBounds.x + 6.0f;
            const float bodyWidth = std::max(0.0f, clipBounds.width - 12.0f);
            for (int i = 1; i < stepCount; ++i) {
                const float stepX = bodyX + (bodyWidth / stepCount) * i;
                const bool major = (i % 4) == 0;
                renderer.drawLine(
                    AestraUI::NUIPoint(stepX, noteAreaY + 1.0f),
                    AestraUI::NUIPoint(stepX, clipBounds.bottom() - 4.0f),
                    1.0f,
                    AestraUI::NUIColor(1.0f, 1.0f, 1.0f, major ? 0.08f : 0.04f)
                );
            }
            
            for (const auto& n : midiPayload.notes) {
                float noteStartX = fullClipBounds.x + (n.startBeat / pattern->lengthBeats) * fullClipBounds.width;
                float noteWidth = (n.durationBeats / pattern->lengthBeats) * fullClipBounds.width;
                
                float normalizedPitch = (float)(n.pitch - minPitch) / pitchRange;
                float noteY = noteAreaY + noteAreaHeight * (1.0f - normalizedPitch) - laneHeight;
                float noteHeight = std::max(2.0f, laneHeight - 1.5f);
                
                AestraUI::NUIRect noteRect(noteStartX, noteY, std::max(1.0f, noteWidth), std::max(1.0f, noteHeight));
                
                if (noteRect.x + noteRect.width > clipBounds.x && noteRect.x < clipBounds.x + clipBounds.width) {
                    if (noteRect.x < clipBounds.x) {
                        noteRect.width -= (clipBounds.x - noteRect.x);
                        noteRect.x = clipBounds.x;
                    }
                    if (noteRect.x + noteRect.width > clipBounds.x + clipBounds.width) {
                        noteRect.width = (clipBounds.x + clipBounds.width) - noteRect.x;
                    }
                    renderer.fillRoundedRect(noteRect, 2.0f, AestraUI::NUIColor(1.0f, 0.985f, 0.93f, isSelected ? 0.88f : 0.78f));
                    if (noteRect.width > 6.0f && noteRect.height > 2.5f) {
                        renderer.fillRoundedRect(
                            {noteRect.x + 1.0f, noteRect.y + 1.0f, std::max(0.0f, noteRect.width - 2.0f), std::max(0.0f, noteRect.height - 2.0f)},
                            1.5f,
                            baseColor.lightened(0.26f).withAlpha(0.42f)
                        );
                    }
                }
            }
        }
    }
}


// =============================================================================
// SECTION: Main Render Entry
// =============================================================================

void TrackUIComponent::renderStatic(AestraUI::NUIRenderer& renderer) {
    AestraUI::NUIRect bounds = getBounds();
    
    // Clear clip bounds map - will be repopulated during drawClipAtPosition
    // m_allClipBounds.clear(); // Handled in render logic now
    if (!m_trackManager) return;
    auto& playlist = m_trackManager->getPlaylistModel();
    auto lane = playlist.getLane(m_laneId);
    // Get theme colors and layout
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    
    // Zebra striping moved to TrackManagerUI for guaranteed rendering order
    
    AestraUI::NUIColor trackBgColor =
        (m_rowIndex % 2 == 0) ? AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.008f)
                              : AestraUI::NUIColor(0.0f, 0.0f, 0.0f, 0.030f);

    // Selection Highlight (Static base)
    if (isSelected()) {
         AestraUI::NUIColor selectedColor = themeManager.getColor("accentPrimary").withAlpha(0.075f);
         trackBgColor = selectedColor; 
    } else if (isHovered()) {
         trackBgColor = AestraUI::NUIColor::white().withAlpha(0.020f);
    }
    
    // Apply background
    renderer.fillRect(bounds, trackBgColor); 
    AestraUI::NUIColor borderColor = themeManager.getColor("border");
    
    float controlAreaWidth = std::min(layout.trackControlsWidth, bounds.width);
    
    if (m_isPrimaryForLane) {
        AestraUI::NUIRect controlBounds(bounds.x, bounds.y, controlAreaWidth, bounds.height);
        
        // Control Area base: elevated surface from palette.
        AestraUI::NUIColor baseControlColor = themeManager.getColor("surfaceTertiary");
        
        // Static Control Area State
        if (m_channel) {
            if (m_selected) {
                 // Selected: clear, quiet lane ownership.
                 baseControlColor = themeManager.getColor("accentPrimary").withAlpha(0.13f);
             } else if (m_channel->isSoloed()) {
                 baseControlColor = themeManager.getColor("accentCyan").withAlpha(0.10f);
             } else if (m_channel->isMuted()) {
                 baseControlColor = themeManager.getColor("backgroundSecondary");
             } else if (isHovered()) {
                 baseControlColor = themeManager.getColor("surfaceRaised").withAlpha(0.70f);
             }
        }
        
        // Render Control Area Background
        renderer.fillRect(controlBounds, baseControlColor);
        
        // Separator Line (Bright Glass Border) between Controls and Timeline
        renderer.drawLine(
            AestraUI::NUIPoint(controlBounds.right(), controlBounds.y),
            AestraUI::NUIPoint(controlBounds.right(), controlBounds.bottom()),
            1.0f,
            themeManager.getColor("border").withAlpha(0.48f)
        );
        
        // Lane color strip (identity)
        if (lane) {
            uint32_t argb = lane->colorRGBA;
            float a = ((argb >> 24) & 0xFF) / 255.0f;
            float r = ((argb >> 16) & 0xFF) / 255.0f;
            float g = ((argb >> 8) & 0xFF) / 255.0f;
            float b = (argb & 0xFF) / 255.0f;
            AestraUI::NUIColor stripColor(r, g, b, a > 0.0f ? a : 1.0f);
            
            const float stripWidth = 3.0f;
            const float stripAlpha = (m_selected || (m_channel && (m_channel->isArmed() || m_channel->isSoloed()))) ? 0.86f : 0.52f;
            const auto stripBright = restrainDawColor(stripColor, 0.84f, 0.62f, stripAlpha);
            renderer.fillRect(AestraUI::NUIRect(bounds.x, bounds.y, stripWidth, bounds.height), stripBright);
        }

        // Keep separator clean and flat; no extra chrome shadow strip.

        drawPlaylistGrid(renderer, bounds);
    }

    // Render Clips (Heavy part)
    m_allClipBounds.clear();
    // Use stored modId for caching check if needed later, but here we just draw
    float clipOpacity = (m_playlistMode == PlaylistMode::Automation) ? 0.3f : 1.0f;
    renderer.setOpacity(clipOpacity);
    for (const auto& clip : lane->clips) {
        drawClipAtPosition(renderer, clip, bounds, controlAreaWidth);
    }
    renderer.setOpacity(1.0f);
}


// Render Dynamic Overlays (Grid Area Only: Mute/Solo dimming, Automation, Selection)
void TrackUIComponent::renderDynamic(AestraUI::NUIRenderer& renderer) {
    AestraUI::NUIRect bounds = getBounds();
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    float controlAreaWidth = std::min(layout.trackControlsWidth, bounds.width);

    // Automation Layer (v3.1)
    if (m_playlistMode == PlaylistMode::Automation) {
        renderAutomationLayer(renderer, bounds, bounds.x + controlAreaWidth);
    }

    // Live Recording Waveform (v3.0.2) - Dynamic real-time update
    drawLiveWaveform(renderer, bounds, controlAreaWidth);

    // Apply overlay for muted/solo state (Grid Area Only)
    if (m_isPrimaryForLane) {
        bool anySoloed = false;
        if (m_trackManager) {
            // Optimization: TrackManager should pass this state down? 
            // For now, iterating 50 tracks is cheap.
            for (size_t i = 0; i < m_trackManager->getChannelCount(); ++i) {
                if (m_trackManager->getChannel(i)->isSoloed()) {
                    anySoloed = true;
                    break;
                }
            }
        }

        const bool soloSuppressed = anySoloed && m_channel && !m_channel->isSoloed();

        AestraUI::NUIRect gridArea(
            bounds.x + controlAreaWidth,
            bounds.y,
            bounds.width - controlAreaWidth,
            bounds.height
        );
        
        if (m_channel && m_channel->isSoloed()) {
            renderer.fillRect(gridArea, themeManager.getColor("accentCyan").withAlpha(0.06f));
        }

        float dimAlpha = 0.0f;
        if (soloSuppressed) dimAlpha = std::max(dimAlpha, 0.28f);
        if (m_channel && m_channel->isMuted()) dimAlpha = std::max(dimAlpha, 0.40f);

        if (dimAlpha > 0.0f) {
            renderer.fillRect(gridArea, AestraUI::NUIColor(0.0f, 0.0f, 0.0f, dimAlpha));
        }
    }
}

void TrackUIComponent::onRender(AestraUI::NUIRenderer& renderer) {
    AESTRA_ZONE("TrackUI_Render");
    renderStatic(renderer);
    renderDynamic(renderer);
}


void TrackUIComponent::renderControlOverlay(AestraUI::NUIRenderer& renderer) {
    if (!m_isPrimaryForLane) return;
    
    const AestraUI::NUIRect bounds = getBounds();
    if (bounds.isEmpty()) return;

    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    const float controlAreaWidth = std::min(layout.trackControlsWidth, bounds.width);
    const AestraUI::NUIRect controlAreaBounds(bounds.x, bounds.y, controlAreaWidth, bounds.height);

    // Layered control slab with cool depth.
    renderer.fillRect(controlAreaBounds, themeManager.getColor("surfaceRaised").withAlpha(isHovered() ? 0.30f : 0.18f));

    AestraUI::NUIRect highlightRect = controlAreaBounds;
    highlightRect.height = 1.0f;
    renderer.fillRect(highlightRect, themeManager.getColor("primary").withAlpha(0.20f));
    
    // Right Border (Separator)
    AestraUI::NUIRect borderRect(controlAreaBounds.right() - 1.0f, controlAreaBounds.y, 1.0f, controlAreaBounds.height);
    renderer.fillRect(borderRect, themeManager.getColor("borderSubtle").withAlpha(0.92f));

    // Inline Volume Meter (Behind Name) - Uses real audio levels from MeterSnapshotBuffer
    if (m_channel && !m_channel->isMuted() && m_trackManager) {
        // Get meter data from MeterSnapshotBuffer via TrackManager
        auto meterSnapshots = m_trackManager->getMeterSnapshots();
        auto slotMapPtr = m_trackManager->getChannelSlotMapShared();
        
        if (meterSnapshots && slotMapPtr) {
            uint32_t slotIndex = slotMapPtr->getSlotIndex(m_channel->getChannelId());
            if (slotIndex != ChannelSlotMap::INVALID_SLOT) {
                auto readout = meterSnapshots->readSnapshot(slotIndex);
                
                // Use peak levels (linear 0..1+), average L/R for mono display
                float level = (readout.peakL + readout.peakR) * 0.5f;
                level = level * m_channel->getVolume(); // Scale by track volume
                
                if (level > 0.001f) {
                    level = std::min(1.0f, std::max(0.0f, level));
                    float visualLevel = std::pow(level, 0.5f); // Perceptual scaling
                    
                    float meterX = bounds.x + 20.0f; 
                    float meterY = bounds.y + 13.0f;
                    float meterW = 140.0f * visualLevel;
                    float meterH = 22.0f;
                    
                    AestraUI::NUIRect meterRect(meterX, meterY, meterW, meterH);
                    
                    AestraUI::NUIColor meterColor = themeManager.getColor("success").withAlpha(0.055f);
                    if (visualLevel > 0.8f) meterColor = themeManager.getColor("error").withAlpha(0.08f);
                    else if (visualLevel > 0.5f) meterColor = themeManager.getColor("warning").withAlpha(0.075f);
                    
                    renderer.fillRoundedRect(meterRect, 4.0f, meterColor);
                }
            }
        }
    }

    // Holographic Loading Feedback (Tech Aesthetic)
    if (m_isLoading) {
        auto cyan = themeManager.getColor("accentCyan");
        float time = static_cast<float>(Aestra::Platform::getUtils()->getTime());
        
        // Background Glow
        renderer.fillRect(controlAreaBounds, cyan.withAlpha(0.05f));
        
        // Scanning Bar
        float scanProgress = std::fmod(time * 1.2f, 1.0f);
        float scanX = controlAreaBounds.x + (controlAreaBounds.width * scanProgress);
        renderer.drawLine(AestraUI::NUIPoint(scanX, controlAreaBounds.y), 
                         AestraUI::NUIPoint(scanX, controlAreaBounds.bottom()), 
                         2.0f, cyan.withAlpha(0.3f * (1.0f - std::abs(scanProgress - 0.5f) * 2.0f)));

        // Animated "IMPORTING..." Text
        std::string loadingText = "IMPORTING";
        int dots = (int)(time * 3.0f) % 4;
        for(int i=0; i<dots; ++i) loadingText += ".";
        
        // Center text in control area
        float fontSize = 11.0f;
        auto textSize = renderer.measureText(loadingText, fontSize);
        renderer.drawText(loadingText, 
                         AestraUI::NUIPoint(controlAreaBounds.x + (controlAreaBounds.width - textSize.width) * 0.5f, 
                                          controlAreaBounds.y + controlAreaBounds.height - 12.0f), 
                         fontSize, cyan.withAlpha(0.8f));
    }

    // Apply highlight overlay (Selection / Solo / Mute)
    if (m_channel) {
        bool anySoloed = false;
        if (m_trackManager) {
            const size_t channelCount = m_trackManager->getChannelCount();
            for (size_t i = 0; i < channelCount; ++i) {
                if (auto ch = m_trackManager->getChannel(i)) {
                    if (ch->isSoloed()) {
                        anySoloed = true;
                        break;
                    }
                }
            }
        }

        const bool soloSuppressed = anySoloed && !m_channel->isSoloed();

        if (m_channel->isSoloed()) {
            renderer.fillRect(controlAreaBounds, themeManager.getColor("accentCyan").withAlpha(0.10f));
        } else if (m_channel->isMuted()) {
            renderer.fillRect(controlAreaBounds, themeManager.getColor("backgroundSecondary").withAlpha(0.62f));
        } else if (soloSuppressed) {
            renderer.fillRect(controlAreaBounds, themeManager.getColor("backgroundSecondary").withAlpha(0.44f));
        }

        // SELECTION OVERLAY
        if (m_selected) {
            renderer.fillRect(controlAreaBounds, themeManager.getColor("accentPrimary").withAlpha(0.08f));
            AestraUI::NUIRect selectionBar(controlAreaBounds.x, controlAreaBounds.y, 3.0f, controlAreaBounds.height);
            renderer.fillRect(selectionBar, themeManager.getColor("accentPrimary").withAlpha(0.95f));
        }
    }

    // Track color strip (identity)
    if (m_channel) {
        AestraUI::NUIColor stripColor;
        
        if (m_isLoading) {
            stripColor = themeManager.getColor("accentCyan");
            // Add a subtle pulse to the strip during loading
            float pulse = (std::sin(static_cast<float>(Aestra::Platform::getUtils()->getTime()) * 8.0f) * 0.5f + 0.5f);
            stripColor = stripColor.withAlpha(0.6f + pulse * 0.4f);
        } else {
            // Exact same logic as updateTrackNameColors to ensure match
            std::string trackName = m_channel->getName();
            size_t spacePos = trackName.find(' ');
            bool foundBrightColor = false;

            if (spacePos != std::string::npos) {
                static const std::vector<AestraUI::NUIColor> brightColors = {
                    AestraUI::NUIColor(1.0f, 0.8f, 0.2f, 1.0f),   
                    AestraUI::NUIColor(0.2f, 1.0f, 0.8f, 1.0f),   
                    AestraUI::NUIColor(1.0f, 0.4f, 0.8f, 1.0f),   
                    AestraUI::NUIColor(0.6f, 1.0f, 0.2f, 1.0f),   
                    AestraUI::NUIColor(1.0f, 0.6f, 0.2f, 1.0f),   
                    AestraUI::NUIColor(0.4f, 0.8f, 1.0f, 1.0f),   
                    AestraUI::NUIColor(1.0f, 0.2f, 0.4f, 1.0f),   
                    AestraUI::NUIColor(0.8f, 0.4f, 1.0f, 1.0f),   
                    AestraUI::NUIColor(1.0f, 0.9f, 0.1f, 1.0f),   
                    AestraUI::NUIColor(0.1f, 0.9f, 0.6f, 1.0f)    
                };

                uint32_t trackNumber = 0;
                if (parseTrailingTrackNumber(trackName, trackNumber)) {
                    size_t colorIndex = (trackNumber - 1) % brightColors.size();
                    stripColor = brightColors[colorIndex];
                    foundBrightColor = true;
                }
                
                if (!foundBrightColor) {
                   uint32_t trackId = m_channel->getChannelId();
                   size_t colorIndex = (trackId - 1) % brightColors.size();
                   stripColor = brightColors[colorIndex];
                   foundBrightColor = true;
                }
            }

            if (!foundBrightColor) {
                const uint32_t argb = m_channel->getColor();
                const float a = ((argb >> 24) & 0xFF) / 255.0f;
                const float r = ((argb >> 16) & 0xFF) / 255.0f;
                const float g = ((argb >> 8) & 0xFF) / 255.0f;
                const float b = (argb & 0xFF) / 255.0f;
                stripColor = AestraUI::NUIColor(r, g, b, a > 0.0f ? a : 1.0f);
            }
        }
        
        const float stripWidth = 3.0f;
        const float stripAlpha = (m_selected || m_channel->isArmed() || m_channel->isSoloed()) ? 0.90f : 0.64f;
        stripColor = restrainDawColor(stripColor, 0.86f, 0.62f, stripAlpha);
        
        // Draw strip
        renderer.fillRect(AestraUI::NUIRect(bounds.x, bounds.y, stripWidth, bounds.height), stripColor);

        // If loading, draw a small progress bar on the strip itself
        if (m_isLoading && m_loadProgress > 0.0f) {
            float progressHeight = bounds.height * std::min(1.0f, m_loadProgress);
            renderer.fillRect(AestraUI::NUIRect(bounds.x, bounds.bottom() - progressHeight, stripWidth, progressHeight), themeManager.getColor("textPrimary").withAlpha(0.75f));
        }

        // Selection Highlight Line (Inner Glow)
        if (m_selected) {
            auto glowColor = themeManager.getColor("highlightGlow");
            // Top highlight line inside control area (skipping strip)
            renderer.fillRect(AestraUI::NUIRect(bounds.x + stripWidth, bounds.y, controlAreaWidth - stripWidth, 1.0f), glowColor);
            // Bottom highlight
            renderer.fillRect(AestraUI::NUIRect(bounds.x + stripWidth, bounds.y + bounds.height - 1.0f, controlAreaWidth - stripWidth, 1.0f), glowColor.withAlpha(0.5f));
        }
    }

    // Explicit Separators for Control Area (ensures they are on top of background)
    // Top
    renderer.drawLine(
        AestraUI::NUIPoint(bounds.x, bounds.y),
        AestraUI::NUIPoint(bounds.x + controlAreaWidth, bounds.y),
        1.0f,
        themeManager.getColor("borderSubtle").withAlpha(0.84f)
    );
    // Bottom
    renderer.drawLine(
        AestraUI::NUIPoint(bounds.x, bounds.bottom() - 1),
        AestraUI::NUIPoint(bounds.x + controlAreaWidth, bounds.bottom() - 1),
        1.0f,
        themeManager.getColor("borderSubtle").withAlpha(0.84f)
    );


    // Draw vertical separator between control area and playlist area
    renderer.drawLine(
        AestraUI::NUIPoint(bounds.x + controlAreaWidth, bounds.y),
        AestraUI::NUIPoint(bounds.x + controlAreaWidth, bounds.y + bounds.height),
        1.0f,
        themeManager.getColor("glassBorder")
    );

    // Render the track name directly; track control widgets remain hit targets only.
    // Drawing the button widgets here reintroduces bordered/pill artifacts in cached rows.
    if (m_nameLabel) {
        m_nameLabel->onRender(renderer);
    }

    if (m_channel) {
        const auto textIdle = themeManager.getColor("textPrimary").withAlpha(isHovered() ? 0.68f : 0.54f);
        const auto muteActive = AestraUI::NUIColor::fromHex(0xf97316, 0.95f);
        const auto soloActive = themeManager.getColor("accentPrimary").withAlpha(0.95f);
        const auto recordActive = AestraUI::NUIColor::fromHex(0xef4444, 0.95f);
        const float fontSize = 11.0f;

        const auto drawButtonShell = [&](const std::shared_ptr<AestraUI::NUIButton>& button,
                                         bool active,
                                         AestraUI::NUIColor activeColor) {
            if (!button) {
                return;
            }
            const auto rect = button->getBounds();
            const bool hovered = button->isHovered() && button->isEnabled();
            AestraUI::NUIColor bg = AestraUI::NUIColor::white().withAlpha(hovered ? 0.050f : 0.022f);
            AestraUI::NUIColor border = themeManager.getColor("border").withAlpha(hovered ? 0.25f : 0.11f);
            if (active) {
                bg = activeColor.withAlpha(0.18f);
                border = activeColor.withAlpha(0.55f);
            }
            const float pillRadius = rect.height * 0.5f;
            renderer.fillRoundedRect(rect, pillRadius, bg);
            renderer.strokeRoundedRect(rect, pillRadius, 1.0f, border);
            if (active) {
                renderer.strokeRoundedRect(rect, pillRadius, 1.0f, activeColor.withAlpha(0.45f));
            }
        };

        const auto drawControlLabel = [&](const std::shared_ptr<AestraUI::NUIButton>& button,
                                          const std::string& label,
                                          AestraUI::NUIColor color,
                                          bool active,
                                          AestraUI::NUIColor activeColor) {
            if (!button) {
                return;
            }
            drawButtonShell(button, active, activeColor);
            const auto rect = button->getBounds();
            const auto textSize = renderer.measureText(label, fontSize);
            renderer.drawText(label,
                              AestraUI::NUIPoint(rect.x + (rect.width - textSize.width) * 0.5f,
                                                 std::round(renderer.calculateTextY(rect, fontSize))),
                              fontSize,
                              color);
        };

        // Draw volume knob (replaces route button)
        {
            const auto& knobBounds = m_volumeKnobBounds;
            if (!knobBounds.isEmpty()) {
                const float cx = knobBounds.x + knobBounds.width * 0.5f;
                const float cy = knobBounds.y + knobBounds.height * 0.5f;
                const float r = std::min(knobBounds.width, knobBounds.height) * 0.38f;

                // Arc angles: 7 o'clock (135°) to 5 o'clock (405°)
                constexpr float ARC_START = 135.0f * 3.14159265f / 180.0f;
                constexpr float ARC_END = 405.0f * 3.14159265f / 180.0f;
                const float t = std::clamp(m_volumeKnobValue, 0.0f, 1.5f) / 1.5f;
                const float currentAng = ARC_START + t * (ARC_END - ARC_START);

                // Background track arc
                const int segments = 24;
                std::vector<AestraUI::NUIPoint> trackPoints;
                trackPoints.reserve(segments + 1);
                for (int i = 0; i <= segments; ++i) {
                    float theta = ARC_START + i * (ARC_END - ARC_START) / segments;
                    trackPoints.push_back({cx + std::cos(theta) * r, cy + std::sin(theta) * r});
                }
                renderer.drawPolyline(trackPoints.data(), static_cast<int>(trackPoints.size()), 2.0f,
                                      AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.08f));

                // Active value arc
                if (t > 0.0f) {
                    std::vector<AestraUI::NUIPoint> activePoints;
                    int activeSegs = std::max(1, static_cast<int>(std::round(t * segments)));
                    activePoints.reserve(activeSegs + 1);
                    for (int i = 0; i <= activeSegs; ++i) {
                        float theta = ARC_START + i * (currentAng - ARC_START) / activeSegs;
                        activePoints.push_back({cx + std::cos(theta) * r, cy + std::sin(theta) * r});
                    }
                    AestraUI::NUIColor knobColor = themeManager.getColor("accentPrimary");
                    if (m_volumeKnobHovered || m_isDraggingVolumeKnob) knobColor = knobColor.withAlpha(1.0f);
                    else knobColor = knobColor.withAlpha(0.85f);
                    renderer.drawPolyline(activePoints.data(), static_cast<int>(activePoints.size()), 2.5f, knobColor);
                }

                // Pointer dot at current value
                const float ptrX = cx + std::cos(currentAng) * (r * 0.72f);
                const float ptrY = cy + std::sin(currentAng) * (r * 0.72f);
                renderer.fillCircle({ptrX, ptrY}, 1.8f, AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.9f));
            }
        }

        drawControlLabel(m_muteButton, "M", m_channel->isMuted() ? muteActive : textIdle,
                         m_channel->isMuted(), muteActive);
        drawControlLabel(m_soloButton, "S", m_channel->isSoloed() ? soloActive : textIdle,
                         m_channel->isSoloed(), soloActive);
        drawControlLabel(m_recordButton, m_channel->isMonitoringEnabled() ? "I" : "R",
                         m_channel->isArmed() ? recordActive : textIdle,
                         m_channel->isArmed(), recordActive);
    }

    // Track number marker (left of name): 10px, 40% white.
    if (m_nameLabel && m_channel) {
        constexpr float stripWidth = 3.0f;
        uint32_t trackNumber = m_channel->getChannelId();
        const auto laneName = m_nameLabel->getText();
        uint32_t parsedNumber = 0;
        if (parseTrailingTrackNumber(laneName, parsedNumber)) {
            trackNumber = parsedNumber;
        }
        const auto nameBounds = m_nameLabel->getBounds();
        renderer.drawText(
            std::to_string(trackNumber),
            AestraUI::NUIPoint(controlAreaBounds.x + stripWidth + 8.0f, nameBounds.y + 2.0f),
            10.5f,
            AestraUI::NUIColor(1.0f, 1.0f, 1.0f, m_selected ? 0.70f : 0.46f)
        );

    }
}

// Draw playlist grid (beat/bar grid)
void TrackUIComponent::drawPlaylistGrid(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& bounds) {
    AESTRA_ZONE("TrackUI_Grid");
    // Get layout dimensions from theme
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    const float controlAreaWidth = std::min(layout.trackControlsWidth, bounds.width);
    
    // Grid settings - start after control area (robust to narrow widths)
    const float desiredGap = 5.0f;
    const float gridGap = std::min(desiredGap, std::max(0.0f, bounds.width - controlAreaWidth));
    const float gridStartX = bounds.x + controlAreaWidth + gridGap;
    const float gridWidth = std::max(0.0f, bounds.width - controlAreaWidth - gridGap);
    const float gridEndX = gridStartX + gridWidth;

    if (gridWidth <= 0.0f) {
        return;
    }
    
    // 1. LOW-CONTRAST BAR SHADING
    float pixelsPerBar = m_pixelsPerBeat * m_beatsPerBar;
    int startBar = static_cast<int>(m_timelineScrollOffset / pixelsPerBar);
    int endBar = static_cast<int>((m_timelineScrollOffset + gridWidth) / pixelsPerBar) + 1;
    
    for (int bar = startBar; bar <= endBar; ++bar) {
        float x = gridStartX + (bar * pixelsPerBar) - m_timelineScrollOffset;
        
        // Zebra Striping: Draw slightly lighter background for odd bars
        if (bar % 2 != 0) {
             float rectX = x;
             float rectW = pixelsPerBar;
             
             // Manual clipping for zebra striping
             if (rectX < gridStartX) {
                 rectW -= (gridStartX - rectX);
                 rectX = gridStartX;
             }
             
             if (rectX + rectW > gridEndX) {
                 rectW = gridEndX - rectX;
             }
             
             if (rectW > 0 && rectX < gridEndX) {
                 renderer.fillRect(
                     AestraUI::NUIRect(rectX, bounds.y, rectW, bounds.height), 
                     AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.003f)
                 );
             }
        }
    }

    // DISABLED: LOOP REGION HIGHLIGHT (using blue ruler/grid highlight instead)
    /*
    if (m_loopEnabled && m_loopEndBeat > m_loopStartBeat) {
        // Convert loop beats to pixel positions
        float loopStartX = gridStartX + (static_cast<float>(m_loopStartBeat) * m_pixelsPerBeat) - m_timelineScrollOffset;
        float loopEndX = gridStartX + (static_cast<float>(m_loopEndBeat) * m_pixelsPerBeat) - m_timelineScrollOffset;
        
        // Clamp to grid bounds
        float drawStartX = std::max(loopStartX, gridStartX);
        float drawEndX = std::min(loopEndX, gridEndX);
        float loopWidth = drawEndX - drawStartX;
        
        if (loopWidth > 0 && drawStartX < gridEndX) {
            // Glass-colored loop region highlight
            AestraUI::NUIColor loopFillColor = themeManager.getColor("accentCyan").withAlpha(0.08f);
            renderer.fillRect(
                AestraUI::NUIRect(drawStartX, bounds.y, loopWidth, bounds.height),
                loopFillColor
            );
            
            // Draw loop boundary markers (vertical lines at start and end)
            AestraUI::NUIColor loopMarkerColor = themeManager.getColor("accentCyan").withAlpha(0.6f);
            
            // Start marker (if visible)
            if (loopStartX >= gridStartX && loopStartX <= gridEndX) {
                renderer.drawLine(
                    AestraUI::NUIPoint(loopStartX, bounds.y),
                    AestraUI::NUIPoint(loopStartX, bounds.y + bounds.height),
                    2.0f,
                    loopMarkerColor
                );
            }
            
            // End marker (if visible)
            if (loopEndX >= gridStartX && loopEndX <= gridEndX) {
                renderer.drawLine(
                    AestraUI::NUIPoint(loopEndX, bounds.y),
                    AestraUI::NUIPoint(loopEndX, bounds.y + bounds.height),
                    2.0f,
                    loopMarkerColor
                );
            }
        }
    }
    */

    // 2. GRID LINES SYNCED TO RULER LABEL INTERVAL
    // Mirror the ruler's barStride logic exactly so major grid lines always align
    // with labeled bars (e.g., 1/9/17/25 at lower zoom).
    const int beatsPerBar = std::max(1, m_beatsPerBar);
    int barStride = 1;
    const float minLabelSpacingPx = 28.0f; // matches TrackManagerUI::renderTimeRuler
    while ((pixelsPerBar * static_cast<float>(barStride)) < minLabelSpacingPx && barStride < 128) {
        barStride *= 2;
    }

    const double startBeat = m_timelineScrollOffset / m_pixelsPerBeat;
    const double endBeat = startBeat + (gridWidth / m_pixelsPerBeat);
    const int firstVisibleBar = static_cast<int>(std::floor(startBeat / static_cast<double>(beatsPerBar))) - 1;
    const int lastVisibleBar = static_cast<int>(std::ceil(endBeat / static_cast<double>(beatsPerBar))) + 1;

    // Grid hierarchy from palette tokens.
    const AestraUI::NUIColor barLineColor = AestraUI::NUIColor::fromHex(0x2a2a37).withAlpha(0.44f);
    const AestraUI::NUIColor beatLineColor = AestraUI::NUIColor::fromHex(0x242431).withAlpha(0.11f);
    const AestraUI::NUIColor subBeatLineColor = AestraUI::NUIColor::fromHex(0x22222d).withAlpha(0.055f);
    const bool drawBeatSubdivisions = (m_pixelsPerBeat >= 10.0f);
    const bool drawFurtherSubdivisions = (m_pixelsPerBeat >= 54.0f);

    for (int bar = firstVisibleBar; bar <= lastVisibleBar; ++bar) {
        const float barX = gridStartX + (bar * pixelsPerBar) - m_timelineScrollOffset;
        if (barX >= gridStartX && barX <= gridEndX) {
            const bool isPrimary = (barStride > 0) && (bar % barStride == 0);
            renderer.drawLine(
                AestraUI::NUIPoint(barX, bounds.y),
                AestraUI::NUIPoint(barX, bounds.y + bounds.height),
                isPrimary ? 1.05f : 1.0f,
                isPrimary ? barLineColor : subBeatLineColor
            );
        }

        // Unified subdivision visibility: beats at >=10 px, finer halves at >=30 px.
        if (drawBeatSubdivisions) {
            for (int beat = 1; beat < beatsPerBar; ++beat) {
                const float beatX = barX + (beat * m_pixelsPerBeat);
                if (beatX < gridStartX || beatX > gridEndX) continue;
                renderer.drawLine(
                    AestraUI::NUIPoint(beatX, bounds.y),
                    AestraUI::NUIPoint(beatX, bounds.y + bounds.height),
                    1.0f,
                    beatLineColor
                );
            }
        }

        if (drawFurtherSubdivisions) {
            const float halfBeatOffset = m_pixelsPerBeat * 0.5f;
            for (int beat = 0; beat < beatsPerBar; ++beat) {
                const float subBeatX = barX + (beat * m_pixelsPerBeat) + halfBeatOffset;
                if (subBeatX < gridStartX || subBeatX > gridEndX) continue;
                renderer.drawLine(
                    AestraUI::NUIPoint(subBeatX, bounds.y),
                    AestraUI::NUIPoint(subBeatX, bounds.y + bounds.height),
                    1.0f,
                    subBeatLineColor
                );
            }
        }
    }
}

void TrackUIComponent::onMouseEnter() {
    // Ensure parent knows we need updates (if any)
    NUIComponent::onMouseEnter();
    // Force cache invalidation immediately on enter
    if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback();
}

void TrackUIComponent::onMouseLeave() {
    NUIComponent::onMouseLeave();
    
    // Reset trim hover state when mouse leaves track bounds
    if (m_hoverTrimEdge != TrimEdge::None) {
        m_hoverTrimEdge = TrimEdge::None;
        repaint();
    }
    
    // Force cache invalidation immediately on leave
    if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback();
}

// (Stubs removed)

void TrackUIComponent::onUpdate(double deltaTime) {
    // Only update UI when track state might have changed, not every frame
    // This prevents overriding hover colors unnecessarily

    // Update children state
    if (m_channel) {
        bool currentMuted = m_channel->isMuted();
        bool currentSoloed = m_channel->isSoloed();

        // Check if buttons match channel state
        if (m_muteButton && m_muteButton->isToggled() != currentMuted) {
            m_muteButton->setToggled(currentMuted);
        }
        if (m_soloButton && m_soloButton->isToggled() != currentSoloed) {
            m_soloButton->setToggled(currentSoloed);
        }
    }

    // Update children
    NUIComponent::onUpdate(deltaTime);
}

void TrackUIComponent::onResize(int width, int height) {
    AestraUI::NUIRect bounds = getBounds();
    
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    const float controlAreaWidth = std::min(layout.trackControlsWidth, bounds.width);

    // Buttons cluster (horizontal, right-aligned within the control area)
    const float buttonW = 16.0f;
    const float buttonH = 18.0f;
    const float spacing = 6.0f;
    const int numButtons = 4; // M, S, R, Volume knob
    const float buttonsTotalW = numButtons * buttonW + (numButtons - 1) * spacing;

    const float leftPad = 14.0f;
    const float rightPad = 8.0f;
    const float trackNumberWidth = 14.0f;
    const float numberNameGap = 6.0f;

    // Position relative to component origin, then add absolute offset
    const float localButtonsXStart = controlAreaWidth - rightPad - buttonsTotalW;
    const float localButtonsY = (bounds.height - buttonH) * 0.5f;

    // Keep track number + name anchored to the left, with flexible space to buttons.
    const float localLabelLeft = leftPad + trackNumberWidth + numberNameGap;
    const float localInlineRight = localButtonsXStart - 8.0f;
    const float localInlineWidth = std::max(0.0f, localInlineRight - localLabelLeft);
    const float localNameHeight = std::max(14.0f, layout.trackLabelHeight - 2.0f);
    const float localNameY = localButtonsY + std::max(0.0f, (buttonH - localNameHeight) * 0.5f);
    const float localLabelWidth = std::max(40.0f, localInlineWidth);

    // Name label - use NUIAbsolute for global coordinate system
    if (m_nameLabel) {
        m_nameLabel->setBounds(AestraUI::NUIRect(bounds.x + localLabelLeft, bounds.y + localNameY, localLabelWidth, localNameHeight));
    }
    if (m_volumeFader) {
        m_volumeFader->setVisible(false);
    }

    float xCursor = localButtonsXStart;
    if (m_muteButton) {
        m_muteButton->setBounds(AestraUI::NUIRect(bounds.x + xCursor, bounds.y + localButtonsY, buttonW, buttonH));
        xCursor += buttonW + spacing;
    }
    if (m_soloButton) {
        m_soloButton->setBounds(AestraUI::NUIRect(bounds.x + xCursor, bounds.y + localButtonsY, buttonW, buttonH));
        xCursor += buttonW + spacing;
    }
    if (m_recordButton) {
        m_recordButton->setBounds(AestraUI::NUIRect(bounds.x + xCursor, bounds.y + localButtonsY, buttonW, buttonH));
        xCursor += buttonW + spacing;
    }
    // Volume knob in the slot where route button was
    m_volumeKnobBounds = AestraUI::NUIRect(bounds.x + xCursor, bounds.y + localButtonsY, buttonW + 2.0f, buttonH);

    AestraUI::NUIComponent::onResize(width, height);
}

// =============================================================================
// SECTION: Event Handling
// =============================================================================

bool TrackUIComponent::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    // 1. Invalidate Cache on mouse movement in control area (for button hover feedback)
    // AestraUI doesn't use event.type enum, so we infer from context.
    // Any mouse event in the control area checks for invalidation.
    if (m_onCacheInvalidationCallback) {
        // Check if we are over the control area
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float controlWidth = layout.trackControlsWidth;
        
        if (event.position.x <= getBounds().x + controlWidth) {
             m_onCacheInvalidationCallback();
        }
    }

    AestraUI::NUIRect bounds = getBounds();
    
    // Early exit: If event is outside our bounds and we're not in an active operation, don't handle it
    bool isInsideBounds = bounds.contains(event.position);
    bool isActiveOperation = m_isTrimming || m_isDraggingClip || m_clipDragPotential || m_isDraggingPoint;
    bool isControlCapture = m_isDraggingVolumeFader ||
                            m_isDraggingVolumeKnob ||
                            (m_muteButton && m_muteButton->isPressed()) ||
                            (m_soloButton && m_soloButton->isPressed()) ||
                            (m_recordButton && m_recordButton->isPressed());
    bool controlsNeedEvents = isControlCapture ||
                              (m_volumeFader && m_volumeFader->isHovered()) ||
                              (m_muteButton && m_muteButton->isHovered()) ||
                              (m_soloButton && m_soloButton->isHovered()) ||
                              (m_recordButton && m_recordButton->isHovered()) ||
                              m_volumeKnobHovered;
    
    if (!isInsideBounds && !isActiveOperation && !controlsNeedEvents) {
        return false;  // Let parent/siblings handle it (e.g., scrollbar)
    }
    
    // Get theme to determine control area bounds
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    float controlAreaWidth = layout.trackControlsWidth;
    float controlAreaEndX = bounds.x + controlAreaWidth;
    float gridStartX = bounds.x + controlAreaWidth + 5;
    float gridEndX = bounds.x + bounds.width - 5;
    
    // === HOVER EDGE DETECTION (for resize cursor) ===
    // Update hover state on every mouse move (not just press)
    if (!m_isTrimming && isInsideBounds && !event.pressed) {
        TrimEdge newHoverEdge = TrimEdge::None;
        
        for (const auto& [clipId, clipBounds] : m_allClipBounds) {
            if (!clipBounds.contains(event.position)) continue;
            
            float leftEdge = clipBounds.x;
            float rightEdge = clipBounds.x + clipBounds.width;
            
            // Check left edge
            if (std::abs(event.position.x - leftEdge) < TRIM_EDGE_WIDTH &&
                event.position.y >= clipBounds.y && 
                event.position.y <= clipBounds.y + clipBounds.height) {
                newHoverEdge = TrimEdge::Left;
                break;
            }
            
            // Check right edge
            if (std::abs(event.position.x - rightEdge) < TRIM_EDGE_WIDTH &&
                event.position.y >= clipBounds.y && 
                event.position.y <= clipBounds.y + clipBounds.height) {
                newHoverEdge = TrimEdge::Right;
                break;
            }
        }
        
        if (m_hoverTrimEdge != newHoverEdge) {
            m_hoverTrimEdge = newHoverEdge;
            repaint(); // Trigger redraw for cursor feedback
        }
    }
    
    // Keep button hover/press state accurate even when leaving the track row.
    // This is important for cached UIs (and prevents stuck hover/press visuals).
    if (isInsideBounds || controlsNeedEvents) {
        auto routeControlButton = [&](const std::shared_ptr<AestraUI::NUIButton>& button) -> bool {
            if (!button) return false;
            
            // Explicitly handle hover state since we are manually routing
            bool isOver = button->getBounds().contains(event.position);
            if (button->isHovered() != isOver) {
                button->setHovered(isOver);
            }

            const bool handled = button->onMouseEvent(event);
            return handled;
        };

        bool handledByControls = false;

        // Volume knob mouse handling
        {
            const bool isOver = m_volumeKnobBounds.contains(event.position);
            if (m_volumeKnobHovered != isOver) {
                m_volumeKnobHovered = isOver;
                if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback();
            }

            if (event.pressed && event.button == AestraUI::NUIMouseButton::Left && isOver) {
                m_isDraggingVolumeKnob = true;
                m_volumeKnobDragStartPos = event.position;
                m_volumeKnobDragStartValue = m_volumeKnobValue;
                if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback();
                handledByControls = true;
            } else if (event.released && event.button == AestraUI::NUIMouseButton::Left && m_isDraggingVolumeKnob) {
                m_isDraggingVolumeKnob = false;
                AestraUI::NUIComponent::hideRemoteTooltip(this);
                if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback();
                handledByControls = true;
            } else if (m_isDraggingVolumeKnob && event.button == AestraUI::NUIMouseButton::None) {
                // Dragging: vertical motion changes volume (up = louder)
                float dy = m_volumeKnobDragStartPos.y - event.position.y;
                float delta = dy * 0.008f;
                if (event.modifiers & AestraUI::NUIModifiers::Shift) {
                    delta *= 0.25f;
                }
                float newValue = std::clamp(m_volumeKnobDragStartValue + delta, 0.0f, 1.5f);
                if (std::abs(newValue - m_volumeKnobValue) > 1e-5f) {
                    m_volumeKnobValue = newValue;
                    if (m_channel && m_trackManager) {
                        m_trackManager->getCommandHistory().pushAndExecute(
                            std::make_shared<Aestra::Audio::SetVolumeCommand>(*m_channel, newValue));
                    }
                    // Show tooltip with percentage
                    int pct = static_cast<int>(std::round(newValue * 100.0f));
                    AestraUI::NUIComponent::showRemoteTooltip("Vol " + std::to_string(pct) + "%", event.position, this);
                    if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback();
                }
                handledByControls = true;
            }
        }

        if (m_volumeFader) {
            const bool isOver = m_volumeFader->getBounds().contains(event.position);
            if (m_volumeFader->isHovered() != isOver) {
                m_volumeFader->setHovered(isOver);
            }
            handledByControls = m_volumeFader->onMouseEvent(event) || handledByControls;
        }
        handledByControls = routeControlButton(m_muteButton) || handledByControls;
        handledByControls = routeControlButton(m_soloButton) || handledByControls;
        handledByControls = routeControlButton(m_recordButton) || handledByControls;

        if (isInsideBounds && m_volumeFader && m_volumeFader->getBounds().contains(event.position)) {
            AestraUI::NUIComponent::showRemoteTooltip("Track Volume", event.position, this);
        } else if (isInsideBounds && m_muteButton && m_muteButton->getBounds().contains(event.position)) {
            AestraUI::NUIComponent::showRemoteTooltip("Mute Track (M)", event.position, this);
        } else if (isInsideBounds && m_soloButton && m_soloButton->getBounds().contains(event.position)) {
            AestraUI::NUIComponent::showRemoteTooltip("Solo Track (S)", event.position, this);
        } else if (isInsideBounds && m_recordButton && m_recordButton->getBounds().contains(event.position)) {
            AestraUI::NUIComponent::showRemoteTooltip("Arm for Recording (O)", event.position, this);
        } else if (isInsideBounds && m_volumeKnobHovered) {
            int pct = static_cast<int>(std::round(m_volumeKnobValue * 100.0f));
            AestraUI::NUIComponent::showRemoteTooltip("Vol " + std::to_string(pct) + "%", event.position, this);
        } else if (isInsideBounds) {
            AestraUI::NUIComponent::hideRemoteTooltip(this);
        }

        if (event.pressed && event.button == AestraUI::NUIMouseButton::Right &&
            m_recordButton && m_recordButton->getBounds().contains(event.position)) {
            if (m_onTrackSelectedCallback) {
                m_onTrackSelectedCallback(this, false);
            }
            showRecordModeMenu(event.position);
            return true;
        }

        if (handledByControls) {
            // Clicking controls should also select the track (v3.1)
            if (event.pressed && event.button == AestraUI::NUIMouseButton::Left && m_onTrackSelectedCallback) {
                bool shift = (event.modifiers & AestraUI::NUIModifiers::Shift) ||
                             (event.modifiers & AestraUI::NUIModifiers::CapsLock);
                m_onTrackSelectedCallback(this, shift);
            }
            return true;
        }

        // === TRACK CONTEXT MENU (Right Click on Header/Control Area) ===
        if (event.pressed && event.button == AestraUI::NUIMouseButton::Right && event.position.x <= controlAreaEndX) {
            // Select track on right-click too
            if (m_onTrackSelectedCallback) {
                m_onTrackSelectedCallback(this, false);
            }

            if (auto parentMgr = dynamic_cast<TrackManagerUI*>(getParent())) {
                parentMgr->openTrackContextMenu(event.position, m_onSendToAuditionCallback);
                return true;
            }
        }
    }

    // PRIORITY 3: Automation Layer (v3.1)
    // Handle Mouse Release for automation point dragging FIRST - before any bounds checks
    // This ensures release is processed even when mouse has moved far outside bounds
    if (m_playlistMode == PlaylistMode::Automation && m_isDraggingPoint) {
        if (event.released && event.button == AestraUI::NUIMouseButton::Left) {
            m_isDraggingPoint = false;
            m_draggedPointIndex = -1;
            m_draggedCurveIndex = -1;
            
            // Release mouse capture
            if (auto parentMgr = dynamic_cast<TrackManagerUI*>(getParent())) {
                if (auto win = parentMgr->getPlatformWindow()) {
                    win->setMouseCapture(false);
                }
            }
            return true;
        }
    }
    
    if (m_playlistMode == PlaylistMode::Automation && (isInsideBounds || m_isDraggingPoint)) {
        if (event.position.x >= gridStartX || m_isDraggingPoint) {
            double beat = (event.position.x - gridStartX + m_timelineScrollOffset) / m_pixelsPerBeat;
            double value = 1.0 - std::clamp((static_cast<double>(event.position.y) - bounds.y) / bounds.height, 0.0, 1.0);
            
            auto& playlist = m_trackManager->getPlaylistModel();
            auto lane = playlist.getLane(m_laneId);

            if (lane && !lane->automationCurves.empty()) {
                auto& curve = lane->automationCurves[0]; // For now, automate first curve (Volume)

                // Right Click -> Delete Point
                if (event.pressed && event.button == AestraUI::NUIMouseButton::Right) {
                    auto& points = curve.getPoints();
                    for (int i = 0; i < (int)points.size(); ++i) {
                        float px = gridStartX + (static_cast<float>(points[i].beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
                        float py = bounds.y + (1.0f - static_cast<float>(points[i].value)) * bounds.height;
                        
                        if (AestraUI::distance({px, py}, event.position) < 12.0f) {
                            curve.removePoint(i);
                            setDirty(true);
                            repaint();
                            if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback();
                            return true;
                        }
                    }
                }

                // Left Click -> Select/Add Point
                if (event.pressed && event.button == AestraUI::NUIMouseButton::Left && isInsideBounds) {
                    int hitIndex = -1;
                    auto& points = curve.getPoints();
                    for (int i = 0; i < (int)points.size(); ++i) {
                        float px = gridStartX + (static_cast<float>(points[i].beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
                        float py = bounds.y + (1.0f - static_cast<float>(points[i].value)) * bounds.height;
                        
                        if (AestraUI::distance({px, py}, event.position) < 12.0f) {
                            hitIndex = i;
                            break;
                        }
                    }

                    if (hitIndex != -1) {
                        m_isDraggingPoint = true;
                        m_draggedPointIndex = hitIndex;
                        m_draggedCurveIndex = 0;
                        
                        // Capture mouse
                        if (auto parentMgr = dynamic_cast<TrackManagerUI*>(getParent())) {
                            if (auto win = parentMgr->getPlatformWindow()) {
                                win->setMouseCapture(true);
                            }
                        }
                        repaint(); // Request redraw to show selection state if any
                        if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback(); // Force parent update
                        return true;
                    } else if (isInsideBounds) {
                        // Add new point - Default to smooth curve (0.5 tension)
                        double bpm = m_trackManager ? m_trackManager->getPlaylistModel().getBPM() : 120.0;
                        double sampleRate = m_trackManager ? m_trackManager->getPlaylistModel().getProjectSampleRate() : 48000.0;
                        double samplesPerBeat = (sampleRate * 60.0) / std::max(bpm, 1.0);
                        curve.addPoint(beat, value, samplesPerBeat, 0.5f);
                        setDirty(true);
                        repaint(); // Immediate update
                        if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback(); // Force parent update
                        
                        // Start dragging the new point
                        auto& pts = curve.getPoints();
                        for (int i = 0; i < (int)pts.size(); ++i) {
                            if (std::abs(pts[i].beat - beat) < 0.001) {
                                m_isDraggingPoint = true;
                                m_draggedPointIndex = i;
                                m_draggedCurveIndex = 0;
                                
                                // Capture mouse
                                if (auto parentMgr = dynamic_cast<TrackManagerUI*>(getParent())) {
                                    if (auto win = parentMgr->getPlatformWindow()) {
                                        win->setMouseCapture(true);
                                    }
                                }
                                break;
                            }
                        }
                        return true;
                    }
                }

                // Dragging Logic
                if (m_isDraggingPoint && m_draggedCurveIndex == 0) {
                    auto& pts = curve.getPoints();
                    if (m_draggedPointIndex >= 0 && m_draggedPointIndex < (int)pts.size()) {
                        double newBeat = std::max(0.0, beat);
                        double newValue = value;
                        
                        pts[m_draggedPointIndex].beat = newBeat;
                        pts[m_draggedPointIndex].value = newValue;
                        
                        curve.sortPoints();
                        
                        // Re-find index after sort
                        for (int i = 0; i < (int)pts.size(); ++i) {
                            if (pts[i].beat == newBeat && pts[i].value == newValue) {
                                m_draggedPointIndex = i;
                                break;
                            }
                        }
                        
                        setDirty(true);
                        repaint(); // Ensure immediate redraw
                        if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback();
                        return true;
                    }
                }
            }
            
            // Allow selecting track in automation mode if not interacting with points
            if (event.pressed && event.button == AestraUI::NUIMouseButton::Left && isInsideBounds) {
                if (m_onTrackSelectedCallback) {
                    const bool shift = (event.modifiers & AestraUI::NUIModifiers::Shift) ||
                                       (event.modifiers & AestraUI::NUIModifiers::CapsLock);
                    m_onTrackSelectedCallback(this, shift);
                }
            }
            
            if (isInsideBounds) return true;
        }
    }
    
    auto& dragManager = AestraUI::NUIDragDropManager::getInstance();
    
    // Handle mouse release - always process to clear state
    if (!event.pressed && event.button == AestraUI::NUIMouseButton::Left) {
        bool wasActive = m_isTrimming || m_isDraggingClip || m_clipDragPotential;
        if (m_isTrimming) {
            Log::info("Finished trimming clip");
        }
        
        // Instant Drag Finish
        if (m_isDraggingClip) {
             if (auto parentMgr = dynamic_cast<TrackManagerUI*>(getParent())) {
                 parentMgr->finishInstantClipDrag();
                 if (auto win = parentMgr->getPlatformWindow()) {
                     win->setMouseCapture(false);
                 }
             }
        }

        m_clipDragPotential = false;
        m_isDraggingClip = false;
        m_isTrimming = false;
        m_trimEdge = TrimEdge::None;
        m_activeClipId = ClipInstanceID{};  // Clear active clip
        
        // Only consume the event if we were doing something
        if (wasActive) {
            return true;
        }
        return false;
    }
    
    // PRIORITY 1.5: Instant Clip Dragging Update
    if (m_isDraggingClip && !event.released && event.button == AestraUI::NUIMouseButton::Left) {
         if (auto parentMgr = dynamic_cast<TrackManagerUI*>(getParent())) {
             parentMgr->updateInstantClipDrag(event.position);
         }
         return true;
    }
    
    // PRIORITY 2: Handle active trimming (mouse move while trimming)
    if (m_isTrimming && m_activeClipId.isValid()) {
        auto& clipBounds = m_allClipBounds[m_activeClipId];
        float deltaX = event.position.x - m_trimDragStartX;
        
        if (m_trackManager && clipBounds.width > 0) {
            auto lane = m_trackManager->getPlaylistModel().getLane(m_laneId);
            if (lane) {
                for (size_t i = 0; i < lane->clips.size(); ++i) {
                    auto& clip = lane->clips[i];
                    if (clip.id == m_activeClipId) {
                        double deltaBeats = (deltaX / m_pixelsPerBeat);
                        
                        if (m_trimEdge == TrimEdge::Left) {
                            // Trim left: move start beat and reduce duration
                            double newStart = std::max(0.0, m_trimOriginalStart + deltaBeats);
                            newStart = snapBeatToGrid(newStart); // Apply snap
                            
                            double endBeat = m_trimOriginalStart + m_trimOriginalDuration;
                            clip.startBeat = std::min(newStart, endBeat - 0.1); // Keep minimum duration
                            clip.durationBeats = endBeat - clip.startBeat;
                        } else if (m_trimEdge == TrimEdge::Right) {
                            // Trim right: change end position (duration)
                            double newEnd = m_trimOriginalStart + m_trimOriginalDuration + deltaBeats;
                            newEnd = snapBeatToGrid(newEnd); // Apply snap
                            
                            clip.durationBeats = std::max(0.1, newEnd - clip.startBeat);
                        }
                        
                        if (m_onCacheInvalidationCallback) {
                            m_onCacheInvalidationCallback();
                        }
                        break;
                    }
                }
            }
        }
        return true;
    }
    
    // PRIORITY 3: Handle drag threshold detection on MOUSE MOVE
    if (m_clipDragPotential && !event.pressed && !event.released && !dragManager.isDragging()) {
        float dx = event.position.x - m_clipDragStartPos.x;
        float dy = event.position.y - m_clipDragStartPos.y;
        float distance = std::sqrt(dx * dx + dy * dy);
        
        const float DRAG_THRESHOLD = 5.0f;
        if (distance >= DRAG_THRESHOLD && m_activeClipId.isValid()) {
            m_isDraggingClip = true;
            m_clipDragPotential = false;
            
            // Replaced DragManager with Instant Drag
            if (auto parentMgr = dynamic_cast<TrackManagerUI*>(getParent())) {
                if (m_trackManager) {
                    auto lane = m_trackManager->getPlaylistModel().getLane(m_laneId);
                    if (lane) {
                        for (const auto& clip : lane->clips) {
                            if (clip.id == m_activeClipId && clip.patternId.isValid()) {
                                auto* pattern = m_trackManager->getPatternManager().getPattern(clip.patternId);
                                if (pattern && pattern->isMidi() && m_onPatternClipDragStarted) {
                                    m_onPatternClipDragStarted(clip.patternId);
                                }
                                break;
                            }
                        }
                    }
                }
                parentMgr->startInstantClipDrag(this, m_activeClipId, event.position);
                
                // Capture mouse to follow outside bounds
                if (auto win = parentMgr->getPlatformWindow()) {
                     win->setMouseCapture(true);
                }
            }
            
            return true;
        }
    }

    
    // PRIORITY 4: Handle clip manipulation in the grid/playlist area only (mouse press)
    if (event.pressed && event.button == AestraUI::NUIMouseButton::Left && isInsideBounds) {
        // Position relative to local component origin
        AestraUI::NUIPoint localPos(event.position.x - bounds.x, event.position.y - bounds.y);

        // Only process clip manipulation if click is in the grid area (not control area)
        if (localPos.x >= controlAreaWidth) {
            
            // Check if split tool is active
            bool isSplitToolActive = m_isSplitToolActiveCallback ? m_isSplitToolActiveCallback() : false;
            
            // === MULTI-CLIP HIT TESTING ===
            // Find which clip was clicked (check all clips in m_allClipBounds)
            ClipInstanceID clickedClipId = ClipInstanceID{};
            AestraUI::NUIRect clickedClipBounds;
            
            for (const auto& [clipId, clipBounds] : m_allClipBounds) {
                if (clipBounds.contains(event.position)) {
                    clickedClipId = clipId;
                    clickedClipBounds = clipBounds;
                    break;
                }
            }
            
            // Handle SPLIT TOOL - click on clip to split at that position
            if (isSplitToolActive && clickedClipId.isValid()) {
                // Calculate beat position using same math as the visual cursor in TrackManagerUI
                // This ensures the split occurs exactly where the preview line shows
                float mouseRelX = event.position.x - gridStartX + m_timelineScrollOffset;
                double mouseBeat = mouseRelX / static_cast<double>(m_pixelsPerBeat);
                
                Log::info("Split requested at " + std::to_string(mouseBeat) + " beats");
                
                if (m_onSplitRequestedCallback) {
                    // Pass the beat position - TrackManagerUI will snap it
                    m_onSplitRequestedCallback(this, mouseBeat);
                }
                return true;
            }

            // Handle PAINT TOOL - Click on empty space
            if (auto parentMgr = dynamic_cast<TrackManagerUI*>(getParent())) {
                if (parentMgr->getCurrentTool() == PlaylistTool::Paint && !clickedClipId.isValid()) {
                     double beat = (event.position.x - gridStartX + m_timelineScrollOffset) / m_pixelsPerBeat;
                     parentMgr->onPaintClip(this, beat);
                     return true;
                }
            }
            
            // Check if clicking on any clip for drag initiation or trimming
            if (clickedClipId.isValid()) {
                auto now = std::chrono::steady_clock::now();
                const long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                const bool manualDoubleClick =
                    (m_lastClickedClipId == clickedClipId) &&
                    (nowMs - m_lastClipClickTimeMs < 400);
                m_lastClickedClipId = clickedClipId;
                m_lastClipClickTimeMs = nowMs;

                if ((manualDoubleClick || event.doubleClick) && m_trackManager) {
                    auto lane = m_trackManager->getPlaylistModel().getLane(m_laneId);
                    if (lane) {
                        for (const auto& clip : lane->clips) {
                            if (clip.id == clickedClipId && clip.patternId.isValid()) {
                                auto* pattern = m_trackManager->getPatternManager().getPattern(clip.patternId);
                                if (pattern && pattern->isMidi() && m_onPatternClipOpenRequested) {
                                    m_onPatternClipOpenRequested(clip.patternId);
                                    return true;
                                }
                                break;
                            }
                        }
                    }
                }

                float leftEdge = clickedClipBounds.x;
                float rightEdge = clickedClipBounds.x + clickedClipBounds.width;
                
                // Left edge trim detection
                if (std::abs(event.position.x - leftEdge) < TRIM_EDGE_WIDTH &&
                    event.position.y >= clickedClipBounds.y && 
                    event.position.y <= clickedClipBounds.y + clickedClipBounds.height) {
                    m_trimEdge = TrimEdge::Left;
                    m_isTrimming = true;
                    m_trimDragStartX = event.position.x;
                    m_activeClipId = clickedClipId;
                    
                    // Store original state for relative drag
                    if (m_trackManager) {
                        auto lane = m_trackManager->getPlaylistModel().getLane(m_laneId);
                        if (lane) {
                            for (const auto& clip : lane->clips) {
                                if (clip.id == clickedClipId) {
                                    m_trimOriginalStart = clip.startBeat;
                                    m_trimOriginalDuration = clip.durationBeats;
                                    break;
                                }
                            }
                        }
                    }
                    
                    if (m_onTrackSelectedCallback) {
                        bool shift = (event.modifiers & AestraUI::NUIModifiers::Shift) ||
                                     (event.modifiers & AestraUI::NUIModifiers::CapsLock);
                        m_onTrackSelectedCallback(this, shift);
                    } else {
                        m_selected = true;
                    }
                    Log::info("Started trimming left edge of clip: " + clickedClipId.toString());
                    return true;
                }
                
                // Right edge trim detection
                if (std::abs(event.position.x - rightEdge) < TRIM_EDGE_WIDTH &&
                    event.position.y >= clickedClipBounds.y && 
                    event.position.y <= clickedClipBounds.y + clickedClipBounds.height) {
                    m_trimEdge = TrimEdge::Right;
                    m_isTrimming = true;
                    m_trimDragStartX = event.position.x;
                    m_activeClipId = clickedClipId;
                    
                    // Store original state for relative drag
                    if (m_trackManager) {
                        auto lane = m_trackManager->getPlaylistModel().getLane(m_laneId);
                        if (lane) {
                            for (size_t i = 0; i < lane->clips.size(); ++i) {
                                const auto& clip = lane->clips[i];
                                if (clip.id == clickedClipId) {
                                    m_trimOriginalStart = clip.startBeat;
                                    m_trimOriginalDuration = clip.durationBeats;
                                    break;
                                }
                            }
                        }
                    }
                    
                    if (m_onTrackSelectedCallback) {
                        bool shift = (event.modifiers & AestraUI::NUIModifiers::Shift) ||
                                     (event.modifiers & AestraUI::NUIModifiers::CapsLock);
                        m_onTrackSelectedCallback(this, shift);
                    } else {
                        m_selected = true;
                    }
                    Log::info("Started trimming right edge of clip: " + clickedClipId.toString());
                    return true;
                }
                
                m_clipDragPotential = true;
                m_clipDragStartPos = event.position;
                m_activeClipId = clickedClipId;
                if (m_onTrackSelectedCallback) {
                    bool shift = (event.modifiers & AestraUI::NUIModifiers::Shift) ||
                                 (event.modifiers & AestraUI::NUIModifiers::CapsLock);
                    m_onTrackSelectedCallback(this, shift);
                } else {
                    m_selected = true;
                }
                
                if (m_onClipSelectedCallback) {
                    m_onClipSelectedCallback(this, clickedClipId);
                }
                
                Log::info("Clip selected - ready for drag: " + clickedClipId.toString());
                return true;

            }

            
            // Grid area click (not on any clip) - select track
            if (m_onTrackSelectedCallback) {
                bool shift = (event.modifiers & AestraUI::NUIModifiers::Shift) ||
                             (event.modifiers & AestraUI::NUIModifiers::CapsLock);
                m_onTrackSelectedCallback(this, shift);
            } else {
                m_selected = true;
            }
            return true;
        }
        
        // Click in control area (but not on a button) - just select the track
        if (event.position.x < controlAreaEndX) {
            if (m_onTrackSelectedCallback) {
                bool shift = (event.modifiers & AestraUI::NUIModifiers::Shift) ||
                             (event.modifiers & AestraUI::NUIModifiers::CapsLock);
                m_onTrackSelectedCallback(this, shift);
            } else {
                m_selected = true;
            }
            return true;
        }
    }
    
    // Handle right-click delete using the same clip hit path as left-click selection.
    if (event.pressed && event.button == AestraUI::NUIMouseButton::Right && isInsideBounds) {
        ClipInstanceID clickedClipId = ClipInstanceID{};
        for (const auto& [clipId, clipBounds] : m_allClipBounds) {
            if (clipBounds.contains(event.position)) {
                clickedClipId = clipId;
                break;
            }
        }

        if (clickedClipId.isValid()) {
            m_activeClipId = clickedClipId;

            if (m_onTrackSelectedCallback) {
                m_onTrackSelectedCallback(this, false);
            } else {
                m_selected = true;
            }

            if (m_onClipSelectedCallback) {
                m_onClipSelectedCallback(this, clickedClipId);
            }

            if (m_onClipDeletedCallback) {
                m_onClipDeletedCallback(this, clickedClipId, event.position);
            }

            return true;
        }
    }

    // Pass through to parent if not handled
    return false;
}


void TrackUIComponent::renderAutomationLayer(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& bounds, float gridStartX) {
    auto& playlist = m_trackManager->getPlaylistModel();
    auto lane = playlist.getLane(m_laneId);
    if (!lane) return;

    auto& theme = AestraUI::NUIThemeManager::getInstance();
    
    // Automation Area bounds (exclude controls)
    AestraUI::NUIRect gridArea(gridStartX, bounds.y, bounds.width - (gridStartX - bounds.x), bounds.height);
    
    // For now, if no curves exist, let's create a default volume curve for testing (DELEEME LATER)
    if (lane->automationCurves.empty()) {
        // Just for demo purposes in this task
        // lane->automationCurves.push_back(AutomationCurve("Volume"));
    }

    for (const auto& curve : lane->automationCurves) {
        if (!curve.isVisible()) continue;

        const auto& points = curve.getPoints();
        if (points.empty()) {
            // Draw a flat line at default value if no points
            float y = gridArea.y + (1.0f - static_cast<float>(curve.getDefaultValue())) * gridArea.height;
            renderer.drawLine(AestraUI::NUIPoint(gridArea.x, y),
                              AestraUI::NUIPoint(gridArea.right(), y),
                              1.5f, theme.getColor("accentCyan").withAlpha(0.4f));
            continue;
        }

        double bpm = m_trackManager ? m_trackManager->getPlaylistModel().getBPM() : 120.0;
        double sampleRate = m_trackManager ? m_trackManager->getPlaylistModel().getProjectSampleRate() : 48000.0;
        double samplesPerBeat = (sampleRate * 60.0) / std::max(bpm, 1.0);

        AestraUI::NUIColor curveColor = theme.getColor("accentCyan");

        // Draw lines between points
        // Draw lines between points
        std::vector<AestraUI::NUIPoint> polyPoints;
        polyPoints.reserve(1024);

        for (size_t i = 1; i < points.size(); ++i) {
            const auto& p1 = points[i-1];
            const auto& p2 = points[i];
            
            // Adaptive subdivision based on screen space length
            float sx1 = gridStartX + (static_cast<float>(p1.beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
            float sy1 = gridArea.y + (1.0f - static_cast<float>(p1.value)) * gridArea.height;
            float sx2 = gridStartX + (static_cast<float>(p2.beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
            float sy2 = gridArea.y + (1.0f - static_cast<float>(p2.value)) * gridArea.height;
            
            float dist = std::sqrt(std::pow(sx2 - sx1, 2) + std::pow(sy2 - sy1, 2));
            
            // Use fine subdivisions for smooth curves - 1 vertex per pixel
            int steps = static_cast<int>(dist);
            steps = std::clamp(steps, 4, 512); // Min 4 for smooth curves, Max 512
            
            for (int s = 0; s <= steps; ++s) {
                 double t = static_cast<double>(s) / static_cast<double>(steps);
                 double beat = p1.beat + (p2.beat - p1.beat) * t;
                 double val = curve.getValueAtBeat(beat, samplesPerBeat);
                 
                 float x = gridStartX + (static_cast<float>(beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
                 float y = gridArea.y + (1.0f - static_cast<float>(val)) * gridArea.height;
                 
                 polyPoints.emplace_back(x, y);
            }
        }
        
        if (polyPoints.size() >= 2) {
            // Use thick 2px capsules - high subdivision minimizes angle between segments so joints are invisible
            renderer.drawPolyline(polyPoints.data(), static_cast<int>(polyPoints.size()), 2.0f, curveColor);
        }
            
        
        // Draw endpoints before/after first/last point if they are within view
        if (!points.empty()) {
            const auto& first = points.front();
            float fx = gridStartX + (static_cast<float>(first.beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
            float fy = gridArea.y + (1.0f - static_cast<float>(first.value)) * gridArea.height;
            if (fx > gridArea.x) {
                renderer.drawLine(AestraUI::NUIPoint(gridArea.x, fy), AestraUI::NUIPoint(fx, fy), 1.5f, curveColor.withAlpha(0.5f));
            }
            
            const auto& last = points.back();
            float lx = gridStartX + (static_cast<float>(last.beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
            float ly = gridArea.y + (1.0f - static_cast<float>(last.value)) * gridArea.height;
            if (lx < gridArea.right()) {
                renderer.drawLine(AestraUI::NUIPoint(lx, ly), AestraUI::NUIPoint(gridArea.right(), ly), 1.5f, curveColor.withAlpha(0.5f));
            }
        }

        // Draw point handles
        for (const auto& p : points) {
            float x = gridStartX + (static_cast<float>(p.beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
            float y = gridArea.y + (1.0f - static_cast<float>(p.value)) * gridArea.height;
            
            if (x < gridArea.x || x > gridArea.right()) continue;
            
            AestraUI::NUIColor ptColor = p.selected ? theme.getColor("primary") : curveColor;
            renderer.fillRect(AestraUI::NUIRect(x - 3, y - 3, 6, 6), ptColor);
            renderer.strokeRect(AestraUI::NUIRect(x - 4, y - 4, 8, 8), 1.0f, theme.getColor("border"));
        }
    }
}


void TrackUIComponent::drawLiveWaveform(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& bounds, float controlAreaWidth) {
    if (!m_trackManager->isRecording()) return;
    if (!m_channel->isArmed()) return;

    std::vector<float> recordingData;
    double startBeat = 0.0;
    bool gotSnapshot = m_trackManager->getRecordingDataSnapshot(m_channel->getChannelId(), recordingData, startBeat);
    
    if (!gotSnapshot || recordingData.empty()) return;

    // Layout parameters
    const float gridStartX = bounds.x + controlAreaWidth + 5.0f; // + gap
    const float centerY = bounds.y + bounds.height * 0.5f;
    const float halfHeight = bounds.height * 0.5f; // Use full height for live wave
    
    double bpm = m_trackManager->getPlaylistModel().getBPM();
    double sampleRate = m_trackManager->getOutputSampleRate(); // Usage of Output Rate is safe here as recording is captured at this rate
    if (sampleRate <= 0.0) sampleRate = 48000.0;
    
    // Map samples to pixels
    // pixels_per_sample = pixels_per_beat * beats_per_second / samples_per_second
    // beats_per_second = bpm / 60
    double bitsPerSecond = bpm / 60.0;
    double samplesPerPixel = sampleRate / (bitsPerSecond * m_pixelsPerBeat);
    
    // Calculate start X in screen coordinates
    float startX = gridStartX + (static_cast<float>(startBeat) * m_pixelsPerBeat) - m_timelineScrollOffset;
    
    size_t totalSamples = recordingData.size();
    float endX = startX + (totalSamples / static_cast<float>(samplesPerPixel));
    
    if (endX < gridStartX || startX > bounds.right()) return;

    // Drawing Loop (Decimated)
    AestraUI::NUIColor waveColor = AestraUI::NUIThemeManager::getInstance().getColor("error"); // Red for recording
    
    std::vector<AestraUI::NUIPoint> topPoints;
    std::vector<AestraUI::NUIPoint> bottomPoints;
    
    float visibleStartPixel = std::max(gridStartX, startX) - startX;
    float visibleEndPixel = std::min(bounds.right(), endX) - startX;
    
    if (visibleEndPixel <= visibleStartPixel) return;
    
    int startPixelInt = static_cast<int>(visibleStartPixel);
    int endPixelInt = static_cast<int>(visibleEndPixel);
    
    size_t numPoints = endPixelInt - startPixelInt;
    topPoints.reserve(numPoints);
    bottomPoints.reserve(numPoints);
    
    for (int p = startPixelInt; p < endPixelInt; ++p) {
        size_t sampleIndex = static_cast<size_t>(p * samplesPerPixel);
        size_t nextSampleIndex = static_cast<size_t>((p + 1) * samplesPerPixel);
        
        if (sampleIndex >= totalSamples) break;
        if (nextSampleIndex > totalSamples) nextSampleIndex = totalSamples;
        
        float peak = 0.0f;
        for (size_t i = sampleIndex; i < nextSampleIndex; ++i) {
            float val = std::abs(recordingData[i]);
            if (val > peak) peak = val;
        }
        
        float env = std::pow(std::min(1.0f, peak), 0.75f);
        
        float screenX = startX + p;
        float topY = centerY - env * halfHeight;
        float bottomY = centerY + env * halfHeight;
        
        if (bottomY - topY < 1.0f) {
            topY = centerY - 0.5f;
            bottomY = centerY + 0.5f;
        }
        
        topPoints.push_back(AestraUI::NUIPoint(screenX, topY));
        bottomPoints.push_back(AestraUI::NUIPoint(screenX, bottomY));
    }
    
    if (!topPoints.empty()) {
        const AestraUI::NUIColor topFill = waveColor.withAlpha(0.35f);
        const AestraUI::NUIColor bottomFill = waveColor.withAlpha(0.14f);
        const AestraUI::NUIColor peakLine = waveColor.withAlpha(0.55f);
        const AestraUI::NUIColor centerLine = waveColor.withAlpha(0.15f);

        for (size_t i = 0; i < topPoints.size(); ++i) {
            const float x = topPoints[i].x;
            const float topY = topPoints[i].y;
            const float bottomY = bottomPoints[i].y;

            renderer.drawLine(AestraUI::NUIPoint(x, centerY), AestraUI::NUIPoint(x, topY), 1.0f, topFill);
            renderer.drawLine(AestraUI::NUIPoint(x, centerY), AestraUI::NUIPoint(x, bottomY), 1.0f, bottomFill);
            renderer.drawLine(AestraUI::NUIPoint(x, topY), AestraUI::NUIPoint(x, bottomY), 1.0f, peakLine);
        }

        renderer.drawLine(
            AestraUI::NUIPoint(topPoints.front().x, centerY),
            AestraUI::NUIPoint(topPoints.back().x, centerY),
            1.0f,
            centerLine
        );
    }
}

} // namespace Audio
} // namespace Aestra
