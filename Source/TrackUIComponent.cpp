// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "TrackUIComponent.h"
#include "TrackManagerUI.h"
#include "../NomadUI/Platform/NUIPlatformBridge.h"
#include "../NomadAudio/include/MixerChannel.h"
#include "../NomadAudio/include/TrackManager.h"
#include "../NomadAudio/include/PlaylistModel.h"
#include "../NomadAudio/include/PatternManager.h"

#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadCore/include/NomadLog.h"
#include <algorithm>
#include <algorithm>
#include <cmath>
#include <chrono>

namespace Nomad {
namespace Audio {

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
    m_nameLabel = std::make_shared<NomadUI::NUILabel>();
    
    std::string name = "Lane";
    if (m_trackManager) {
        if (auto lane = m_trackManager->getPlaylistModel().getLane(m_laneId)) {
            name = lane->name;
        }
    }
    m_nameLabel->setText(name.empty() ? m_channel->getName() : name);

    {
        auto& themeManager = NomadUI::NUIThemeManager::getInstance();
        // Use large font for track names
        m_nameLabel->setFontSize(themeManager.getFontSize("l"));
    }
    m_nameLabel->setEllipsize(true);
    updateTrackNameColors();
    addChild(m_nameLabel);

    // Create mute button
    m_muteButton = std::make_shared<NomadUI::NUIButton>();
    m_muteButton->setText("M");
    m_muteButton->setStyle(NomadUI::NUIButton::Style::Secondary); 
    m_muteButton->setToggleable(true);
    m_muteButton->setHoverColor(NomadUI::NUIThemeManager::getInstance().getColor("textSecondary").withAlpha(0.4f));
    m_muteButton->setPressedColor(NomadUI::NUIThemeManager::getInstance().getColor("accentAmber")); 
    m_muteButton->setTextColor(NomadUI::NUIColor::white());
    m_muteButton->setFontSize(NomadUI::NUIThemeManager::getInstance().getFontSize("m"));
    m_muteButton->setCornerRadius(13.0f);
    m_muteButton->setCornerRadius(13.0f);
    m_muteButton->setOnToggle([this](bool) { onMuteToggled(); });
    m_muteButton->setTooltip("Mute Track (M)");
    addChild(m_muteButton);

    // Create solo button
    m_soloButton = std::make_shared<NomadUI::NUIButton>();
    m_soloButton->setText("S");
    m_soloButton->setStyle(NomadUI::NUIButton::Style::Secondary);
    m_soloButton->setToggleable(true);
    m_soloButton->setHoverColor(NomadUI::NUIThemeManager::getInstance().getColor("textSecondary").withAlpha(0.4f));
    m_soloButton->setPressedColor(NomadUI::NUIThemeManager::getInstance().getColor("accentCyan"));
    m_soloButton->setTextColor(NomadUI::NUIColor::white());
    m_soloButton->setFontSize(NomadUI::NUIThemeManager::getInstance().getFontSize("m"));
    m_soloButton->setCornerRadius(13.0f);
    m_soloButton->setCornerRadius(13.0f);
    m_soloButton->setOnToggle([this](bool) { onSoloToggled(); });
    m_soloButton->setTooltip("Solo Track (S)");
    addChild(m_soloButton);

    // Create record button
    m_recordButton = std::make_shared<NomadUI::NUIButton>();
    m_recordButton->setText("R");
    m_recordButton->setStyle(NomadUI::NUIButton::Style::Secondary);
    m_recordButton->setToggleable(true);
    m_recordButton->setHoverColor(NomadUI::NUIThemeManager::getInstance().getColor("textSecondary").withAlpha(0.4f));
    m_recordButton->setPressedColor(NomadUI::NUIThemeManager::getInstance().getColor("error"));
    m_recordButton->setFontSize(NomadUI::NUIThemeManager::getInstance().getFontSize("m"));
    m_recordButton->setCornerRadius(13.0f);
    m_recordButton->setCornerRadius(13.0f);
    m_recordButton->setOnToggle([this](bool) { onRecordToggled(); });
    m_recordButton->setTooltip("Arm for Recording (R)");
    addChild(m_recordButton);

