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
    m_recordButton->setText("●");
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
        updateUI();
        Log::info("Track " + m_track->getName() + " mute: " + (newMute ? "ON" : "OFF"));
    }
}

void TrackUIComponent::onSoloToggled() {
    if (m_track) {
        bool newSolo = !m_track->isSoloed();
        m_track->setSolo(newSolo);
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

void TrackUIComponent::drawWaveform(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& bounds) {
    if (!m_track) return;

    // Get track audio data for waveform visualization
    // This is a simplified waveform - in a real DAW you'd want more sophisticated rendering

    const int waveformHeight = 60;
    const int waveformY = bounds.y + 30;

    uint32_t color = m_track->getColor();
    NomadUI::NUIColor waveformColor = NomadUI::NUIColor(
        (color >> 16) & 0xFF,
        (color >> 8) & 0xFF,
        color & 0xFF,
        (color >> 24) & 0xFF
    ) / 255.0f;
    waveformColor = waveformColor.withAlpha(0.8f);

    // Draw center line
    renderer.drawLine(
        NomadUI::NUIPoint(bounds.x, waveformY + waveformHeight / 2),
        NomadUI::NUIPoint(bounds.x + bounds.width, waveformY + waveformHeight / 2),
        1.0f,
        waveformColor
    );

    // Draw waveform samples (simplified representation)
    const int samplesPerPixel = 100; // Adjust for detail level
    for (int x = 0; x < static_cast<int>(bounds.width); x += 2) {
        // Calculate sample range for this pixel
        int sampleStart = x * samplesPerPixel;
        int sampleEnd = sampleStart + samplesPerPixel;

        if (sampleStart >= sampleEnd) continue;

        // Find peak in this range (simplified)
        float peak = 0.1f; // Default low level

        int amplitude = static_cast<int>(peak * waveformHeight / 2);
        if (amplitude > 0) {
            renderer.drawLine(
                NomadUI::NUIPoint(bounds.x + x, waveformY + waveformHeight / 2 - amplitude),
                NomadUI::NUIPoint(bounds.x + x, waveformY + waveformHeight / 2 + amplitude),
                1.0f,
                waveformColor
            );
        }
    }
}

// UI Rendering
void TrackUIComponent::onRender(NomadUI::NUIRenderer& renderer) {
    NomadUI::NUIRect bounds = getBounds();

    // Get theme colors
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    NomadUI::NUIColor trackBgColor = themeManager.getColor("backgroundPrimary"); // #19191c - Same black as title bar

    // Draw track background only (clean slate for future playlist grid)
    renderer.fillRect(bounds, trackBgColor);

    // Render child components (controls only - name label and buttons)
    renderChildren(renderer);
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

    // Handle track-specific mouse events (like clicking on waveform to set play position)
    if (event.pressed && event.button == NomadUI::NUIMouseButton::Left) {
        NomadUI::NUIRect bounds = getBounds();
        if (bounds.contains(event.position.x, event.position.y)) {
            // Calculate position within track for waveform click
            if (m_track && event.position.y > bounds.y + 30) {
                // Clicked in waveform area - set play position
                double clickRatio = static_cast<double>(event.position.x - bounds.x) / bounds.width;
                double newPosition = clickRatio * m_track->getDuration();
                m_track->setPosition(newPosition);
                Log::info("Track position set to: " + std::to_string(newPosition));
            }
        }
    }

    // Pass through to parent if not handled
    return NUIComponent::onMouseEvent(event);
}

} // namespace Audio
} // namespace Nomad
