// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "TrackUIComponent.h"
#include "../NomadAudio/include/TrackManager.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadCore/include/NomadLog.h"
#include <algorithm>
#include <cmath>

namespace Nomad {
namespace Audio {

TrackUIComponent::TrackUIComponent(std::shared_ptr<Track> track, TrackManager* trackManager)
    : m_track(track)
    , m_trackManager(trackManager)
{
    if (!m_track) {
        Log::error("TrackUIComponent created with null track");
        return;
    }

    // Create track name label
    m_nameLabel = std::make_shared<NomadUI::NUILabel>();
    m_nameLabel->setText(m_track->getName());
    {
        auto& themeManager = NomadUI::NUIThemeManager::getInstance();
        // Use large font for track names (now 14px after theme reduction)
        m_nameLabel->setFontSize(themeManager.getFontSize("l"));
    }
    updateTrackNameColors();
    addChild(m_nameLabel);
    
    // Create duration label (shows sample duration)
    m_durationLabel = std::make_shared<NomadUI::NUILabel>();
    m_durationLabel->setText("");
    {
        auto& themeManager = NomadUI::NUIThemeManager::getInstance();
        m_durationLabel->setFontSize(themeManager.getFontSize("m"));
        m_durationLabel->setTextColor(themeManager.getColor("textSecondary"));
    }
    addChild(m_durationLabel);


    // Create mute button
    m_muteButton = std::make_shared<NomadUI::NUIButton>();
    m_muteButton->setText("M");
    m_muteButton->setStyle(NomadUI::NUIButton::Style::Secondary); 
    m_muteButton->setToggleable(true);
    // Use theme-derived hover color (transparent/subtle)
    m_muteButton->setHoverColor(NomadUI::NUIThemeManager::getInstance().getColor("textSecondary").withAlpha(0.15f));
    // Active state: Amber/Orange (Ableton/FL style)
    m_muteButton->setPressedColor(NomadUI::NUIThemeManager::getInstance().getColor("accentAmber")); 
    m_muteButton->setTextColor(NomadUI::NUIColor::white());
    m_muteButton->setFontSize(NomadUI::NUIThemeManager::getInstance().getFontSize("m"));
    m_muteButton->setOnClick([this]() {
        onMuteToggled();
    });
    addChild(m_muteButton);

    // Create solo button
    m_soloButton = std::make_shared<NomadUI::NUIButton>();
    m_soloButton->setText("S");
    m_soloButton->setStyle(NomadUI::NUIButton::Style::Secondary);
    m_soloButton->setToggleable(true);
    m_soloButton->setHoverColor(NomadUI::NUIThemeManager::getInstance().getColor("textSecondary").withAlpha(0.15f));
    // Active state: Cyan/Blue
    m_soloButton->setPressedColor(NomadUI::NUIThemeManager::getInstance().getColor("accentCyan"));
    m_soloButton->setTextColor(NomadUI::NUIColor::white());
    m_soloButton->setFontSize(NomadUI::NUIThemeManager::getInstance().getFontSize("m"));
    m_soloButton->setOnClick([this]() {
        onSoloToggled();
    });
    addChild(m_soloButton);

    // Create record button
    m_recordButton = std::make_shared<NomadUI::NUIButton>();
    m_recordButton->setText("●");
    m_recordButton->setStyle(NomadUI::NUIButton::Style::Icon); 
    m_recordButton->setToggleable(true);
    m_recordButton->setHoverColor(NomadUI::NUIThemeManager::getInstance().getColor("textSecondary").withAlpha(0.15f));
    // Active state: Red
    m_recordButton->setPressedColor(NomadUI::NUIThemeManager::getInstance().getColor("error"));
    m_recordButton->setFontSize(NomadUI::NUIThemeManager::getInstance().getFontSize("m"));
    m_recordButton->setOnClick([this]() {
        onRecordToggled();
    });
    addChild(m_recordButton);

    updateUI();
}

TrackUIComponent::~TrackUIComponent() {
    Log::info("TrackUIComponent destroyed for track: " + (m_track ? m_track->getName() : "null"));
}

// UI Callbacks
void TrackUIComponent::onVolumeChanged(float volume) {
    if (m_track) {
        m_track->setVolume(volume);
        Log::info("Track " + m_track->getName() + " volume: " + std::to_string(volume));
    }
}

void TrackUIComponent::onPanChanged(float pan) {
    if (m_track) {
        m_track->setPan(pan);
        Log::info("Track " + m_track->getName() + " pan: " + std::to_string(pan));
    }
}

void TrackUIComponent::onMuteToggled() {
    if (m_track) {
        bool newMute = !m_track->isMuted();
        m_track->setMute(newMute);
        
        // If muting, auto-disable solo (mute takes priority)
        if (newMute && m_track->isSoloed()) {
            m_track->setSolo(false);
        }
        
        // Force immediate UI update
        updateUI();
        
        // Force repaint of the component
        repaint();
        
        Log::info("Track " + m_track->getName() + " mute: " + (newMute ? "ON" : "OFF"));
    }
}

void TrackUIComponent::onSoloToggled() {
    if (m_track) {
        bool newSolo = !m_track->isSoloed();
        
        // If enabling solo, notify parent to handle exclusive solo logic
        if (newSolo && m_onSoloToggledCallback) {
            m_onSoloToggledCallback(this);
        }
        
        m_track->setSolo(newSolo);
        
        // If soloing, auto-disable mute (solo takes priority)
        if (newSolo && m_track->isMuted()) {
            m_track->setMute(false);
        }
        
        // Force immediate UI update
        updateUI();
        
        // Force repaint of the component
        repaint();
        
        Log::info("Track " + m_track->getName() + " solo: " + (newSolo ? "ON" : "OFF"));
    }
}

void TrackUIComponent::onRecordToggled() {
    if (m_track) {
        TrackState state = m_track->getState();
        if (state == TrackState::Recording) {
            m_track->stopRecording();
        } else if (state == TrackState::Empty) {
            m_track->startRecording();
        }
        updateUI();
        Log::info("Track " + m_track->getName() + " record: " + (m_track->isRecording() ? "START" : "STOP"));
    }
}

void TrackUIComponent::updateUI() {
    if (!m_track) return;

    // Invalidate parent cache since button colors are changing
    if (m_onCacheInvalidationCallback) {
        m_onCacheInvalidationCallback();
    }

    // Update track name colors with bright colors based on number
    updateTrackNameColors();
    
    // Update duration label if sample is loaded
    if (m_durationLabel) {
        double duration = m_track->getDuration();
        if (duration > 0.0) {
            // Format as MM:SS.mmm
            int minutes = static_cast<int>(duration / 60.0);
            int seconds = static_cast<int>(duration) % 60;
            int milliseconds = static_cast<int>((duration - static_cast<int>(duration)) * 1000.0);
            
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%02d:%02d.%03d", minutes, seconds, milliseconds);
            m_durationLabel->setText(buffer);
        } else {
            m_durationLabel->setText("");
        }
    }


    if (m_muteButton) {
        auto& themeManager = NomadUI::NUIThemeManager::getInstance();
        // Use theme colors for Primary style
        m_muteButton->setBackgroundColor(themeManager.getColor("surfaceTertiary")); // #28282d - same as transport buttons

        // Active state (muted/unmuted) - no hover colors, just clear state indication
        m_muteButton->setToggled(m_track->isMuted());
        if (m_track->isMuted()) {
            m_muteButton->setTextColor(NomadUI::NUIColor::black()); // Black text when muted (on yellow)
        } else {
            m_muteButton->setTextColor(themeManager.getColor("textPrimary")); // White text when not muted
        }
    }

    if (m_soloButton) {
        auto& themeManager = NomadUI::NUIThemeManager::getInstance();
        // Use theme colors for Primary style
        m_soloButton->setBackgroundColor(themeManager.getColor("surfaceTertiary")); // #28282d - same as transport buttons

        // Active state (soloed/unsoloed) - no hover colors, just clear state indication
        m_soloButton->setToggled(m_track->isSoloed());
        if (m_track->isSoloed()) {
            m_soloButton->setTextColor(NomadUI::NUIColor::black()); // Black text when soloed (on cyan)
        } else {
            m_soloButton->setTextColor(themeManager.getColor("textPrimary")); // White text when not soloed
        }
    }

    if (m_recordButton) {
        auto& themeManager = NomadUI::NUIThemeManager::getInstance();
        // Use theme colors for Icon style
        m_recordButton->setBackgroundColor(themeManager.getColor("surfaceTertiary")); // #28282d - same as transport buttons
        TrackState state = m_track->getState();
        m_recordButton->setToggled(state == TrackState::Recording);
        if (state == TrackState::Recording) {
            m_recordButton->setTextColor(NomadUI::NUIColor::white()); // White text when recording (on red)
        } else {
            m_recordButton->setTextColor(themeManager.getColor("textPrimary")); // White text when not recording
        }
    }
}

void TrackUIComponent::updateTrackNameColors() {
    if (!m_nameLabel || !m_track) return;

    std::string trackName = m_track->getName();

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
        uint32_t trackId = m_track->getTrackId();
        size_t colorIndex = (trackId - 1) % brightColors.size();
        m_nameLabel->setTextColor(brightColors[colorIndex]);
    } else {
        // Fallback for non-standard track names
        uint32_t color = m_track->getColor();
        float r = ((color >> 16) & 0xFF) / 255.0f * 0.8f;
        float g = ((color >> 8) & 0xFF) / 255.0f * 0.8f;
        float b = (color & 0xFF) / 255.0f * 0.8f;
        float a = ((color >> 24) & 0xFF) / 255.0f;
        m_nameLabel->setTextColor(NomadUI::NUIColor(r, g, b, a));
    }
}

void TrackUIComponent::generateWaveformCache(int width, int height) {
    if (!m_track) return;
    
    const auto& audioData = m_track->getAudioData();
    if (audioData.empty()) {
        Log::warning("generateWaveformCache: Audio data is empty for track " + m_track->getName());
        return;
    }
    
    uint32_t numChannels = m_track->getNumChannels();
    if (numChannels == 0) return;
    
    Log::info("generateWaveformCache: Generating cache for " + m_track->getName() + 
              " Size: " + std::to_string(audioData.size()) + 
              " Width: " + std::to_string(width));

    // Clear and resize cache
    m_waveformCache.clear();
    m_waveformCache.resize(width, {0.0f, 0.0f});
    
    // Calculate samples per pixel
    uint32_t totalSamples = static_cast<uint32_t>(audioData.size()) / numChannels;
    float samplesPerPixel = static_cast<float>(totalSamples) / static_cast<float>(width);
    
    // Pre-calculate min/max for each pixel column
    for (int x = 0; x < width; ++x) {
        uint32_t sampleStart = static_cast<uint32_t>(x * samplesPerPixel);
        uint32_t sampleEnd = static_cast<uint32_t>((x + 1) * samplesPerPixel);
        
        if (sampleStart >= totalSamples) break;
        if (sampleEnd > totalSamples) sampleEnd = totalSamples;
        
        float minVal = 1000.0f;
        float maxVal = -1000.0f;
        bool hasSamples = false;

        // Ensure we check at least one sample
        if (sampleEnd <= sampleStart && sampleStart < totalSamples) {
             sampleEnd = sampleStart + 1;
        }
        
        for (uint32_t s = sampleStart; s < sampleEnd; ++s) {
            uint32_t sampleIndex = s * numChannels;
            if (sampleIndex < audioData.size()) {
                float sample = audioData[sampleIndex];
                if (sample < minVal) minVal = sample;
                if (sample > maxVal) maxVal = sample;
                hasSamples = true;
            }
        }

        if (!hasSamples) {
            minVal = 0.0f;
            maxVal = 0.0f;
        }
        
        m_waveformCache[x] = {minVal, maxVal};

        // Debug log for middle pixel
        if (x == width / 2) {
             Log::info("generateWaveformCache: Middle pixel min=" + std::to_string(minVal) + " max=" + std::to_string(maxVal));
        }
    }
    
    // Update cache state
    m_cachedWidth = width;
    m_cachedHeight = height;
    m_cachedAudioDataSize = audioData.size();
}

// Draw waveform for a specific track (for multi-clip lane support)
void TrackUIComponent::drawWaveformForTrack(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& bounds,
                                            std::shared_ptr<Track> track, float offsetRatio, float visibleRatio) {
    if (!track) return;

    const auto& audioData = track->getAudioData();
    if (audioData.empty()) return;

    int width = static_cast<int>(bounds.width);
    int height = static_cast<int>(bounds.height);
    
    // For non-primary tracks, we need to generate waveform data on the fly
    // (the cache is only for the primary track)
    // Use a simpler approach: directly sample the audio data
    
    uint32_t color = track->getColor();
    NomadUI::NUIColor waveformColor = NomadUI::NUIColor(
        (color >> 16) & 0xFF,
        (color >> 8) & 0xFF,
        color & 0xFF,
        (color >> 24) & 0xFF
    ) / 255.0f;
    waveformColor = waveformColor.withAlpha(0.7f);
    
    int centerY = bounds.y + height / 2;
    
    // Draw center line
    renderer.drawLine(
        NomadUI::NUIPoint(bounds.x, centerY),
        NomadUI::NUIPoint(bounds.x + bounds.width, centerY),
        1.0f,
        waveformColor.withAlpha(0.3f)
    );
    
    // Calculate sample range to draw
    size_t numChannels = track->getNumChannels();
    size_t totalFrames = audioData.size() / (numChannels > 0 ? numChannels : 1);
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
    
    float halfHeight = height / 2.0f;
    size_t framesPerPixel = std::max(size_t(1), visibleFrames / width);
    
    for (int x = 0; x < width; ++x) {
        size_t frameIndex = startFrame + (x * visibleFrames) / width;
        size_t frameEnd = std::min(frameIndex + framesPerPixel, endFrame);
        
        float minVal = 0.0f;
        float maxVal = 0.0f;
        
        // Find min/max in this pixel's range
        for (size_t f = frameIndex; f < frameEnd; ++f) {
            for (size_t c = 0; c < numChannels; ++c) {
                size_t sampleIndex = f * numChannels + c;
                if (sampleIndex < audioData.size()) {
                    float sample = audioData[sampleIndex];
                    minVal = std::min(minVal, sample);
                    maxVal = std::max(maxVal, sample);
                }
            }
        }
        
        // Calculate screen coordinates
        float topY = centerY - maxVal * halfHeight;
        float bottomY = centerY - minVal * halfHeight;
        
        // Ensure silence is rendered as a 1px line
        if (bottomY - topY < 1.0f) {
            topY = centerY - 0.5f;
            bottomY = centerY + 0.5f;
        }
        
        topPoints.push_back(NomadUI::NUIPoint(bounds.x + x, topY));
        bottomPoints.push_back(NomadUI::NUIPoint(bounds.x + x, bottomY));
    }
    
    // Single draw call for entire waveform
    if (!topPoints.empty()) {
        renderer.fillWaveform(topPoints.data(), bottomPoints.data(), static_cast<int>(topPoints.size()), waveformColor);
    }
}

void TrackUIComponent::drawWaveform(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& bounds,
                                     float offsetRatio, float visibleRatio) {
    if (!m_track) return;

    const auto& audioData = m_track->getAudioData();
    if (audioData.empty()) return;

    int width = static_cast<int>(bounds.width);
    int height = static_cast<int>(bounds.height);
    
    // Generate cache for reasonable resolution (fixed size, not based on pixel width)
    // Use a fixed cache size of 4096 samples to represent the full waveform
    const int CACHE_SIZE = 4096;
    
    // Regenerate cache only if audio data changed
    if (m_cachedAudioDataSize != audioData.size() || m_waveformCache.size() != CACHE_SIZE) {
        generateWaveformCache(CACHE_SIZE, height);
    }
    
    // Fast drawing from cache
    if (m_waveformCache.empty()) {
        Log::warning("drawWaveform: Cache empty for " + m_track->getName());
        return;
    }
    
    uint32_t color = m_track->getColor();
    NomadUI::NUIColor waveformColor = NomadUI::NUIColor(
        (color >> 16) & 0xFF,
        (color >> 8) & 0xFF,
        color & 0xFF,
        (color >> 24) & 0xFF
    ) / 255.0f;
    waveformColor = waveformColor.withAlpha(0.7f);
    
    int centerY = bounds.y + height / 2;
    
    // Draw center line
    renderer.drawLine(
        NomadUI::NUIPoint(bounds.x, centerY),
        NomadUI::NUIPoint(bounds.x + bounds.width, centerY),
        1.0f,
        waveformColor.withAlpha(0.3f)
    );
    
    // Calculate which portion of the cache to draw based on scroll/zoom
    int cacheSize = static_cast<int>(m_waveformCache.size());
    int cacheStartIndex = static_cast<int>(offsetRatio * cacheSize);
    int cacheEndIndex = static_cast<int>((offsetRatio + visibleRatio) * cacheSize);
    
    // Clamp to valid range
    cacheStartIndex = std::max(0, std::min(cacheStartIndex, cacheSize - 1));
    cacheEndIndex = std::max(cacheStartIndex, std::min(cacheEndIndex, cacheSize));
    
    int visibleCacheSamples = cacheEndIndex - cacheStartIndex;
    if (visibleCacheSamples <= 0) {
        Log::warning("drawWaveform: No visible samples. Start=" + std::to_string(cacheStartIndex) + " End=" + std::to_string(cacheEndIndex));
        return;
    }
    
    // OPTIMIZED: Build waveform as triangle strip instead of per-pixel lines
    // This reduces draw calls from 1920 to 1 per waveform
    std::vector<NomadUI::NUIPoint> topPoints;
    std::vector<NomadUI::NUIPoint> bottomPoints;
    topPoints.reserve(width);
    bottomPoints.reserve(width);
    
    float halfHeight = height / 2.0f;
    
    for (int x = 0; x < width; ++x) {
        // Map screen pixel to cache index
        float cacheProgress = static_cast<float>(x) / static_cast<float>(width);
        int cacheIndex = cacheStartIndex + static_cast<int>(cacheProgress * visibleCacheSamples);
        
        if (cacheIndex >= cacheEndIndex || cacheIndex >= cacheSize) break;
        
        const auto& minMax = m_waveformCache[cacheIndex];
        float minVal = minMax.first;
        float maxVal = minMax.second;
        
        // Calculate screen coordinates
        float topY = centerY - maxVal * halfHeight;
        float bottomY = centerY - minVal * halfHeight;
        
        // Ensure silence is rendered as a 1px line
        if (bottomY - topY < 1.0f) {
            topY = centerY - 0.5f;
            bottomY = centerY + 0.5f;
        }
        
        // Top point (max value) and bottom point (min value)
        topPoints.push_back(NomadUI::NUIPoint(bounds.x + x, topY));
        bottomPoints.push_back(NomadUI::NUIPoint(bounds.x + x, bottomY));
    }
    
    // Single draw call for entire waveform
    if (!topPoints.empty()) {
        renderer.fillWaveform(topPoints.data(), bottomPoints.data(), static_cast<int>(topPoints.size()), waveformColor);
    }
}

// Draw sample clip container (FL Studio style) - uses a specific track (for multi-clip lanes)
void TrackUIComponent::drawSampleClipForTrack(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& clipBounds, std::shared_ptr<Track> track) {
    if (!track) return;
    
    // Get track color
    uint32_t color = track->getColor();
    NomadUI::NUIColor clipColor = NomadUI::NUIColor(
        (color >> 16) & 0xFF,
        (color >> 8) & 0xFF,
        color & 0xFF,
        (color >> 24) & 0xFF
    ) / 255.0f;
    
    // Draw semi-transparent filled background
    NomadUI::NUIColor bgColor = clipColor.withAlpha(0.15f);
    renderer.fillRect(clipBounds, bgColor);
    
    // Draw border around the clip - white when selected, track color otherwise
    NomadUI::NUIColor borderColor;
    float borderWidth;
    
    // Check if this specific clip is selected (primary track selected and this is primary)
    bool clipSelected = m_selected && (track == m_track);
    
    if (clipSelected) {
        // Bright white border for selected clip
        borderColor = NomadUI::NUIColor(1.0f, 1.0f, 1.0f, 0.95f);
        borderWidth = 2.0f;
    } else {
        borderColor = clipColor.withAlpha(0.6f);
        borderWidth = 1.0f;
    }
    
    // Top border (thicker when selected)
    renderer.drawLine(
        NomadUI::NUIPoint(clipBounds.x, clipBounds.y),
        NomadUI::NUIPoint(clipBounds.x + clipBounds.width, clipBounds.y),
        clipSelected ? 3.0f : 2.0f,
        clipSelected ? borderColor : borderColor.withAlpha(0.8f)
    );
    
    // Bottom border
    renderer.drawLine(
        NomadUI::NUIPoint(clipBounds.x, clipBounds.y + clipBounds.height),
        NomadUI::NUIPoint(clipBounds.x + clipBounds.width, clipBounds.y + clipBounds.height),
        borderWidth,
        borderColor
    );
    
    // Left border
    renderer.drawLine(
        NomadUI::NUIPoint(clipBounds.x, clipBounds.y),
        NomadUI::NUIPoint(clipBounds.x, clipBounds.y + clipBounds.height),
        borderWidth,
        borderColor
    );
    
    // Right border
    renderer.drawLine(
        NomadUI::NUIPoint(clipBounds.x + clipBounds.width, clipBounds.y),
        NomadUI::NUIPoint(clipBounds.x + clipBounds.width, clipBounds.y + clipBounds.height),
        borderWidth,
        borderColor
    );
    
    // Draw sample name strip at top of clip (FL Studio style)
    float nameStripHeight = 16.0f;
    if (clipBounds.height > nameStripHeight + 5) {
        // Name strip background (solid color from track)
        NomadUI::NUIRect nameStripBounds(
            clipBounds.x,
            clipBounds.y,
            clipBounds.width,
            nameStripHeight
        );
        NomadUI::NUIColor stripColor = clipColor.withAlpha(0.85f);
        renderer.fillRect(nameStripBounds, stripColor);
        
        // Draw sample name text (from the actual loaded file, not track name)
        std::string sampleName;
        const std::string& sourcePath = track->getSourcePath();
        if (!sourcePath.empty()) {
            // Extract filename from source path
            size_t lastSlash = sourcePath.find_last_of("/\\");
            sampleName = (lastSlash != std::string::npos) ? sourcePath.substr(lastSlash + 1) : sourcePath;
            // Remove extension for cleaner display
            size_t lastDot = sampleName.find_last_of('.');
            if (lastDot != std::string::npos) {
                sampleName = sampleName.substr(0, lastDot);
            }
        } else {
            sampleName = track->getName(); // Fallback to track name if no source path
        }
        // Truncate if too long
        if (sampleName.length() > 20 && clipBounds.width < 200) {
            sampleName = sampleName.substr(0, 17) + "...";
        }
        
        // Text color (light on dark background)
        NomadUI::NUIColor textColor(1.0f, 1.0f, 1.0f, 0.95f);
        
        // Draw text with padding
        NomadUI::NUIPoint textPos(clipBounds.x + 4.0f, clipBounds.y + 2.0f);
        renderer.drawText(sampleName, textPos, 11.0f, textColor);
    }
}

// Legacy wrapper for backward compatibility
void TrackUIComponent::drawSampleClip(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& clipBounds) {
    drawSampleClipForTrack(renderer, clipBounds, m_track);
}

// Helper to draw a clip at its calculated position (for multi-clip lane support)
void TrackUIComponent::drawClipAtPosition(NomadUI::NUIRenderer& renderer, std::shared_ptr<Track> clip,
                                          const NomadUI::NUIRect& bounds, float controlAreaWidth) {
    if (!clip || clip->getAudioData().empty()) return;
    
    // Calculate waveform position in timeline space
    double startPositionSeconds = clip->getStartPositionInTimeline();
    double audioDuration = clip->getDuration();
    double bpm = 120.0; // TODO: Get from project settings
    double secondsPerBeat = 60.0 / bpm;
    
    // Convert timeline position to beats
    double startPositionInBeats = startPositionSeconds / secondsPerBeat;
    double durationInBeats = audioDuration / secondsPerBeat;
    
    // Calculate waveform dimensions in pixel space
    float waveformWidthInPixels = static_cast<float>(durationInBeats * m_pixelsPerBeat);
    float waveformStartX = bounds.x + controlAreaWidth + 5 +
                           static_cast<float>(startPositionInBeats * m_pixelsPerBeat) -
                           m_timelineScrollOffset;
    
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
                NomadUI::NUIRect clippedClipBounds(
                    clipStartX,
                    bounds.y + 2,
                    clipWidth,
                    bounds.height - 4
                );
                drawSampleClipForTrack(renderer, clippedClipBounds, clip);
                
                // Store FULL clip bounds for hit testing (for ALL clips, not just primary)
                NomadUI::NUIRect fullClipBounds(
                    waveformStartX,
                    bounds.y + 2,
                    waveformWidthInPixels,
                    bounds.height - 4
                );
                m_allClipBounds[clip] = fullClipBounds;
                
                // Also store for primary if this is the primary track (backwards compatibility)
                if (clip == m_track) {
                    m_clipBounds = fullClipBounds;
                }
                
                // Draw waveform INSIDE the clip
                float nameStripHeight = 16.0f;
                float waveformPadding = 2.0f;
                NomadUI::NUIRect waveformInsideClip(
                    visibleStartX,
                    bounds.y + 2 + nameStripHeight + waveformPadding,
                    visibleWidth,
                    bounds.height - 4 - nameStripHeight - waveformPadding * 2
                );
                drawWaveformForTrack(renderer, waveformInsideClip, clip, offsetRatio, visibleRatio);
            }
        }
    }
}