    updateUI();
}

// ...






TrackUIComponent::~TrackUIComponent() {
    Log::info("TrackUIComponent destroyed for lane: " + m_laneId.toString());
}

// UI Callbacks
void TrackUIComponent::onVolumeChanged(float volume) {
    if (m_channel) {
        m_channel->setVolume(volume);
        Log::info("Lane " + m_laneId.toString() + " volume: " + std::to_string(volume));
    }
}

void TrackUIComponent::onPanChanged(float pan) {
    if (m_channel) {
        m_channel->setPan(pan);
        Log::info("Lane " + m_laneId.toString() + " pan: " + std::to_string(pan));
    }
}


void TrackUIComponent::onMuteToggled() {
    if (m_channel) {
        bool isMuted = m_muteButton->isToggled();
        m_channel->setMute(isMuted);
        
        // Mutual Exclusivity: If Muting, turn off Solo
        if (isMuted && m_channel->isSoloed()) {
             Log::info("Mutual Exclusivity: Turning OFF Solo because Mute activated.");
             m_channel->setSolo(false);
             if (m_soloButton) m_soloButton->setToggled(false);
        }
        
        Log::info("Lane " + m_laneId.toString() + " muted: " + (isMuted ? "ON" : "OFF"));
        updateUI(); 
        repaint();
        if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback();
    }
}


void TrackUIComponent::onSoloToggled() {
    if (m_channel) {
        bool newSolo = m_soloButton->isToggled(); // Use button state
        m_channel->setSolo(newSolo);
        
        // Mutual Exclusivity: If Soloing, turn off Mute
        if (newSolo && m_channel->isMuted()) {
            Log::info("Mutual Exclusivity: Turning OFF Mute because Solo activated.");
            m_channel->setMute(false);
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
        // Record state handling to be determined in v3.0
        Log::info("Lane " + m_laneId.toString() + " record toggled");
        updateUI();
    }
}


void TrackUIComponent::updateUI() {
    if (!m_channel) return;

    // Invalidate parent cache since button colors are changing
    if (m_onCacheInvalidationCallback) {
        m_onCacheInvalidationCallback();
    }

    // Update track name colors with bright colors based on number
    updateTrackNameColors();

    auto& themeManager = NomadUI::NUIThemeManager::getInstance();

    // Standard Glassy Look for Inactive State (Grey Glass)
    NomadUI::NUIColor inactiveBg = themeManager.getColor("textSecondary").withAlpha(0.15f);
    NomadUI::NUIColor inactiveHover = themeManager.getColor("textSecondary").withAlpha(0.25f);
    NomadUI::NUIColor inactiveText = themeManager.getColor("textSecondary");

    if (m_muteButton) {
        m_muteButton->setToggled(m_channel->isMuted());
        
        if (m_channel->isMuted()) {
            // Active: Strong Neon Amber (Mute)
            m_muteButton->setBackgroundColor(themeManager.getColor("accentAmber").withAlpha(0.6f));
            m_muteButton->setTextColor(NomadUI::NUIColor::white()); 
            m_muteButton->setHoverColor(themeManager.getColor("accentAmber").withAlpha(0.8f));
            m_muteButton->setBorderEnabled(true);
        } else {
            // Inactive: Grey Glass (Better visibility than black)
            m_muteButton->setBackgroundColor(inactiveBg);
            m_muteButton->setTextColor(inactiveText);
            m_muteButton->setHoverColor(inactiveHover);
            m_muteButton->setBorderEnabled(true);
        }
    }

    if (m_soloButton) {
        m_soloButton->setToggled(m_channel->isSoloed());
        
        if (m_channel->isSoloed()) {
            // Active: Strong Neon Cyan (Solo)
            m_soloButton->setBackgroundColor(themeManager.getColor("accentCyan").withAlpha(0.6f));
            m_soloButton->setTextColor(NomadUI::NUIColor::white());
            m_soloButton->setHoverColor(themeManager.getColor("accentCyan").withAlpha(0.8f));
            m_soloButton->setBorderEnabled(true);
        } else {
            // Inactive: Grey Glass
            m_soloButton->setBackgroundColor(inactiveBg);
            m_soloButton->setTextColor(inactiveText);
            m_soloButton->setHoverColor(inactiveHover);
            m_soloButton->setBorderEnabled(true);
        }
    }

    if (m_recordButton) {
        // Record state styling
        // Assuming inactive for now, but styling for consistency
        m_recordButton->setBackgroundColor(inactiveBg);
        m_recordButton->setTextColor(inactiveText);
        m_recordButton->setHoverColor(inactiveHover);
        m_recordButton->setBorderEnabled(true);
    }
}


void TrackUIComponent::updateTrackNameColors() {
    if (!m_nameLabel || !m_channel) return;

    std::string trackName = m_channel->getName();

    // Apply bright colors based on track number for "Track X" format
    size_t spacePos = trackName.find(' ');
    if (spacePos != std::string::npos) {
        // Create bright colors for the track based on number
        static const std::vector<NomadUI::NUIColor> brightColors = {
            NomadUI::NUIColor(1.0f, 0.8f, 0.2f, 1.0f),   // Bright yellow/gold
            NomadUI::NUIColor(0.2f, 1.0f, 0.8f, 1.0f),   // Bright cyan
            NomadUI::NUIColor(1.0f, 0.4f, 0.8f, 1.0f),   // Bright pink/magenta
            NomadUI::NUIColor(0.6f, 1.0f, 0.2f, 1.0f),   // Bright lime
            NomadUI::NUIColor(1.0f, 0.6f, 0.2f, 1.0f),   // Bright orange
            NomadUI::NUIColor(0.4f, 0.8f, 1.0f, 1.0f),   // Bright blue
            NomadUI::NUIColor(1.0f, 0.2f, 0.4f, 1.0f),   // Bright red
            NomadUI::NUIColor(0.8f, 0.4f, 1.0f, 1.0f),   // Bright purple
            NomadUI::NUIColor(1.0f, 0.9f, 0.1f, 1.0f),   // Bright yellow
            NomadUI::NUIColor(0.1f, 0.9f, 0.6f, 1.0f)    // Bright teal
        };

        // Extract track number from name for consistent coloring
        // For "Track X" format, use X-1 for 0-based indexing
        size_t numberPos = trackName.find_last_not_of("0123456789");
        if (numberPos != std::string::npos && numberPos < trackName.length() - 1) {
            std::string numberStr = trackName.substr(numberPos + 1);
            try {
                uint32_t trackNumber = std::stoul(numberStr);
                size_t colorIndex = (trackNumber - 1) % brightColors.size();
                m_nameLabel->setTextColor(brightColors[colorIndex]);
                return; // Successfully set color, exit
            } catch (const std::exception&) {
                // Fall through to fallback if number parsing fails
            }
        }

        // Get track index from ID for consistent coloring (fallback)
        uint32_t trackId = m_channel->getChannelId();
        size_t colorIndex = (trackId - 1) % brightColors.size();
        m_nameLabel->setTextColor(brightColors[colorIndex]);
    } else {
        // Fallback for non-standard track names
        uint32_t color = m_channel->getColor();
        float r = ((color >> 16) & 0xFF) / 255.0f * 0.8f;
        float g = ((color >> 8) & 0xFF) / 255.0f * 0.8f;
        float b = (color & 0xFF) / 255.0f * 0.8f;
        float a = ((color >> 24) & 0xFF) / 255.0f;
        m_nameLabel->setTextColor(NomadUI::NUIColor(r, g, b, a));
    }
}

void TrackUIComponent::generateWaveformCache(int, int) {
    // Waveform caching for entire track is deprecated in v3.0 (clips have their own caching)
}

// Draw waveform for a specific clip
void TrackUIComponent::drawWaveformForClip(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& bounds,
                                          const ClipInstance& clip, float offsetRatio, float visibleRatio) {
    if (!m_trackManager) return;

    // Resolve audio data through Pattern and Source managers
    auto& patternMgr = m_trackManager->getPatternManager();
    auto& sourceMgr = m_trackManager->getSourceManager();
    
    auto pattern = patternMgr.getPattern(clip.patternId);
    if (!pattern || !pattern->isAudio()) return;
    
    auto audioPayload = std::get_if<AudioSlicePayload>(&pattern->payload);
    if (!audioPayload) return;
    
    auto source = sourceMgr.getSource(audioPayload->audioSourceId);
    if (!source || !source->isReady()) return;
    
    auto bufferPtr = source->getBuffer();
    if (!bufferPtr || bufferPtr->numFrames == 0) return;

    const auto& audioData = *bufferPtr;
    const std::vector<float>& samples = audioData.interleavedData;

    int width = static_cast<int>(bounds.width);
    int height = static_cast<int>(bounds.height);
    
    // Get color from clip (v3.0) or Sync with Track (v3.1)
    NomadUI::NUIColor waveformColor;
    
    // Sync with Track Bright Color logic
    bool determined = false;
    if (m_channel) {
        std::string trackName = m_channel->getName();
        size_t spacePos = trackName.find(' ');
        bool foundBrightColor = false;
        
        static const std::vector<NomadUI::NUIColor> brightColors = {
            NomadUI::NUIColor(1.0f, 0.8f, 0.2f, 1.0f),   
            NomadUI::NUIColor(0.2f, 1.0f, 0.8f, 1.0f),   
            NomadUI::NUIColor(1.0f, 0.4f, 0.8f, 1.0f),   
            NomadUI::NUIColor(0.6f, 1.0f, 0.2f, 1.0f),   
            NomadUI::NUIColor(1.0f, 0.6f, 0.2f, 1.0f),   
            NomadUI::NUIColor(0.4f, 0.8f, 1.0f, 1.0f),   
            NomadUI::NUIColor(1.0f, 0.2f, 0.4f, 1.0f),   
            NomadUI::NUIColor(0.8f, 0.4f, 1.0f, 1.0f),   
            NomadUI::NUIColor(1.0f, 0.9f, 0.1f, 1.0f),   
            NomadUI::NUIColor(0.1f, 0.9f, 0.6f, 1.0f)    
        };

        if (spacePos != std::string::npos) {
            size_t numberPos = trackName.find_last_not_of("0123456789");
            if (numberPos != std::string::npos && numberPos < trackName.length() - 1) {
                 std::string numberStr = trackName.substr(numberPos + 1);
                 try {
                     uint32_t trackNumber = std::stoul(numberStr);
                     size_t colorIndex = (trackNumber - 1) % brightColors.size();
                     waveformColor = brightColors[colorIndex];
                     foundBrightColor = true;
                 } catch (...) {}
            }
            if (!foundBrightColor) {
               uint32_t trackId = m_channel->getChannelId();
               size_t colorIndex = (trackId - 1) % brightColors.size();
               waveformColor = brightColors[colorIndex];
               foundBrightColor = true;
            }
        }
        
        if (!foundBrightColor) {
               uint32_t c = m_channel->getColor();
               waveformColor = NomadUI::NUIColor((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, (c >> 24) & 0xFF) / 255.0f;
        }
        determined = true;
    } 
    
    if (!determined) {
        uint32_t color = clip.colorRGBA;
        waveformColor = NomadUI::NUIColor(
            (color >> 16) & 0xFF,
            (color >> 8) & 0xFF,
            color & 0xFF,
            (color >> 24) & 0xFF
        ) / 255.0f;
    }

    waveformColor = waveformColor.withAlpha(0.7f);

    int centerY = static_cast<int>(bounds.y + height / 2);
    
    // Draw center line
    renderer.drawLine(
        NomadUI::NUIPoint(bounds.x, static_cast<float>(centerY)),
        NomadUI::NUIPoint(bounds.x + bounds.width, static_cast<float>(centerY)),
        1.0f,
        waveformColor.withAlpha(0.3f)
    );
    
    // Calculate sample range to draw
    size_t numChannels = audioData.numChannels;
    size_t totalFrames = audioData.numFrames;
    size_t startFrame = static_cast<size_t>(offsetRatio * totalFrames);
    size_t endFrame = static_cast<size_t>((offsetRatio + visibleRatio) * totalFrames);
    
    startFrame = std::min(startFrame, totalFrames);
    endFrame = std::min(endFrame, totalFrames);
    
    size_t visibleFrames = endFrame - startFrame;
    if (visibleFrames == 0 || width <= 0) return;
    
    // Build waveform as points
    std::vector<NomadUI::NUIPoint> topPoints;
    std::vector<NomadUI::NUIPoint> bottomPoints;
    topPoints.reserve(width);
    bottomPoints.reserve(width);
    
    const float kRmsSmoothing = 0.5f;
    float rmsSmooth = 0.0f;
    const float halfHeight = height * 0.5f;

    // Fixed: Ensure hp variables are initialized outside the loop
    float hpX1L = 0, hpY1L = 0, hpX1R = 0, hpY1R = 0;
    const float hpA = 0.99f; // Simple DC blocker coefficient
    
    for (int x = 0; x < width; ++x) {
        size_t frameIndex = startFrame + (static_cast<size_t>(x) * visibleFrames / width);
        size_t frameEnd = startFrame + (static_cast<size_t>(x + 1) * visibleFrames / width);
        frameEnd = std::min(frameEnd, totalFrames);
        
        float peak = 0.0f;
        double sumSq = 0.0;
        int count = 0;

        for (size_t f = frameIndex; f < frameEnd; ++f) {
            size_t base = f * numChannels;
            if (base + (numChannels - 1) >= samples.size()) break;

            if (numChannels == 1) {
                const float inL = samples[base];
                const float hp = hpA * (hpY1L + inL - hpX1L);
                hpX1L = inL;
                hpY1L = hp;

                const float visual = hp * 0.85f + inL * 0.15f;
                peak = std::max(peak, std::abs(visual));
                sumSq += static_cast<double>(visual) * static_cast<double>(visual);
                ++count;
            } else {
                const float inL = samples[base];
                const float inR = samples[base + 1];

                const float hpL = hpA * (hpY1L + inL - hpX1L);
                hpX1L = inL;
                hpY1L = hpL;

                const float hpR = hpA * (hpY1R + inR - hpX1R);
                hpX1R = inR;
                hpY1R = hpR;

                const float visL = hpL * 0.85f + inL * 0.15f;
                const float visR = hpR * 0.85f + inR * 0.15f;

                peak = std::max(peak, std::max(std::abs(visL), std::abs(visR)));
                sumSq += static_cast<double>(visL) * static_cast<double>(visL);
                sumSq += static_cast<double>(visR) * static_cast<double>(visR);
                count += 2;
            }
        }

        const float rms = (count > 0) ? static_cast<float>(std::sqrt(sumSq / static_cast<double>(count))) : 0.0f;
        rmsSmooth += (rms - rmsSmooth) * kRmsSmoothing;

        float env = rmsSmooth * 0.65f + peak * 0.35f;
        env = std::pow(std::min(1.0f, env), 0.75f);
        
        // Calculate screen coordinates
        float topY = static_cast<float>(centerY) - env * halfHeight;
        float bottomY = static_cast<float>(centerY) + env * halfHeight;
        
        // Ensure silence is rendered as a 1px line
        if (bottomY - topY < 1.0f) {
            topY = static_cast<float>(centerY) - 0.5f;
            bottomY = static_cast<float>(centerY) + 0.5f;
        }
        
        topPoints.push_back(NomadUI::NUIPoint(bounds.x + x, topY));
        bottomPoints.push_back(NomadUI::NUIPoint(bounds.x + x, bottomY));
    }
    
    // Single draw call for entire waveform with GRADIENT (bright top, darker bottom)
    if (!topPoints.empty()) {
        NomadUI::NUIColor colorTop = waveformColor.lightened(0.15f);   // Brighter at peaks
        NomadUI::NUIColor colorBottom = waveformColor.darkened(0.25f);  // Darker at center
        renderer.fillWaveformGradient(topPoints.data(), bottomPoints.data(), static_cast<int>(topPoints.size()), colorTop, colorBottom);
    }
}

void TrackUIComponent::drawSampleClipForClip(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& clipBounds,
                                            const NomadUI::NUIRect& fullClipBounds, const ClipInstance& clip) {
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    
    // Rounded corners for "pill" aesthetic
    const float clipRadius = 4.0f;
    
    // Get clip color from pattern via PatternManager
    NomadUI::NUIColor clipColor = themeManager.getColor("primary");
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
                static const std::vector<NomadUI::NUIColor> brightColors = {
                    NomadUI::NUIColor(1.0f, 0.8f, 0.2f, 1.0f),   
                    NomadUI::NUIColor(0.2f, 1.0f, 0.8f, 1.0f),   
                    NomadUI::NUIColor(1.0f, 0.4f, 0.8f, 1.0f),   
                    NomadUI::NUIColor(0.6f, 1.0f, 0.2f, 1.0f),   
                    NomadUI::NUIColor(1.0f, 0.6f, 0.2f, 1.0f),   
                    NomadUI::NUIColor(0.4f, 0.8f, 1.0f, 1.0f),   
                    NomadUI::NUIColor(1.0f, 0.2f, 0.4f, 1.0f),   
                    NomadUI::NUIColor(0.8f, 0.4f, 1.0f, 1.0f),   
                    NomadUI::NUIColor(1.0f, 0.9f, 0.1f, 1.0f),   
                    NomadUI::NUIColor(0.1f, 0.9f, 0.6f, 1.0f)    
                };

                if (spacePos != std::string::npos) {
                    size_t numberPos = trackName.find_last_not_of("0123456789");
                    if (numberPos != std::string::npos && numberPos < trackName.length() - 1) {
                         std::string numberStr = trackName.substr(numberPos + 1);
                         try {
                             uint32_t trackNumber = std::stoul(numberStr);
                             size_t colorIndex = (trackNumber - 1) % brightColors.size();
                             clipColor = brightColors[colorIndex];
                             foundBrightColor = true;
                         } catch (...) {}
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
                    clipColor = NomadUI::NUIColor((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, (c >> 24) & 0xFF) / 255.0f;
                }
            } else {
                uint32_t color = clip.colorRGBA;
                clipColor = NomadUI::NUIColor(
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
    
    // Draw semi-transparent filled background (ROUNDED)
    NomadUI::NUIColor bgColor = clipColor.withAlpha(0.15f);
    renderer.fillRoundedRect(clipBounds, clipRadius, bgColor);
    
    // Draw border (ghost instances get a dashed-style different border)
    bool clipSelected = (clip.id == m_activeClipId);
    bool isGhostInstance = (patternRefCount > 1 && patternInstanceIndex > 1);
    
    NomadUI::NUIColor borderColor = clipSelected ? NomadUI::NUIColor::white() : clipColor.withAlpha(0.6f);
    if (isGhostInstance) {
        borderColor = clipColor.withAlpha(0.4f);  // Dimmer border for ghost instances
    }
    float borderWidth = clipSelected ? 2.0f : 1.0f;
    
    renderer.strokeRoundedRect(clipBounds, clipRadius, borderWidth, borderColor);
    
    // Draw name strip at top
    float nameStripHeight = 16.0f;
    if (clipBounds.height > nameStripHeight + 5) {
        NomadUI::NUIRect nameStripBounds(clipBounds.x, clipBounds.y, clipBounds.width, nameStripHeight);
        
        // Ghost instances get a slightly different name strip color
        NomadUI::NUIColor stripColor = isGhostInstance ? clipColor.withAlpha(0.65f) : clipColor.withAlpha(0.85f);
        renderer.fillRect(nameStripBounds, stripColor);
        
        if (fullClipBounds.x >= clipBounds.x) {
            // Draw name
            renderer.drawText(sampleName, NomadUI::NUIPoint(fullClipBounds.x + 4.0f, clipBounds.y + 2.0f), 
                             11.0f, NomadUI::NUIColor::white());
            
            // Draw pattern instance indicator (e.g., "1/3") if pattern has multiple instances
            if (patternRefCount > 1) {
                std::string instanceText = std::to_string(patternInstanceIndex) + "/" + std::to_string(patternRefCount);
                float textWidth = instanceText.length() * 6.0f;  // Approximate width
                float indicatorX = fullClipBounds.x + fullClipBounds.width - textWidth - 4.0f;
                
                // Only draw if there's room
                if (indicatorX > fullClipBounds.x + 40.0f) {
                    renderer.drawText(instanceText, NomadUI::NUIPoint(indicatorX, clipBounds.y + 2.0f), 
                                     10.0f, NomadUI::NUIColor(1.0f, 1.0f, 1.0f, 0.7f));
                }
                
                // Draw small ghost icon (link symbol) next to name
                float iconX = fullClipBounds.x + 4.0f + sampleName.length() * 6.0f + 4.0f;
                if (iconX < indicatorX - 20.0f) {
                    // Use static icon to avoid reloading SVG every frame
                    static std::shared_ptr<NomadUI::NUIIcon> linkIcon;
                    if (!linkIcon) {
                        const char* linkSvg = R"(
                            <svg viewBox="0 0 24 24" fill="currentColor">
                                <path d="M3.9 12c0-1.71 1.39-3.1 3.1-3.1h4V7H7c-2.76 0-5 2.24-5 5s2.24 5 5 5h4v-1.9H7c-1.71 0-3.1-1.39-3.1-3.1zM8 13h8v-2H8v2zm9-6h-4v1.9h4c1.71 0 3.1 1.39 3.1 3.1s-1.39 3.1-3.1 3.1h-4V17h4c2.76 0 5-2.24 5-5s-2.24-5-5-5z"/>
                            </svg>
                        )";
                        linkIcon = std::make_shared<NomadUI::NUIIcon>(linkSvg);
                    }
                    
                    float linkSize = 12.0f;
                    linkIcon->setColor(NomadUI::NUIColor(1.0f, 1.0f, 1.0f, 0.6f));
                    linkIcon->setBounds(NomadUI::NUIRect(iconX, clipBounds.y + 1.0f, linkSize, linkSize));
                    linkIcon->onRender(renderer);
                }
            }
        }
    }
}

void TrackUIComponent::drawSampleClip(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& clipBounds) {
    // Legacy stub - do nothing or implement if needed for backward compatibility
}


// Helper to draw a clip at its calculated position (for multi-clip lane support)
void TrackUIComponent::drawClipAtPosition(NomadUI::NUIRenderer& renderer, const ClipInstance& clip,
                                          const NomadUI::NUIRect& bounds, float controlAreaWidth) {
    
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
                const NomadUI::NUIRect fullClipBounds(
                    waveformStartX,
                    bounds.y + 2,
                    waveformWidthInPixels,
                    bounds.height - 4
                );

                // Store FULL clip bounds for hit testing
                m_allClipBounds[clip.id] = fullClipBounds;

                NomadUI::NUIRect clippedClipBounds(
                    clipStartX,
                    bounds.y + 2,
                    clipWidth,
                    bounds.height - 4
                );
                // Check if this is a pattern clip or sample clip
                bool isPattern = false;
                if (m_trackManager && clip.patternId.isValid()) {
                    auto pattern = m_trackManager->getPatternManager().getPattern(clip.patternId);
                    if (pattern && pattern->isMidi()) {
                        isPattern = true;
                    }
                }

                if (isPattern) {
                    drawPatternClipForClip(renderer, clippedClipBounds, fullClipBounds, clip);
                } else {
                    drawSampleClipForClip(renderer, clippedClipBounds, fullClipBounds, clip);
                    
                    // Draw waveform INSIDE the clip
                    float nameStripHeight = 16.0f;
                    float waveformPadding = 4.0f;  // Increased from 2 for more breathing room
                    float cornerPadding = 6.0f;    // Increased from 4 for bottom/side clearance
                    NomadUI::NUIRect waveformInsideClip(
                        visibleStartX + cornerPadding,  // Add left padding for rounded corners
                        bounds.y + 2 + nameStripHeight + waveformPadding,
                        visibleWidth - cornerPadding * 2,  // Shrink width for both corners
                        bounds.height - 4 - nameStripHeight - waveformPadding - cornerPadding  // Extra bottom padding
                    );
                    drawWaveformForClip(renderer, waveformInsideClip, clip, offsetRatio, visibleRatio);
                }
            }
        }
    }
}

void TrackUIComponent::drawPatternClipForClip(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& clipBounds,
                                              const NomadUI::NUIRect& fullClipBounds, const ClipInstance& clip) {
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    
    // 1. Draw Background (Pattern Color)
    NomadUI::NUIColor baseColor = NomadUI::NUIColor::fromHex(clip.colorRGBA);
    
    // Selection state check
    bool isSelected = false;
    // TODO: Connect to SelectionModel (likely via TrackManagerUI or similar)
    // if (m_trackManager) {
    //     isSelected = m_trackManager->getSelectionModel().isClipSelected(clip.id);
    // }
    
    // Mute state
    if (clip.muted) {
        baseColor = baseColor.withAlpha(0.4f);
    }
    
    // Background fill
    renderer.fillRect(clipBounds, baseColor.withAlpha(0.3f)); // Semi-transparent body
    renderer.strokeRect(clipBounds, 1.0f, isSelected ? themeManager.getColor("accentCyan") : baseColor);
    
    // 2. Draw Name Strip
    float nameStripHeight = 16.0f;
    NomadUI::NUIRect nameStripBounds = clipBounds;
    nameStripBounds.height = nameStripHeight;
    renderer.fillRect(nameStripBounds, baseColor); // Solid header
    
    // Clip Name
    std::string clipName = clip.name;
    if (clipName.empty()) {
        if (m_trackManager) {
            auto pattern = m_trackManager->getPatternManager().getPattern(clip.patternId);
            if (pattern) clipName = pattern->name;
        }
    }

    renderer.drawText(clipName, NomadUI::NUIPoint(clipBounds.x + 4.0f, clipBounds.y + 2.0f), 
                      10.0f, themeManager.getColor("textPrimary"));
                      
    // 3. Draw MIDI Notes
    if (m_trackManager && clip.patternId.isValid()) {
        auto pattern = m_trackManager->getPatternManager().getPattern(clip.patternId);
        if (pattern && pattern->isMidi()) {
            const auto& midiPayload = std::get<MidiPayload>(pattern->payload);
            
            // Define drawing area for notes (below header)
            float noteAreaY = fullClipBounds.y + nameStripHeight;
            float noteAreaHeight = fullClipBounds.height - nameStripHeight;
            
            // Pitch range (zoom logic could improve this, but for now 0-127 is fine or C3-C6)
            // Let's autosize to used range or default to C2-C6 (36-84)
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
            int pitchRange = std::max(12, maxPitch - minPitch); // Ensure at least an octave
            
            NomadUI::NUIColor noteColor = baseColor.lightened(0.2f).withAlpha(0.8f);
            
            for (const auto& n : midiPayload.notes) {
                // Calculate note geometry relative to FULL clip
                float noteStartX = fullClipBounds.x + (n.startBeat / pattern->lengthBeats) * fullClipBounds.width;
                float noteWidth = (n.durationBeats / pattern->lengthBeats) * fullClipBounds.width;
                
                // Vertical position (inverted, higher pitch = higher Y, but screen Y is down)
                // We want high pitch at top (low Y)
                float normalizedPitch = (float)(n.pitch - minPitch) / pitchRange;
                float noteY = noteAreaY + noteAreaHeight * (1.0f - normalizedPitch) - (noteAreaHeight / pitchRange);
                float noteHeight = (noteAreaHeight / pitchRange) - 1.0f; // 1px gap
                
                NomadUI::NUIRect noteRect(noteStartX, noteY, std::max(1.0f, noteWidth), std::max(1.0f, noteHeight));
                
                // Optimization: Draw only if intersects visible clipped bounds
                if (noteRect.x + noteRect.width > clipBounds.x && noteRect.x < clipBounds.x + clipBounds.width) {
                    // Clip the drawing strictly
                    if (noteRect.x < clipBounds.x) {
                        noteRect.width -= (clipBounds.x - noteRect.x);
                        noteRect.x = clipBounds.x;
                    }
                    if (noteRect.x + noteRect.width > clipBounds.x + clipBounds.width) {
                        noteRect.width = (clipBounds.x + clipBounds.width) - noteRect.x;
                    }
                    
                    renderer.fillRect(noteRect, noteColor);
                }
            }
        }
    }
}


// UI Rendering
void TrackUIComponent::onRender(NomadUI::NUIRenderer& renderer) {
    NomadUI::NUIRect bounds = getBounds();
    
    // Clear clip bounds map - will be repopulated during drawClipAtPosition
    // m_allClipBounds.clear(); // Handled in render logic now
    if (!m_trackManager) return;
    auto& playlist = m_trackManager->getPlaylistModel();
    auto lane = playlist.getLane(m_laneId);
    if (!lane) return;

    // Get theme colors and layout
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    
    // Zebra Striping Logic
    NomadUI::NUIColor trackBgColor = NomadUI::NUIColor::transparent();
    if (m_rowIndex % 2 == 0) {
        // Even rows: slight overlay for separation (5% white opacity)
        trackBgColor = NomadUI::NUIColor(1.0f, 1.0f, 1.0f, 0.03f);
    }
    
    // Selection Highlight
    // Selection Highlight overrides zebra striping for now
    if (isSelected()) {
         NomadUI::NUIColor selectedColor = themeManager.getColor("primary").withAlpha(0.12f);
         trackBgColor = selectedColor; 
    }
    
    // Apply background
    renderer.fillRect(bounds, trackBgColor); 
    NomadUI::NUIColor borderColor = themeManager.getColor("border");
    
    float controlAreaWidth = std::min(layout.trackControlsWidth, bounds.width);
    
    if (m_isPrimaryForLane) {
        NomadUI::NUIRect controlBounds(bounds.x, bounds.y, controlAreaWidth, bounds.height);
        
        // Control Area Polish: Darker Glassy Background
        NomadUI::NUIColor baseControlColor = themeManager.getColor("backgroundSecondary"); 
        
        // Dynamic highlight logic (Selection, Solo, Mute)
        if (m_channel) {
             if (m_selected) {
                 // Selected: Primary Tint
                 baseControlColor = themeManager.getColor("accentPrimary").withAlpha(0.12f);
             } else if (m_channel->isSoloed()) {
                 // Soloed: Subtle Cyan Tint
                 baseControlColor = themeManager.getColor("accentCyan").withAlpha(0.08f);
             } else if (m_channel->isMuted()) {
                 // Muted: Darkened
                 baseControlColor = baseControlColor.darkened(0.2f);
             }
        }
        
        // Render Control Area Background
        renderer.fillRect(controlBounds, baseControlColor);
        
        // Separator Line (Glass Border) between Controls and Timeline
        renderer.drawLine(
            NomadUI::NUIPoint(controlBounds.right(), controlBounds.y),
            NomadUI::NUIPoint(controlBounds.right(), controlBounds.bottom()),
            1.0f,
            themeManager.getColor("glassBorder")
        );
        
        // Top Separator (White)
        renderer.drawLine(
            NomadUI::NUIPoint(bounds.x, bounds.y),
            NomadUI::NUIPoint(bounds.right(), bounds.y),
            1.0f,
            NomadUI::NUIColor::white().withAlpha(0.1f)
        );

        // Bottom Separator (White)
        renderer.drawLine(
            NomadUI::NUIPoint(bounds.x, bounds.bottom() - 1),
            NomadUI::NUIPoint(bounds.right(), bounds.bottom() - 1),
            1.0f,
            NomadUI::NUIColor::white().withAlpha(0.1f)
        );

        // Inline Volume Meter (Behind Name)
        if (m_channel && !m_channel->isMuted()) {
            // Get meter level 
            // Simulated meter level (replace with actual meter from bus when available)
            static auto startTime = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            float time = std::chrono::duration<float>(now - startTime).count();
            
            float level = (sin(time * 5.0f + m_channel->getChannelId()) + 1.0f) * 0.5f * m_channel->getVolume();
            if (level > 0.001f) {
                // Clamp and map level
                level = std::min(1.0f, std::max(0.0f, level));
                // Non-linear mapping for better visual
                float visualLevel = std::pow(level, 0.5f); 
                
                // Define meter location (Behind name label approx area)
                // Name label usually starts around x=20, width=150
                float meterX = bounds.x + 20.0f; 
                float meterY = bounds.y + 10.0f; // Align with top part of track
                float meterW = 140.0f * visualLevel;
                float meterH = 28.0f; // Height of name label area
                
                NomadUI::NUIRect meterRect(meterX, meterY, meterW, meterH);
                
                // Meter Color Gradient (Green -> Yellow -> Red logic simulated)
                NomadUI::NUIColor meterColor = themeManager.getColor("success").withAlpha(0.15f);
                if (visualLevel > 0.8f) meterColor = themeManager.getColor("error").withAlpha(0.2f);
                else if (visualLevel > 0.5f) meterColor = themeManager.getColor("warning").withAlpha(0.2f);
                
                renderer.fillRoundedRect(meterRect, 4.0f, meterColor);
            }
        }

        // Lane color strip (identity)
        uint32_t argb = lane->colorRGBA;
        float a = ((argb >> 24) & 0xFF) / 255.0f;
        float r = ((argb >> 16) & 0xFF) / 255.0f;
        float g = ((argb >> 8) & 0xFF) / 255.0f;
        float b = (argb & 0xFF) / 255.0f;
        NomadUI::NUIColor stripColor(r, g, b, a > 0.0f ? a : 1.0f);
        const float stripWidth = 4.0f; // Slightly slimmer strip
        renderer.fillRect(NomadUI::NUIRect(bounds.x, bounds.y, stripWidth, bounds.height), stripColor);
        
        // Panel Separator (Right side of control area)
        renderer.drawLine(
            NomadUI::NUIPoint(bounds.x + controlAreaWidth, bounds.y),
            NomadUI::NUIPoint(bounds.x + controlAreaWidth, bounds.y + bounds.height),
            1.0f,
            borderColor.withAlpha(0.3f)
        );
        // Add subtle shadow to the separator for depth
        renderer.drawShadow(NomadUI::NUIRect(bounds.x + controlAreaWidth - 1, bounds.y, 2, bounds.height), 
                            0.0f, 0.0f, 4.0f, themeManager.getColor("shadow"));


        float separatorY = bounds.y + bounds.height - 1.0f; // Draw inside bounds
        renderer.drawLine( // Bottom separator
            NomadUI::NUIPoint(bounds.x, separatorY),
            NomadUI::NUIPoint(bounds.x + bounds.width, separatorY),
            2.0f,
            NomadUI::NUIColor(0.0f, 0.0f, 0.0f, 1.0f)
        );
        
        drawPlaylistGrid(renderer, bounds);
    }

    // Optimization: Cache grid and clips rendering
    // If the bounds changed or cache is invalid, rebuild the texture.
    uint64_t currentModId = playlist.getModificationCounter();
    if (bounds.width != m_lastRenderBounds.width || bounds.height != m_lastRenderBounds.height || currentModId != m_lastModelModId) {
        invalidateCache();
    }
    m_lastRenderBounds = bounds;
    m_lastModelModId = currentModId;

    // We only cache the playlist area (everything right of control area)
    // Actually, drawClipAtPosition relies on full bounds logic.
    // For simplicity, we cache the ENTIRE track content if possible, or just the clips layer.
    
    // PERMANENTLY DISABLED: Unresolved FBO/Scissor issues on user hardware.
    // Performance relies on smart repaints, not texture caching.
    bool canCache = false; 
    
    if (canCache) {
        if (!m_backgroundValid) {
            // Setup cache
            if (m_backgroundTexture == 0) {
                // Initial create or resize? renderToTextureBegin handles creation usually or we need createTexture?
                // Assuming renderToTextureBegin(width, height) returns a valid ID or reuses.
                // If implicit management, we store the ID.
                m_backgroundTexture = renderer.renderToTextureBegin(static_cast<int>(bounds.width), static_cast<int>(bounds.height));
            } else {
                 // Re-use logic or delete/recreate?
                 // For now assume renderToTextureBegin handles it.
                 uint32_t newTex = renderer.renderToTextureBegin(static_cast<int>(bounds.width), static_cast<int>(bounds.height));
                 if (newTex != m_backgroundTexture) {
                     if (m_backgroundTexture != 0) renderer.deleteTexture(m_backgroundTexture);
                     m_backgroundTexture = newTex;
                 }
            }

            if (m_backgroundTexture != 0) {
                // Clear texture with transparent or background?
                // We draw relative to (0,0) in the texture.
                // So we need to offset drawing calls? 
                // Or simply push a transform: -bounds.x, -bounds.y
                renderer.pushTransform(-bounds.x, -bounds.y);
                
                // Clear hit buffer?
                m_allClipBounds.clear();

                float clipOpacity = (m_playlistMode == PlaylistMode::Automation) ? 0.3f : 1.0f;
                renderer.setOpacity(clipOpacity);
                for (const auto& clip : lane->clips) {
                    drawClipAtPosition(renderer, clip, bounds, controlAreaWidth);
                }
                renderer.setOpacity(1.0f);
                
                renderer.popTransform();
                renderer.renderToTextureEnd();
                m_backgroundValid = true;
            } else {
                // Fallback if texture creation failed
                canCache = false;
            }
        }
        
        if (canCache && m_backgroundValid) {
            renderer.drawTexture(m_backgroundTexture, bounds, NomadUI::NUIRect(0, 0, bounds.width, bounds.height));
        }
    }
    
    if (!canCache) {
        // Fallback: Immediate mode
        m_allClipBounds.clear();
        float clipOpacity = (m_playlistMode == PlaylistMode::Automation) ? 0.3f : 1.0f;
        renderer.setOpacity(clipOpacity);
        for (const auto& clip : lane->clips) {
            drawClipAtPosition(renderer, clip, bounds, controlAreaWidth);
        }
        renderer.setOpacity(1.0f);
    }

    // Draw Automation Layer (v3.1)
    if (m_playlistMode == PlaylistMode::Automation) {
        renderAutomationLayer(renderer, bounds, bounds.x + controlAreaWidth);
    }

    // Apply overlay for muted/solo state
    if (m_isPrimaryForLane) {
        bool anySoloed = false;
        if (m_trackManager) {
            for (size_t i = 0; i < m_trackManager->getChannelCount(); ++i) {
                if (m_trackManager->getChannel(i)->isSoloed()) {
                    anySoloed = true;
                    break;
                }
            }
        }

        const bool soloSuppressed = anySoloed && m_channel && !m_channel->isSoloed();

        // Overlay area matches the grid area (right of controls)
        NomadUI::NUIRect gridArea(
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
            renderer.fillRect(gridArea, NomadUI::NUIColor(0.0f, 0.0f, 0.0f, dimAlpha));
        }
    }

    if (m_isPrimaryForLane) {
        renderChildren(renderer);
    }
}


void TrackUIComponent::renderControlOverlay(NomadUI::NUIRenderer& renderer) {
    if (!m_isPrimaryForLane) return;
    
    const NomadUI::NUIRect bounds = getBounds();
    if (bounds.isEmpty()) return;

    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    const float controlAreaWidth = std::min(layout.trackControlsWidth, bounds.width);
    const NomadUI::NUIRect controlAreaBounds(bounds.x, bounds.y, controlAreaWidth, bounds.height);

    // Initial fill to clear potential artifacts
    renderer.fillRect(controlAreaBounds, themeManager.getColor("backgroundSecondary"));

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
            renderer.fillRect(controlAreaBounds, themeManager.getColor("accentCyan").withAlpha(0.12f));
        } else if (m_channel->isMuted()) {
            renderer.fillRect(controlAreaBounds, NomadUI::NUIColor(0,0,0,0.35f));
        } else if (soloSuppressed) {
            renderer.fillRect(controlAreaBounds, NomadUI::NUIColor(0,0,0,0.25f));
        }

        // SELECTION OVERLAY
        if (m_selected) {
            renderer.fillRect(controlAreaBounds, themeManager.getColor("accentPrimary").withAlpha(0.15f));
        }
    }

