// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "TrackUIComponent.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadCore/include/NomadLog.h"
#include <algorithm>
#include <cmath>

namespace Nomad {
namespace Audio {

TrackUIComponent::TrackUIComponent(std::shared_ptr<Track> track)
    : m_track(track)
{
    if (!m_track) {
        Log::error("TrackUIComponent created with null track");
        return;
    }

    // Create track name label
    m_nameLabel = std::make_shared<NomadUI::NUILabel>();
    m_nameLabel->setText(m_track->getName());
    updateTrackNameColors();
    addChild(m_nameLabel);
    
    // Create duration label (shows sample duration)
    m_durationLabel = std::make_shared<NomadUI::NUILabel>();
    m_durationLabel->setText("");
    m_durationLabel->setTextColor(NomadUI::NUIColor(0.5f, 0.5f, 0.5f, 1.0f)); // Grey text
    addChild(m_durationLabel);


    // Create mute button
    m_muteButton = std::make_shared<NomadUI::NUIButton>();
    m_muteButton->setText("M");
    m_muteButton->setStyle(NomadUI::NUIButton::Style::Secondary); // Back to Secondary for cool animations
    m_muteButton->setHoverColor(NomadUI::NUIColor(70.0f/255.0f, 70.0f/255.0f, 70.0f/255.0f)); // Dull grey hover
    m_muteButton->setPressedColor(NomadUI::NUIColor(50.0f/255.0f, 50.0f/255.0f, 50.0f/255.0f)); // Darker grey when pressed
    m_muteButton->setOnClick([this]() {
        onMuteToggled();
    });
    addChild(m_muteButton);

    // Create solo button
    m_soloButton = std::make_shared<NomadUI::NUIButton>();
    m_soloButton->setText("S");
    m_soloButton->setStyle(NomadUI::NUIButton::Style::Secondary); // Back to Secondary for cool animations
    m_soloButton->setHoverColor(NomadUI::NUIColor(70.0f/255.0f, 70.0f/255.0f, 70.0f/255.0f)); // Dull grey hover
    m_soloButton->setPressedColor(NomadUI::NUIColor(50.0f/255.0f, 50.0f/255.0f, 50.0f/255.0f)); // Darker grey when pressed
    m_soloButton->setOnClick([this]() {
        onSoloToggled();
    });
    addChild(m_soloButton);

    // Create record button
    m_recordButton = std::make_shared<NomadUI::NUIButton>();
    m_recordButton->setText("â—");
    m_recordButton->setStyle(NomadUI::NUIButton::Style::Icon); // Keep Icon style for record circle
    m_recordButton->setHoverColor(NomadUI::NUIColor(70.0f/255.0f, 70.0f/255.0f, 70.0f/255.0f)); // Dull grey hover
    m_recordButton->setPressedColor(NomadUI::NUIColor(50.0f/255.0f, 50.0f/255.0f, 50.0f/255.0f)); // Darker grey when pressed
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
        
        updateUI();
        Log::info("Track " + m_track->getName() + " mute: " + (newMute ? "ON" : "OFF"));
    }
}

void TrackUIComponent::onSoloToggled() {
    if (m_track) {
        bool newSolo = !m_track->isSoloed();
        m_track->setSolo(newSolo);
        
        // If soloing, auto-disable mute (solo takes priority)
        if (newSolo && m_track->isMuted()) {
            m_track->setMute(false);
        }
        
        updateUI();
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

        // Don't override text color if button is being hovered (to allow hover preview)
        if (!m_muteButton->isHovered()) {
            if (m_track->isMuted()) {
                m_muteButton->setTextColor(themeManager.getColor("error")); // Red text when muted
            } else {
                m_muteButton->setTextColor(themeManager.getColor("textPrimary")); // White text when not muted
            }
        }
    }

    if (m_soloButton) {
        auto& themeManager = NomadUI::NUIThemeManager::getInstance();
        // Use theme colors for Primary style
        m_soloButton->setBackgroundColor(themeManager.getColor("surfaceTertiary")); // #28282d - same as transport buttons

        // Don't override text color if button is being hovered (to allow hover preview)
        if (!m_soloButton->isHovered()) {
            if (m_track->isSoloed()) {
                m_soloButton->setTextColor(themeManager.getColor("accentLime")); // Lime text when soloed
            } else {
                m_soloButton->setTextColor(themeManager.getColor("textPrimary")); // White text when not soloed
            }
        }
    }

    if (m_recordButton) {
        auto& themeManager = NomadUI::NUIThemeManager::getInstance();
        // Use theme colors for Icon style
        m_recordButton->setBackgroundColor(themeManager.getColor("surfaceTertiary")); // #28282d - same as transport buttons
        TrackState state = m_track->getState();
        if (state == TrackState::Recording) {
            m_recordButton->setTextColor(themeManager.getColor("error")); // Red text when recording
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
    if (audioData.empty()) return;
    
    uint32_t numChannels = m_track->getNumChannels();
    if (numChannels == 0) return;
    
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
        
        float minVal = 0.0f;
        float maxVal = 0.0f;
        
        for (uint32_t s = sampleStart; s < sampleEnd; ++s) {
            uint32_t sampleIndex = s * numChannels;
            if (sampleIndex < audioData.size()) {
                float sample = audioData[sampleIndex];
                if (sample < minVal) minVal = sample;
                if (sample > maxVal) maxVal = sample;
            }
        }
        
        m_waveformCache[x] = {minVal, maxVal};
    }
    
    // Update cache state
    m_cachedWidth = width;
    m_cachedHeight = height;
    m_cachedAudioDataSize = audioData.size();
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
    if (m_waveformCache.empty()) return;
    
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
    if (visibleCacheSamples <= 0) return;
    
    // Draw the visible portion across the screen width
    for (int x = 0; x < width; ++x) {
        // Map screen pixel to cache index
        float cacheProgress = static_cast<float>(x) / static_cast<float>(width);
        int cacheIndex = cacheStartIndex + static_cast<int>(cacheProgress * visibleCacheSamples);
        
        if (cacheIndex >= cacheEndIndex || cacheIndex >= cacheSize) break;
        
        const auto& minMax = m_waveformCache[cacheIndex];
        float minVal = minMax.first;
        float maxVal = minMax.second;
        
        int yMin = centerY - static_cast<int>(maxVal * height / 2);
        int yMax = centerY - static_cast<int>(minVal * height / 2);
        
        if (yMax > yMin) {
            renderer.drawLine(
                NomadUI::NUIPoint(bounds.x + x, yMin),
                NomadUI::NUIPoint(bounds.x + x, yMax),
                1.0f,
                waveformColor
            );
        }
    }
}

// Draw sample clip container (FL Studio style)
void TrackUIComponent::drawSampleClip(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& clipBounds) {
    if (!m_track) return;
    
    // Get track color
    uint32_t color = m_track->getColor();
    NomadUI::NUIColor clipColor = NomadUI::NUIColor(
        (color >> 16) & 0xFF,
        (color >> 8) & 0xFF,
        color & 0xFF,
        (color >> 24) & 0xFF
    ) / 255.0f;
    
    // Draw semi-transparent filled background
    NomadUI::NUIColor bgColor = clipColor.withAlpha(0.15f);
    renderer.fillRect(clipBounds, bgColor);
    
    // Draw border around the clip
    NomadUI::NUIColor borderColor = clipColor.withAlpha(0.6f);
    
    // Top border (thicker, brighter)
    renderer.drawLine(
        NomadUI::NUIPoint(clipBounds.x, clipBounds.y),
        NomadUI::NUIPoint(clipBounds.x + clipBounds.width, clipBounds.y),
        2.0f,
        borderColor.withAlpha(0.8f)
    );
    
    // Bottom border
    renderer.drawLine(
        NomadUI::NUIPoint(clipBounds.x, clipBounds.y + clipBounds.height),
        NomadUI::NUIPoint(clipBounds.x + clipBounds.width, clipBounds.y + clipBounds.height),
        1.0f,
        borderColor
    );
    
    // Left border
    renderer.drawLine(
        NomadUI::NUIPoint(clipBounds.x, clipBounds.y),
        NomadUI::NUIPoint(clipBounds.x, clipBounds.y + clipBounds.height),
        1.0f,
        borderColor
    );
    
    // Right border
    renderer.drawLine(
        NomadUI::NUIPoint(clipBounds.x + clipBounds.width, clipBounds.y),
        NomadUI::NUIPoint(clipBounds.x + clipBounds.width, clipBounds.y + clipBounds.height),
        1.0f,
        borderColor
    );
    
    // Draw sample name overlay (top-left corner)
    // TODO: Add text rendering for sample name when text API is available
}

// UI Rendering
void TrackUIComponent::onRender(NomadUI::NUIRenderer& renderer) {
    NomadUI::NUIRect bounds = getBounds();

    // Get theme colors and layout
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    NomadUI::NUIColor trackBgColor = themeManager.getColor("backgroundPrimary"); // #19191c - Same black as title bar
    NomadUI::NUIColor borderColor = themeManager.getColor("border");
    
    // Determine track highlight color based on state
    NomadUI::NUIColor highlightColor = trackBgColor;
    
    if (m_track) {
        if (m_track->isSoloed()) {
            // Inverse highlight for solo (brighter)
            highlightColor = NomadUI::NUIColor(0.3f, 0.3f, 0.35f, 1.0f); // Lighter grey-blue
        } else if (m_track->isMuted()) {
            // Dull highlight for mute (darker)
            highlightColor = NomadUI::NUIColor(0.05f, 0.05f, 0.05f, 1.0f); // Very dark grey
        } else if (m_selected) {
            // Grey highlight for selected
            highlightColor = NomadUI::NUIColor(0.15f, 0.15f, 0.15f, 1.0f); // Medium grey
        }
    }

    // Draw track background (full width)
    renderer.fillRect(bounds, trackBgColor);
    
    // Draw highlight ONLY in control area (not playlist area)
    float buttonX = themeManager.getComponentDimension("trackControls", "buttonStartX");
    float controlAreaWidth = buttonX + layout.controlButtonWidth + 10; // Up to end of buttons
    NomadUI::NUIRect controlAreaBounds(bounds.x, bounds.y, controlAreaWidth, bounds.height);
    renderer.fillRect(controlAreaBounds, highlightColor);
    
    // Draw vertical separator between control area and playlist area
    renderer.drawLine(
        NomadUI::NUIPoint(bounds.x + controlAreaWidth, bounds.y),
        NomadUI::NUIPoint(bounds.x + controlAreaWidth, bounds.y + bounds.height),
        1.0f,
        borderColor.withAlpha(0.5f)
    );
    
    // Draw horizontal separator at bottom of track (full width, inclusive)
    renderer.drawLine(
        NomadUI::NUIPoint(bounds.x, bounds.y + bounds.height),
        NomadUI::NUIPoint(bounds.x + bounds.width, bounds.y + bounds.height),
        1.0f,
        borderColor.withAlpha(0.6f)
    );
    
    // Draw grid ABOVE track content (playlist grid)
    drawPlaylistGrid(renderer, bounds);
    
    // Draw waveform in the playlist area (if sample is loaded)
    // Waveform scrolls and zooms with the timeline
    if (m_track && !m_track->getAudioData().empty()) {
        // Calculate waveform position in timeline space
        // The waveform starts at the sample's timeline position
        
        double startPositionSeconds = m_track->getStartPositionInTimeline();
        double audioDuration = m_track->getDuration(); // in seconds
        double bpm = 120.0; // TODO: Get from project settings
        double secondsPerBeat = 60.0 / bpm;
        
        // Convert timeline position to beats
        double startPositionInBeats = startPositionSeconds / secondsPerBeat;
        double durationInBeats = audioDuration / secondsPerBeat;
        
        // Calculate waveform dimensions in pixel space
        float waveformWidthInPixels = static_cast<float>(durationInBeats * m_pixelsPerBeat);
        float waveformStartX = bounds.x + controlAreaWidth + 5 + 
                               static_cast<float>(startPositionInBeats * m_pixelsPerBeat) - 
                               m_timelineScrollOffset; // Start at sample position, scroll with timeline
        
        // Only draw if waveform is visible in the current viewport
        float gridStartX = bounds.x + controlAreaWidth + 5;
        float gridWidth = bounds.width - controlAreaWidth - 10;
        float gridEndX = gridStartX + gridWidth;
        
        // CRITICAL: Add generous padding for off-screen culling to prevent visible clipping
        // Samples will render beyond screen edges to ensure smooth scrolling experience
        float cullPaddingLeft = 400.0f;   // Start rendering 400px before visible area (increased from 200px)
        float cullPaddingRight = 400.0f;  // Stop rendering 400px after visible area (increased from 200px)
        
        // Check if waveform intersects with visible area (with culling padding)
        if (waveformStartX + waveformWidthInPixels > gridStartX - cullPaddingLeft && 
            waveformStartX < gridEndX + cullPaddingRight) {
            
            // Determine the visible portion of the waveform
            float visibleStartX = std::max(waveformStartX, gridStartX);
            float visibleEndX = std::min(waveformStartX + waveformWidthInPixels, gridEndX);
            float visibleWidth = visibleEndX - visibleStartX;
            
            if (visibleWidth > 0) {
                // Calculate offset and ratio for the visible portion
                float offsetRatio = 0.0f;
                float visibleRatio = 1.0f;
                
                if (waveformStartX < gridStartX) {
                    // Waveform starts off-screen to the left - skip the beginning
                    offsetRatio = (gridStartX - waveformStartX) / waveformWidthInPixels;
                }
                
                if (waveformStartX + waveformWidthInPixels > gridEndX) {
                    // Waveform extends off-screen to the right - calculate visible portion
                    float endRatio = (gridEndX - waveformStartX) / waveformWidthInPixels;
                    visibleRatio = endRatio - offsetRatio;
                }
                
                // Create bounds for the VISIBLE portion only
                // This is where we actually draw on screen
                NomadUI::NUIRect waveformBounds(
                    visibleStartX,  // Start at visible position (clipped)
                    bounds.y + 5,
                    visibleWidth,   // Only the visible width
                    bounds.height - 10
                );
                
                // Draw sample clip container (FL Studio style) - CLIP TO GRID AREA
                // Clip both left (control area) and right (grid end)
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
                    drawSampleClip(renderer, clippedClipBounds);
                }
                
                drawWaveform(renderer, waveformBounds, offsetRatio, visibleRatio);
            }
        }
    }

    // Apply greyscale overlay to playlist area for muted tracks (Bug #8: Mute/Solo Visual Feedback)
    if (m_track && m_track->isMuted()) {
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

    // Render child components (controls only - name label and buttons)
    renderChildren(renderer);
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
    
    // Grid colors
    NomadUI::NUIColor barLineColor(0.3f, 0.3f, 0.3f, 0.4f);   // Bright lines for bars
    NomadUI::NUIColor beatLineColor(0.2f, 0.2f, 0.2f, 0.3f);  // Dimmer lines for beats
    
    // Grid spacing - DYNAMIC based on zoom level from TrackManagerUI
    float pixelsPerBar = m_pixelsPerBeat * m_beatsPerBar;
    
    // Calculate maximum timeline extent (adaptive grid)
    // Only draw grid up to the maximum extent needed by samples
    double bpm = 120.0;
    double secondsPerBeat = 60.0 / bpm;
    double maxExtentInBeats = m_maxTimelineExtent / secondsPerBeat;
    float maxExtentInPixels = static_cast<float>(maxExtentInBeats * m_pixelsPerBeat);
    
    // Limit grid drawing to the max extent
    float gridDrawEndX = std::min(gridStartX + gridWidth, gridStartX + maxExtentInPixels - m_timelineScrollOffset);
    
    // Calculate which bar to start/end drawing from
    int startBar = static_cast<int>(m_timelineScrollOffset / pixelsPerBar);
    int endBar = static_cast<int>((m_timelineScrollOffset + (gridDrawEndX - gridStartX)) / pixelsPerBar) + 1;
    
    // Draw vertical grid lines with horizontal scroll offset
    for (int bar = startBar; bar <= endBar; ++bar) {
        // Calculate x position accounting for scroll offset
        float x = gridStartX + (bar * pixelsPerBar) - m_timelineScrollOffset;
        
        // CRITICAL: Clip to grid area - don't bleed into control area or beyond extent
        if (x < gridStartX || x > gridDrawEndX) {
            continue;
        }
        
        // Bar line (every 4 beats)
        renderer.drawLine(
            NomadUI::NUIPoint(x, bounds.y),
            NomadUI::NUIPoint(x, bounds.y + bounds.height),
            1.0f,
            barLineColor
        );
        
        // Beat lines within the bar
        for (int beat = 1; beat < m_beatsPerBar; ++beat) {
            float beatX = x + (beat * m_pixelsPerBeat);
            
            // CRITICAL: Clip beat lines too - don't bleed into control area or beyond extent
            if (beatX < gridStartX || beatX > gridDrawEndX) {
                continue;
            }
            
            renderer.drawLine(
                NomadUI::NUIPoint(beatX, bounds.y),
                NomadUI::NUIPoint(beatX, bounds.y + bounds.height),
                1.0f,
                beatLineColor
            );
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

    // Name label on the left with compact margin
    if (m_nameLabel) {
        m_nameLabel->setBounds(NUIAbsolute(bounds, layout.panelMargin, centerY - bounds.y, 80, layout.trackLabelHeight));
        // Log::info("TrackUIComponent nameLabel bounds: x=" + std::to_string(bounds.x + layout.panelMargin) + ", y=" + std::to_string(centerY));
    }
    
    // Duration label below name label
    if (m_durationLabel) {
        m_durationLabel->setBounds(NUIAbsolute(bounds, layout.panelMargin, centerY - bounds.y + layout.trackLabelHeight + 2, 80, 16));
    }

    // Buttons vertically centered in a compact group
    float buttonGroupHeight = 3 * layout.controlButtonHeight + 2 * layout.controlButtonSpacing;
    float buttonY = bounds.y + (bounds.height - buttonGroupHeight) / 2.0f; // Center the group vertically
    float buttonX = themeManager.getComponentDimension("trackControls", "buttonStartX"); // Use component-specific setting

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
    // Check if the event is over any child component (buttons) first
    for (auto& child : getChildren()) {
        if (child->getBounds().contains(event.position)) {
            // Event is over a child, let the child handle it first
            if (child->onMouseEvent(event)) {
                return true;
            }
        }
    }

    // Handle track-specific mouse events
    if (event.pressed && event.button == NomadUI::NUIMouseButton::Left) {
        NomadUI::NUIRect bounds = getBounds();
        if (bounds.contains(event.position.x, event.position.y)) {
            // Select this track
            m_selected = true;
            
            // Calculate position within track for waveform/grid click
            if (m_track && event.position.y > bounds.y + 30) {
                // Clicked in grid/playlist area - set play position or add clip
                double clickRatio = static_cast<double>(event.position.x - bounds.x - 120) / (bounds.width - 120); // Account for controls offset
                double newPosition = clickRatio * m_track->getDuration();
                m_track->setPosition(newPosition);
                Log::info("Track position set to: " + std::to_string(newPosition));
            }
            
            return true; // Track consumed the click
        }
    }

    // Pass through to parent if not handled
    return NUIComponent::onMouseEvent(event);
}

} // namespace Audio
} // namespace Nomad