// UI Rendering
void TrackUIComponent::onRender(NomadUI::NUIRenderer& renderer) {
    NomadUI::NUIRect bounds = getBounds();
    
    // Clear clip bounds map - will be repopulated during drawClipAtPosition
    m_allClipBounds.clear();

    // Get theme colors and layout
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    NomadUI::NUIColor trackBgColor = themeManager.getColor("backgroundPrimary"); // #19191c - Same black as title bar
    NomadUI::NUIColor borderColor = themeManager.getColor("border");
    
    float buttonX = themeManager.getComponentDimension("trackControls", "buttonStartX");
    float controlAreaWidth = buttonX + layout.controlButtonWidth + 10; // Up to end of buttons
    
    // Only PRIMARY components draw the control area and background
    // Secondary components (same lane) only draw their clip
    if (m_isPrimaryForLane) {
        // Determine track highlight color based on state
        NomadUI::NUIColor highlightColor = trackBgColor;
        
        if (m_track) {
            if (m_track->isSoloed()) {
                highlightColor = NomadUI::NUIColor(0.3f, 0.3f, 0.35f, 1.0f);
            } else if (m_track->isMuted()) {
                highlightColor = NomadUI::NUIColor(0.05f, 0.05f, 0.05f, 1.0f);
            } else if (m_selected) {
                highlightColor = NomadUI::NUIColor(0.15f, 0.15f, 0.15f, 1.0f);
            }
        }

        // Draw track background (full width)
        renderer.fillRect(bounds, trackBgColor);
        
        // Draw highlight ONLY in control area (not playlist area)
        NomadUI::NUIRect controlAreaBounds(bounds.x, bounds.y, controlAreaWidth, bounds.height);
        renderer.fillRect(controlAreaBounds, highlightColor);
        
        // Draw vertical separator between control area and playlist area
        renderer.drawLine(
            NomadUI::NUIPoint(bounds.x + controlAreaWidth, bounds.y),
            NomadUI::NUIPoint(bounds.x + controlAreaWidth, bounds.y + bounds.height),
            1.0f,
            borderColor.withAlpha(0.5f)
        );
        
        // Draw horizontal separator at bottom of track (filling the gap)
        float separatorY = bounds.y + bounds.height + 1.0f;
        renderer.drawLine(
            NomadUI::NUIPoint(bounds.x, separatorY),
            NomadUI::NUIPoint(bounds.x + bounds.width, separatorY),
            2.0f,
            NomadUI::NUIColor(0.0f, 0.0f, 0.0f, 1.0f)
        );
        
        // Draw grid ABOVE track content (playlist grid)
        drawPlaylistGrid(renderer, bounds);
    }
    
    // === MULTI-CLIP RENDERING (FL STUDIO STYLE) ===
    // Draw ALL clips on this lane: primary track + lane clips
    // Each clip is positioned based on its timeline position
    
    // Draw primary track's clip
    if (m_track && !m_track->getAudioData().empty()) {
        drawClipAtPosition(renderer, m_track, bounds, controlAreaWidth);
    }
    
    // Draw additional lane clips (from split operations)
    for (const auto& laneClip : m_laneClips) {
        if (laneClip && !laneClip->getAudioData().empty()) {
            drawClipAtPosition(renderer, laneClip, bounds, controlAreaWidth);
        }
    }

    // Apply greyscale overlay to playlist area for muted tracks (Bug #8: Mute/Solo Visual Feedback)
    if (m_track && m_track->isMuted() && m_isPrimaryForLane) {
        // Determine playlist area bounds (right side, after control area)
        auto& themeManager = NomadUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float buttonX = themeManager.getComponentDimension("trackControls", "buttonStartX");
        float controlAreaWidth = buttonX + layout.controlButtonWidth + 10;
        
        NomadUI::NUIRect playlistArea(
            bounds.x + controlAreaWidth,
            bounds.y,
            bounds.width - controlAreaWidth,
            bounds.height
        );
        
        // Semi-transparent dark grey overlay to indicate muted state
        NomadUI::NUIColor muteOverlay = NomadUI::NUIColor(0.0f, 0.0f, 0.0f, 0.4f);
        renderer.fillRect(playlistArea, muteOverlay);
    }

    // Render child components (controls only - name label and buttons) - PRIMARY ONLY
    if (m_isPrimaryForLane) {
        renderChildren(renderer);
    }
}