    // Track color strip (identity)
    if (m_channel) {
        NomadUI::NUIColor stripColor;
        
        // Exact same logic as updateTrackNameColors to ensure match
        std::string trackName = m_channel->getName();
        size_t spacePos = trackName.find(' ');
        bool foundBrightColor = false;

        if (spacePos != std::string::npos) {
            static const std::vector<NomadUI::NUIColor> brightColors = {
                NomadUI::NUIColor(1.0f, 0.8f, 0.2f, 1.0f),   // Bright yellow/gold
                NomadUI::NUIColor(0.2f, 1.0f, 0.8f, 1.0f),   // Bright cyan
                NomadUI::NUIColor(1.0f, 0.4f, 0.8f, 1.0f),   // Bright pink/magenta
                NomadUI::NUIColor(0.6f, 1.0f, 0.2f, 1.0f),   // Bright lime
                NomadUI::NUIColor(1.0f, 0.6f, 0.2f, 1.0f),   // Bright orange
                NomadUI::NUIColor(0.4f, 0.8f, 1.0f, 1.0f),   // Bright blue
                NomadUI::NUIColor(1.0f, 0.2f, 0.4f, 1.0f),   // Bright red
                NomadUI::NUIColor(0.8f, 0.4f, 1.0f, 1.0f),   // Bright purple
                NomadUI::NUIColor(1.0f, 0.9f, 0.1f, 1.0f),   // Bright yellow
                NomadUI::NUIColor(0.1f, 0.9f, 0.6f, 1.0f)    // Bright teal
            };

            size_t numberPos = trackName.find_last_not_of("0123456789");
            if (numberPos != std::string::npos && numberPos < trackName.length() - 1) {
                std::string numberStr = trackName.substr(numberPos + 1);
                try {
                    uint32_t trackNumber = std::stoul(numberStr);
                    size_t colorIndex = (trackNumber - 1) % brightColors.size();
                    stripColor = brightColors[colorIndex];
                    foundBrightColor = true;
                } catch (...) {}
            }
            
            if (!foundBrightColor) {
               // Fallback ID based
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
            stripColor = NomadUI::NUIColor(r, g, b, a > 0.0f ? a : 1.0f);
        }
        
        const float stripWidth = 4.0f;
        
        // Draw strip
        renderer.fillRect(NomadUI::NUIRect(bounds.x, bounds.y, stripWidth, bounds.height), stripColor);
        
        // Selection Highlight Line (Inner Glow)
        if (m_selected) {
            auto glowColor = themeManager.getColor("highlightGlow");
            // Top highlight line inside control area (skipping strip)
            renderer.fillRect(NomadUI::NUIRect(bounds.x + stripWidth, bounds.y, controlAreaWidth - stripWidth, 1.0f), glowColor);
            // Bottom highlight
            renderer.fillRect(NomadUI::NUIRect(bounds.x + stripWidth, bounds.y + bounds.height - 1.0f, controlAreaWidth - stripWidth, 1.0f), glowColor.withAlpha(0.5f));
        }
    }

    // Explicit Separators for Control Area (ensures they are on top of background)
    // Top
    renderer.drawLine(
        NomadUI::NUIPoint(bounds.x, bounds.y),
        NomadUI::NUIPoint(bounds.x + controlAreaWidth, bounds.y),
        1.0f,
        NomadUI::NUIColor::white().withAlpha(0.1f)
    );
    // Bottom
    renderer.drawLine(
        NomadUI::NUIPoint(bounds.x, bounds.bottom() - 1),
        NomadUI::NUIPoint(bounds.x + controlAreaWidth, bounds.bottom() - 1),
        1.0f,
        NomadUI::NUIColor::white().withAlpha(0.1f)
    );


    // Draw vertical separator between control area and playlist area
    renderer.drawLine(
        NomadUI::NUIPoint(bounds.x + controlAreaWidth, bounds.y),
        NomadUI::NUIPoint(bounds.x + controlAreaWidth, bounds.y + bounds.height),
        1.0f,
        themeManager.getColor("glassBorder")
    );

    // Render control components (track name + M/S/R)
    renderChildren(renderer);
}

// Draw playlist grid (beat/bar grid)
void TrackUIComponent::drawPlaylistGrid(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& bounds) {
    // Get layout dimensions from theme
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
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
    
    // 1. ZEBRA STRIPING (Per Bar)
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
                     NomadUI::NUIRect(rectX, bounds.y, rectW, bounds.height), 
                     NomadUI::NUIColor(1.0f, 1.0f, 1.0f, 0.03f)
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
            NomadUI::NUIColor loopFillColor = themeManager.getColor("accentCyan").withAlpha(0.08f);
            renderer.fillRect(
                NomadUI::NUIRect(drawStartX, bounds.y, loopWidth, bounds.height),
                loopFillColor
            );
            
            // Draw loop boundary markers (vertical lines at start and end)
            NomadUI::NUIColor loopMarkerColor = themeManager.getColor("accentCyan").withAlpha(0.6f);
            
            // Start marker (if visible)
            if (loopStartX >= gridStartX && loopStartX <= gridEndX) {
                renderer.drawLine(
                    NomadUI::NUIPoint(loopStartX, bounds.y),
                    NomadUI::NUIPoint(loopStartX, bounds.y + bounds.height),
                    2.0f,
                    loopMarkerColor
                );
            }
            
            // End marker (if visible)
            if (loopEndX >= gridStartX && loopEndX <= gridEndX) {
                renderer.drawLine(
                    NomadUI::NUIPoint(loopEndX, bounds.y),
                    NomadUI::NUIPoint(loopEndX, bounds.y + bounds.height),
                    2.0f,
                    loopMarkerColor
                );
            }
        }
    }
    */

    // 2. DYNAMIC SNAP GRID LINES
    double snapDur = NomadUI::MusicTheory::getSnapDuration(m_snapSetting);
    if (m_snapSetting == NomadUI::SnapGrid::None) snapDur = 1.0;
    else if (snapDur <= 0.0001) snapDur = 1.0;

    // Adjust density (Relaxed to 5px to ensure 1/16th notes are visible)
    while ((m_pixelsPerBeat * snapDur) < 5.0f) snapDur *= 2.0;

    double startBeat = m_timelineScrollOffset / m_pixelsPerBeat;
    double endBeat = startBeat + (gridWidth / m_pixelsPerBeat);
    double current = std::floor(startBeat / snapDur) * snapDur;

    // Grid Lines - Using Theme Tokens
    NomadUI::NUIColor barLineColor = themeManager.getColor("gridBar");
    NomadUI::NUIColor beatLineColor = themeManager.getColor("gridBeat");
    NomadUI::NUIColor subBeatLineColor = themeManager.getColor("gridSubdivision");

    for (; current <= endBeat + snapDur; current += snapDur) {
        // Double precision relative subtraction to avoid float jitter on large offsets
        double relX = (current * m_pixelsPerBeat) - static_cast<double>(m_timelineScrollOffset);
        float x = gridStartX + static_cast<float>(relX);
        
        // Strict manual culling
        if (x < gridStartX || x > gridEndX) continue;
        
        bool isBar = (std::fmod(std::abs(current), (double)m_beatsPerBar) < 0.001);
        bool isBeat = (std::fmod(std::abs(current), 1.0) < 0.001);
        
        if (isBar) {
             renderer.drawLine(NomadUI::NUIPoint(x, bounds.y), NomadUI::NUIPoint(x, bounds.y + bounds.height), 1.0f, barLineColor);
        } else if (isBeat) {
             renderer.drawLine(NomadUI::NUIPoint(x, bounds.y), NomadUI::NUIPoint(x, bounds.y + bounds.height), 1.0f, beatLineColor);
        } else {
             // Subdivision: Standard subtle line
             renderer.drawLine(NomadUI::NUIPoint(x, bounds.y), NomadUI::NUIPoint(x, bounds.y + bounds.height), 1.0f, subBeatLineColor);
        }
    }
}

// UI Rendering (kept from previous code)
// ...

void TrackUIComponent::onMouseEnter() {
    // Ensure parent knows we need updates (if any)
    NUIComponent::onMouseEnter();
    // Force cache invalidation immediately on enter
    if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback();
}

void TrackUIComponent::onMouseLeave() {
    NUIComponent::onMouseLeave();
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
    NomadUI::NUIRect bounds = getBounds();
    
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    const float controlAreaWidth = std::min(layout.trackControlsWidth, bounds.width);

    // Buttons cluster (horizontal, right-aligned within the control area)
    const float buttonW = layout.controlButtonWidth;
    const float buttonH = layout.controlButtonHeight;
    const float spacing = layout.controlButtonSpacing;
    const int numButtons = (m_recordButton ? 3 : 2);
    const float buttonsTotalW = numButtons * buttonW + (numButtons - 1) * spacing;
    
    // Position relative to component origin, then add absolute offset
    const float localButtonsXStart = controlAreaWidth - buttonsTotalW - layout.panelMargin;
    const float localButtonsY = (bounds.height - buttonH) * 0.5f;

    // Labels occupy the remaining left side of the control area
    const float localLabelLeft = layout.panelMargin;
    const float localLabelRight = localButtonsXStart - layout.panelMargin;
    const float localLabelWidth = std::max(40.0f, localLabelRight - localLabelLeft);
    const float localCenterY = (bounds.height - layout.trackLabelHeight) * 0.5f;

    // Name label - use NUIAbsolute for global coordinate system
    if (m_nameLabel) {
        m_nameLabel->setBounds(NomadUI::NUIRect(bounds.x + localLabelLeft, bounds.y + localCenterY, localLabelWidth, layout.trackLabelHeight));
    }

    float xCursor = localButtonsXStart;
    if (m_muteButton) {
        m_muteButton->setBounds(NomadUI::NUIRect(bounds.x + xCursor, bounds.y + localButtonsY, buttonW, buttonH));
        xCursor += buttonW + spacing;
    }
    if (m_soloButton) {
        m_soloButton->setBounds(NomadUI::NUIRect(bounds.x + xCursor, bounds.y + localButtonsY, buttonW, buttonH));
        xCursor += buttonW + spacing;
    }
    if (m_recordButton) {
        m_recordButton->setBounds(NomadUI::NUIRect(bounds.x + xCursor, bounds.y + localButtonsY, buttonW, buttonH));
    }

    NomadUI::NUIComponent::onResize(width, height);
}