// Draw playlist grid (beat/bar grid)
void TrackUIComponent::drawPlaylistGrid(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& bounds) {
    // Get layout dimensions from theme
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    float buttonX = themeManager.getComponentDimension("trackControls", "buttonStartX");
    
    // Grid settings - start after buttons (buttonX + buttonWidth + margin)
    float gridStartX = bounds.x + buttonX + layout.controlButtonWidth + 10; // 10px margin after record button
    float gridWidth = bounds.width - (buttonX + layout.controlButtonWidth + 10);
    float gridEndX = gridStartX + gridWidth;
    
    // Grid colors
    NomadUI::NUIColor barLineColor(0.5f, 0.5f, 0.5f, 0.6f);   // Brighter, more visible lines for bars
    NomadUI::NUIColor beatLineColor(0.3f, 0.3f, 0.3f, 0.4f);  // Slightly brighter lines for beats
    
    // Grid spacing - DYNAMIC based on zoom level from TrackManagerUI
    float pixelsPerBar = m_pixelsPerBeat * m_beatsPerBar;
    
    // Calculate which bar to start/end drawing from
    // We start a bit earlier to ensure smooth scrolling, but we'll clip manually
    int startBar = static_cast<int>(m_timelineScrollOffset / pixelsPerBar);
    int endBar = static_cast<int>((m_timelineScrollOffset + gridWidth) / pixelsPerBar) + 1;
    
    // Draw vertical grid lines with horizontal scroll offset
    for (int bar = startBar; bar <= endBar; ++bar) {
        // Calculate x position accounting for scroll offset
        float x = gridStartX + (bar * pixelsPerBar) - m_timelineScrollOffset;
        
        // ZEBRA STRIPING: Draw slightly lighter background for odd bars
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

        // Strict manual culling for lines to prevent bleeding
        // Only draw if strictly within the grid area
        if (x >= gridStartX && x <= gridEndX) {
            // Bar line (every 4 beats)
            renderer.drawLine(
                NomadUI::NUIPoint(x, bounds.y),
                NomadUI::NUIPoint(x, bounds.y + bounds.height),
                2.0f, 
                barLineColor
            );
        }
        
        // Beat lines within the bar
        for (int beat = 1; beat < m_beatsPerBar; ++beat) {
            float beatX = x + (beat * m_pixelsPerBeat);
            
            // Strict manual culling for beat lines
            if (beatX >= gridStartX && beatX <= gridEndX) {
                renderer.drawLine(
                    NomadUI::NUIPoint(beatX, bounds.y),
                    NomadUI::NUIPoint(beatX, bounds.y + bounds.height),
                    1.0f,
                    beatLineColor
                );
            }
        }
    }
}