bool TrackUIComponent::onMouseEvent(const NomadUI::NUIMouseEvent& event) {
    // 1. Invalidate Cache on mouse movement in control area (for button hover feedback)
    // NomadUI doesn't use event.type enum, so we infer from context.
    // Any mouse event in the control area checks for invalidation.
    if (m_onCacheInvalidationCallback) {
        // Check if we are over the control area
        auto& themeManager = NomadUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float controlWidth = layout.trackControlsWidth;
        
        if (event.position.x <= getBounds().x + controlWidth) {
             m_onCacheInvalidationCallback();
        }
    }

    NomadUI::NUIRect bounds = getBounds();
    
    // Early exit: If event is outside our bounds and we're not in an active operation, don't handle it
    bool isInsideBounds = bounds.contains(event.position);
    bool isActiveOperation = m_isTrimming || m_isDraggingClip || m_clipDragPotential || m_isDraggingPoint;
    bool isControlCapture = (m_muteButton && m_muteButton->isPressed()) ||
                            (m_soloButton && m_soloButton->isPressed()) ||
                            (m_recordButton && m_recordButton->isPressed());
    bool controlsNeedEvents = isControlCapture ||
                              (m_muteButton && m_muteButton->isHovered()) ||
                              (m_soloButton && m_soloButton->isHovered()) ||
                              (m_recordButton && m_recordButton->isHovered());
    
    if (!isInsideBounds && !isActiveOperation && !controlsNeedEvents) {
        return false;  // Let parent/siblings handle it (e.g., scrollbar)
    }
    
    // Get theme to determine control area bounds
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    float controlAreaWidth = layout.trackControlsWidth;
    float controlAreaEndX = bounds.x + controlAreaWidth;
    float gridStartX = bounds.x + controlAreaWidth + 5;
    float gridEndX = bounds.x + bounds.width - 5;
    
    // Keep button hover/press state accurate even when leaving the track row.
    // This is important for cached UIs (and prevents stuck hover/press visuals).
    if (isInsideBounds || controlsNeedEvents) {
        auto routeControlButton = [&](const std::shared_ptr<NomadUI::NUIButton>& button) -> bool {
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
        handledByControls = routeControlButton(m_muteButton) || handledByControls;
        handledByControls = routeControlButton(m_soloButton) || handledByControls;
        handledByControls = routeControlButton(m_recordButton) || handledByControls;

        if (handledByControls) {
            // Clicking controls should also select the track (v3.1)
            if (event.pressed && event.button == NomadUI::NUIMouseButton::Left && m_onTrackSelectedCallback) {
                bool shift = (event.modifiers & NomadUI::NUIModifiers::Shift);
                m_onTrackSelectedCallback(this, shift);
            }
            return true;
        }
    }

    // PRIORITY 3: Automation Layer (v3.1)
    // Handle Mouse Release for automation point dragging FIRST - before any bounds checks
    // This ensures release is processed even when mouse has moved far outside bounds
    if (m_playlistMode == PlaylistMode::Automation && m_isDraggingPoint) {
        if (event.released && event.button == NomadUI::NUIMouseButton::Left) {
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
                if (event.pressed && event.button == NomadUI::NUIMouseButton::Right) {
                    auto& points = curve.getPoints();
                    for (int i = 0; i < (int)points.size(); ++i) {
                        float px = gridStartX + (static_cast<float>(points[i].beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
                        float py = bounds.y + (1.0f - static_cast<float>(points[i].value)) * bounds.height;
                        
                        if (NomadUI::distance({px, py}, event.position) < 12.0f) {
                            curve.removePoint(i);
                            setDirty(true);
                            repaint();
                            if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback();
                            return true;
                        }
                    }
                }

                // Left Click -> Select/Add Point
                if (event.pressed && event.button == NomadUI::NUIMouseButton::Left && isInsideBounds) {
                    int hitIndex = -1;
                    auto& points = curve.getPoints();
                    for (int i = 0; i < (int)points.size(); ++i) {
                        float px = gridStartX + (static_cast<float>(points[i].beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
                        float py = bounds.y + (1.0f - static_cast<float>(points[i].value)) * bounds.height;
                        
                        if (NomadUI::distance({px, py}, event.position) < 12.0f) {
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
                        curve.addPoint(beat, value, 0.5f);
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
            if (event.pressed && event.button == NomadUI::NUIMouseButton::Left && isInsideBounds) {
                if (m_onTrackSelectedCallback) {
                    m_onTrackSelectedCallback(this, (event.modifiers & NomadUI::NUIModifiers::Shift));
                }
            }
            
            if (isInsideBounds) return true;
        }
    }
    
    auto& dragManager = NomadUI::NUIDragDropManager::getInstance();
    
    // Handle mouse release - always process to clear state
    if (!event.pressed && event.button == NomadUI::NUIMouseButton::Left) {
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
    if (m_isDraggingClip && !event.released && event.button == NomadUI::NUIMouseButton::Left) {
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
                            double oldStart = clip.startBeat;
                            double oldDuration = clip.durationBeats;
                            clip.startBeat = std::max(0.0, m_trimOriginalStart + deltaBeats);
                            double actualDelta = clip.startBeat - oldStart;
                            clip.durationBeats = std::max(0.1, oldDuration - actualDelta);
                        } else if (m_trimEdge == TrimEdge::Right) {
                            // Trim right: just change duration
                            clip.durationBeats = std::max(0.1, m_trimOriginalDuration + deltaBeats);
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
    if (event.pressed && event.button == NomadUI::NUIMouseButton::Left && isInsideBounds) {
        // Position relative to local component origin
        NomadUI::NUIPoint localPos(event.position.x - bounds.x, event.position.y - bounds.y);

        // Only process clip manipulation if click is in the grid area (not control area)
        if (localPos.x >= controlAreaWidth) {
            
            // Check if split tool is active
            bool isSplitToolActive = m_isSplitToolActiveCallback ? m_isSplitToolActiveCallback() : false;
            
            // === MULTI-CLIP HIT TESTING ===
            // Find which clip was clicked (check all clips in m_allClipBounds)
            ClipInstanceID clickedClipId = ClipInstanceID{};
            NomadUI::NUIRect clickedClipBounds;
            
            for (const auto& [clipId, clipBounds] : m_allClipBounds) {
                if (clipBounds.contains(event.position)) {
                    clickedClipId = clipId;
                    clickedClipBounds = clipBounds;
                    break;
                }
            }
            
            // Handle SPLIT TOOL - click on clip to split at that position
            if (isSplitToolActive && clickedClipId.isValid()) {
                // Find clip to get duration
                if (auto lane = m_trackManager->getPlaylistModel().getLane(m_laneId)) {
                    for (size_t i = 0; i < lane->clips.size(); ++i) {
                        const auto& clip = lane->clips[i];
                        if (clip.id == clickedClipId) {
                            double clickOffsetX = event.position.x - clickedClipBounds.x;
                            double splitRatio = clickOffsetX / clickedClipBounds.width;
                            double splitTimeBeats = splitRatio * clip.durationBeats;
                            
                            Log::info("Split requested at " + std::to_string(splitTimeBeats) + " beats");
                            
                            if (m_onSplitRequestedCallback) {
                                // In v3.0 split might need an updated callback signature, but for now we follow old pattern
                                m_onSplitRequestedCallback(this, splitTimeBeats);
                            }
                            break;
                        }
                    }
                }
                return true;
            }

            
            // Check if clicking on any clip for drag initiation or trimming
            if (clickedClipId.isValid()) {
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
                        bool shift = (event.modifiers & NomadUI::NUIModifiers::Shift);
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
                        bool shift = (event.modifiers & NomadUI::NUIModifiers::Shift);
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
                    bool shift = (event.modifiers & NomadUI::NUIModifiers::Shift);
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
                bool shift = (event.modifiers & NomadUI::NUIModifiers::Shift);
                m_onTrackSelectedCallback(this, shift);
            } else {
                m_selected = true;
            }
            return true;
        }
        
        // Click in control area (but not on a button) - just select the track
        if (event.position.x < controlAreaEndX) {
            if (m_onTrackSelectedCallback) {
                bool shift = (event.modifiers & NomadUI::NUIModifiers::Shift);
                m_onTrackSelectedCallback(this, shift);
            } else {
                m_selected = true;
            }
            return true;
        }
    }
    
    // Handle right-click to delete clip (FL Studio style) - check all clips
    if (event.pressed && event.button == NomadUI::NUIMouseButton::Right && isInsideBounds) {
        // Find which clip was right-clicked
        for (const auto& [clipId, clipBounds] : m_allClipBounds) {
            if (clipBounds.contains(event.position)) {
                if (m_onClipDeletedCallback) {
                    // Send deletion request to parent
                    m_onClipDeletedCallback(this, clipId, event.position);
                }

                return true;
            }
        }
    }

    // Pass through to parent if not handled
    return false;
}


void TrackUIComponent::renderAutomationLayer(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& bounds, float gridStartX) {
    auto& playlist = m_trackManager->getPlaylistModel();
    auto lane = playlist.getLane(m_laneId);
    if (!lane) return;

    auto& theme = NomadUI::NUIThemeManager::getInstance();
    
    // Automation Area bounds (exclude controls)
    NomadUI::NUIRect gridArea(gridStartX, bounds.y, bounds.width - (gridStartX - bounds.x), bounds.height);
    
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
            renderer.drawLine(NomadUI::NUIPoint(gridArea.x, y), 
                              NomadUI::NUIPoint(gridArea.right(), y), 
                              1.5f, theme.getColor("accentCyan").withAlpha(0.4f));
            continue;
        }

        NomadUI::NUIColor curveColor = theme.getColor("accentCyan");
        
        // Draw lines between points
        // Draw lines between points
        std::vector<NomadUI::NUIPoint> polyPoints;
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
                 double val = curve.getValueAtBeat(beat);
                 
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
                renderer.drawLine(NomadUI::NUIPoint(gridArea.x, fy), NomadUI::NUIPoint(fx, fy), 1.5f, curveColor.withAlpha(0.5f));
            }
            
            const auto& last = points.back();
            float lx = gridStartX + (static_cast<float>(last.beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
            float ly = gridArea.y + (1.0f - static_cast<float>(last.value)) * gridArea.height;
            if (lx < gridArea.right()) {
                renderer.drawLine(NomadUI::NUIPoint(lx, ly), NomadUI::NUIPoint(gridArea.right(), ly), 1.5f, curveColor.withAlpha(0.5f));
            }
        }

        // Draw point handles
        for (const auto& p : points) {
            float x = gridStartX + (static_cast<float>(p.beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
            float y = gridArea.y + (1.0f - static_cast<float>(p.value)) * gridArea.height;
            
            if (x < gridArea.x || x > gridArea.right()) continue;
            
            NomadUI::NUIColor ptColor = p.selected ? theme.getColor("primary") : curveColor;
            renderer.fillRect(NomadUI::NUIRect(x - 3, y - 3, 6, 6), ptColor);
            renderer.strokeRect(NomadUI::NUIRect(x - 4, y - 4, 8, 8), 1.0f, theme.getColor("border"));
        }
    }
}

} // namespace Audio
} // namespace Nomad