void TrackUIComponent::onMouseEnter() {
    // Ensure buttons get proper hover events
    NUIComponent::onMouseEnter();
}

void TrackUIComponent::onMouseLeave() {
    // Ensure buttons get proper hover events
    NUIComponent::onMouseLeave();
}

void TrackUIComponent::onUpdate(double deltaTime) {
    // Only update UI when track state might have changed, not every frame
    // This prevents overriding hover colors unnecessarily

    // Check if we need to update UI (track state changed, etc.)
    static TrackState lastState = TrackState::Empty;
    static bool lastMuted = false;
    static bool lastSoloed = false;

    if (m_track) {
        TrackState currentState = m_track->getState();
        bool currentMuted = m_track->isMuted();
        bool currentSoloed = m_track->isSoloed();

        // Only update UI if something actually changed
        if (currentState != lastState || currentMuted != lastMuted || currentSoloed != lastSoloed) {
            updateUI();
            lastState = currentState;
            lastMuted = currentMuted;
            lastSoloed = currentSoloed;
        }
    }

    // Update children
    NUIComponent::onUpdate(deltaTime);
}

void TrackUIComponent::onResize(int width, int height) {
    NomadUI::NUIRect bounds = getBounds();
    // Log::info("TrackUIComponent onResize: parent bounds x=" + std::to_string(bounds.x) + ", y=" + std::to_string(bounds.y) + ", w=" + std::to_string(bounds.width) + ", h=" + std::to_string(bounds.height));

    // Get layout dimensions from theme
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    // Layout controls vertically next to the name label using configurable dimensions
    float centerY = bounds.y + (bounds.height - layout.trackLabelHeight) / 2.0f;
    float buttonX = themeManager.getComponentDimension("trackControls", "buttonStartX"); // Use component-specific setting
    float labelWidth = std::max(80.0f, buttonX - layout.panelMargin * 2.0f);

    // Name label on the left with compact margin
    if (m_nameLabel) {
        m_nameLabel->setBounds(NUIAbsolute(bounds, layout.panelMargin, centerY - bounds.y, labelWidth, layout.trackLabelHeight));
        // Log::info("TrackUIComponent nameLabel bounds: x=" + std::to_string(bounds.x + layout.panelMargin) + ", y=" + std::to_string(centerY));
    }
    
    // Duration label below name label
    if (m_durationLabel) {
        m_durationLabel->setBounds(NUIAbsolute(bounds, layout.panelMargin, centerY - bounds.y + layout.trackLabelHeight + 2, 140, 20));
    }

    // Buttons vertically centered in a compact group
    float buttonGroupHeight = 3 * layout.controlButtonHeight + 2 * layout.controlButtonSpacing;
    float buttonY = bounds.y + (bounds.height - buttonGroupHeight) / 2.0f; // Center the group vertically

    // Layout buttons with compact spacing
    if (m_muteButton) {
        m_muteButton->setBounds(NUIAbsolute(bounds, buttonX, buttonY - bounds.y, layout.controlButtonWidth, layout.controlButtonHeight));
    }

    if (m_soloButton) {
        m_soloButton->setBounds(NUIAbsolute(bounds, buttonX, (buttonY + layout.controlButtonHeight + layout.controlButtonSpacing) - bounds.y, layout.controlButtonWidth, layout.controlButtonHeight));
    }

    if (m_recordButton) {
        m_recordButton->setBounds(NUIAbsolute(bounds, buttonX, (buttonY + 2 * (layout.controlButtonHeight + layout.controlButtonSpacing)) - bounds.y, layout.controlButtonWidth, layout.controlButtonHeight));
    }

    NomadUI::NUIComponent::onResize(width, height);
}

bool TrackUIComponent::onMouseEvent(const NomadUI::NUIMouseEvent& event) {
    NomadUI::NUIRect bounds = getBounds();
    
    // Early exit: If event is outside our bounds and we're not in an active operation, don't handle it
    bool isInsideBounds = bounds.contains(event.position);
    bool isActiveOperation = m_isTrimming || m_isDraggingClip || m_clipDragPotential;
    
    if (!isInsideBounds && !isActiveOperation) {
        return false;  // Let parent/siblings handle it (e.g., scrollbar)
    }
    
    // Get theme to determine control area bounds
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    float buttonX = themeManager.getComponentDimension("trackControls", "buttonStartX");
    float controlAreaWidth = buttonX + layout.controlButtonWidth + 10;
    float controlAreaEndX = bounds.x + controlAreaWidth;
    float gridStartX = bounds.x + controlAreaWidth + 5;
    float gridEndX = bounds.x + bounds.width - 5;
    
    // PRIORITY 1: Always let child buttons handle events in control area first
    // This ensures M/S/Record buttons work even when a sample is loaded
    if (isInsideBounds && event.position.x < controlAreaEndX) {
        for (auto& child : getChildren()) {
            if (child->getBounds().contains(event.position)) {
                if (child->onMouseEvent(event)) {
                    return true;  // Button handled it
                }
            }
        }
    }
    
    auto& dragManager = NomadUI::NUIDragDropManager::getInstance();
    
    // Handle mouse release - always process to clear state
    if (!event.pressed && event.button == NomadUI::NUIMouseButton::Left) {
        bool wasActive = m_isTrimming || m_isDraggingClip || m_clipDragPotential;
        if (m_isTrimming) {
            Log::info("Finished trimming clip");
        }
        m_clipDragPotential = false;
        m_isDraggingClip = false;
        m_isTrimming = false;
        m_trimEdge = TrimEdge::None;
        m_activeClip = nullptr;  // Clear active clip
        
        // Only consume the event if we were doing something
        if (wasActive) {
            return true;
        }
        return false;
    }
    
    // PRIORITY 2: Handle active trimming (mouse move while trimming)
    if (m_isTrimming && m_activeClip) {
        // Use m_activeClip instead of m_track - may be a lane clip
        auto& clipBounds = m_allClipBounds[m_activeClip];
        float deltaX = event.position.x - m_trimDragStartX;
        
        // Convert pixel delta to time delta based on zoom level
        double duration = m_activeClip->getDuration();
        if (duration > 0 && clipBounds.width > 0) {
            double pixelsPerSecond = clipBounds.width / duration;
            double timeDelta = deltaX / pixelsPerSecond;
            
            if (m_trimEdge == TrimEdge::Left) {
                double newTrimStart = std::max(0.0, m_trimOriginalStart + timeDelta);
                double trimEnd = m_trimOriginalEnd < 0 ? duration : m_trimOriginalEnd;
                newTrimStart = std::min(newTrimStart, trimEnd - 0.01);
                m_activeClip->setTrimStart(newTrimStart);
            } else if (m_trimEdge == TrimEdge::Right) {
                double newTrimEnd = std::min(duration, m_trimOriginalEnd + timeDelta);
                newTrimEnd = std::max(newTrimEnd, m_activeClip->getTrimStart() + 0.01);
                m_activeClip->setTrimEnd(newTrimEnd);
            }
            
            if (m_onCacheInvalidationCallback) {
                m_onCacheInvalidationCallback();
            }
        }
        return true;
    }
    
    // PRIORITY 3: Handle drag threshold detection on MOUSE MOVE (not press/release)
    // Mouse move events have pressed=false and released=false
    if (m_clipDragPotential && !event.pressed && !event.released && !dragManager.isDragging()) {
        float dx = event.position.x - m_clipDragStartPos.x;
        float dy = event.position.y - m_clipDragStartPos.y;
        float distance = std::sqrt(dx * dx + dy * dy);
        
        const float DRAG_THRESHOLD = 5.0f;
        if (distance >= DRAG_THRESHOLD && m_activeClip) {
            // Start the drag operation - use m_activeClip (may be primary or lane clip)
            m_isDraggingClip = true;
            m_clipDragPotential = false;
            
            NomadUI::DragData dragData;
            dragData.type = NomadUI::DragDataType::AudioClip;
            dragData.displayName = m_activeClip->getName();
            dragData.filePath = m_activeClip->getSourcePath();
            
            // Find correct track index by searching TrackManager
            int trackIndex = -1;
            if (m_trackManager) {
                for (size_t i = 0; i < m_trackManager->getTrackCount(); ++i) {
                    if (m_trackManager->getTrack(i) == m_activeClip) {
                        trackIndex = static_cast<int>(i);
                        break;
                    }
                }
            }
            dragData.sourceTrackIndex = trackIndex;
            
            dragData.sourceTimePosition = m_activeClip->getStartPositionInTimeline();
            
            uint32_t trackColor = m_activeClip->getColor();
            dragData.accentColor = NomadUI::NUIColor(
                ((trackColor >> 16) & 0xFF) / 255.0f,
                ((trackColor >> 8) & 0xFF) / 255.0f,
                (trackColor & 0xFF) / 255.0f,
                1.0f
            );
            
            auto& clipBounds = m_allClipBounds[m_activeClip];
            dragData.previewWidth = clipBounds.width;
            dragData.previewHeight = clipBounds.height;
            
            dragManager.beginDrag(dragData, m_clipDragStartPos, this);
            Log::info("Started dragging clip: " + dragData.displayName);
            return true;
        }
        // Continue - allow other event processing while waiting for threshold
    }
    
    // PRIORITY 4: Handle clip manipulation in the grid/playlist area only (mouse press)
    if (event.pressed && event.button == NomadUI::NUIMouseButton::Left && isInsideBounds) {
        // Only process clip manipulation if click is in the grid area (not control area)
        if (event.position.x >= gridStartX && event.position.x <= gridEndX) {
            
            // Check if split tool is active
            bool isSplitToolActive = m_isSplitToolActiveCallback ? m_isSplitToolActiveCallback() : false;
            
            // === MULTI-CLIP HIT TESTING ===
            // Find which clip was clicked (check all clips in m_allClipBounds)
            std::shared_ptr<Track> clickedClip = nullptr;
            NomadUI::NUIRect clickedClipBounds;
            
            for (const auto& [clip, clipBounds] : m_allClipBounds) {
                if (clipBounds.contains(event.position)) {
                    clickedClip = clip;
                    clickedClipBounds = clipBounds;
                    break;
                }
            }
            
            // Handle SPLIT TOOL - click on clip to split at that position
            if (isSplitToolActive && clickedClip && clickedClipBounds.width > 0) {
                // Calculate time position from click - simple ratio of click position to clip width
                double clickOffsetX = event.position.x - clickedClipBounds.x;
                double duration = clickedClip->getDuration();
                
                if (clickedClipBounds.width > 0 && duration > 0) {
                    double splitRatio = clickOffsetX / clickedClipBounds.width;
                    // splitTime is seconds from start of the audio (0 to duration)
                    double splitTime = splitRatio * duration;
                    
                    Log::info("Split requested at time: " + std::to_string(splitTime) + 
                              "s (ratio=" + std::to_string(splitRatio) + ", duration=" + std::to_string(duration) + ")");
                    
                    // Find the TrackUIComponent for this clip to send split request
                    // If it's the primary track, use this component
                    // If it's a lane clip, we need to find its actual TrackUIComponent in TrackManagerUI
                    // For now, we'll handle split through the parent
                    if (m_onSplitRequestedCallback && clickedClip == m_track) {
                        m_onSplitRequestedCallback(this, splitTime);
                    } else if (m_onSplitRequestedCallback) {
                        // Lane clip - need to pass the correct track index to parent
                        // We can't easily do this here, so let's log for now and handle via parent
                        Log::info("Split on lane clip - routing to TrackManager");
                        // The parent TrackManagerUI will need to handle this via direct track lookup
                        // For now, we'll call with this component and figure out the clip via position
                        m_onSplitRequestedCallback(this, splitTime);
                    }
                }
                return true;
            }
            
            // Check if clicking on any clip for drag initiation or trimming
            if (clickedClip && clickedClipBounds.width > 0) {
                float leftEdge = clickedClipBounds.x;
                float rightEdge = clickedClipBounds.x + clickedClipBounds.width;
                
                // Left edge trim detection
                if (std::abs(event.position.x - leftEdge) < TRIM_EDGE_WIDTH &&
                    event.position.y >= clickedClipBounds.y && 
                    event.position.y <= clickedClipBounds.y + clickedClipBounds.height) {
                    m_trimEdge = TrimEdge::Left;
                    m_isTrimming = true;
                    m_trimDragStartX = event.position.x;
                    m_trimOriginalStart = clickedClip->getTrimStart();
                    m_trimOriginalEnd = clickedClip->getTrimEnd();
                    m_activeClip = clickedClip;
                    m_selected = true;
                    Log::info("Started trimming left edge of clip: " + clickedClip->getName());
                    return true;
                }
                
                // Right edge trim detection
                if (std::abs(event.position.x - rightEdge) < TRIM_EDGE_WIDTH &&
                    event.position.y >= clickedClipBounds.y && 
                    event.position.y <= clickedClipBounds.y + clickedClipBounds.height) {
                    m_trimEdge = TrimEdge::Right;
                    m_isTrimming = true;
                    m_trimDragStartX = event.position.x;
                    m_trimOriginalStart = clickedClip->getTrimStart();
                    m_trimOriginalEnd = clickedClip->getTrimEnd() < 0 ? clickedClip->getDuration() : clickedClip->getTrimEnd();
                    m_activeClip = clickedClip;
                    m_selected = true;
                    Log::info("Started trimming right edge of clip: " + clickedClip->getName());
                    return true;
                }
                
                // Click inside clip - start potential drag
                m_clipDragPotential = true;
                m_clipDragStartPos = event.position;
                m_activeClip = clickedClip;
                m_selected = true;
                Log::info("Clip selected - ready for drag: " + clickedClip->getName());
                return true;
            }
            
            // Grid area click (not on any clip) - select track and set position
            m_selected = true;
            if (m_track && event.position.y > bounds.y + 30) {
                // Clicked in grid/playlist area - set play position
                double gridWidth = gridEndX - gridStartX;
                double clickRatio = static_cast<double>(event.position.x - gridStartX) / gridWidth;
                clickRatio = std::max(0.0, std::min(1.0, clickRatio));
                double maxDuration = m_track->getDuration() > 0 ? m_track->getDuration() : 10.0; // Default 10s if no audio
                double newPosition = clickRatio * maxDuration;
                m_track->setPosition(newPosition);
                Log::info("Track position set to: " + std::to_string(newPosition));
            }
            return true;
        }
        
        // Click in control area (but not on a button) - just select the track
        if (event.position.x < controlAreaEndX) {
            m_selected = true;
            return true;
        }
    }
    
    // Handle right-click to delete clip (FL Studio style) - check all clips
    if (event.pressed && event.button == NomadUI::NUIMouseButton::Right && isInsideBounds) {
        // Find which clip was right-clicked
        for (const auto& [clip, clipBounds] : m_allClipBounds) {
            if (clipBounds.contains(event.position)) {
                if (m_onClipDeletedCallback) {
                    m_onClipDeletedCallback(this, event.position);
                }
                return true;
            }
        }
    }

    // Pass through to parent if not handled
    return false;
}

} // namespace Audio
} // namespace Nomad
