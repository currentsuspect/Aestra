// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "MixerView.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraCore/include/AestraLog.h"
#include "../AestraCore/include/AestraUnifiedProfiler.h"
#include <algorithm>

namespace Aestra {
namespace Audio {

//=============================================================================
// ChannelStrip Implementation
//=============================================================================

ChannelStrip::ChannelStrip(std::shared_ptr<Track> track, TrackManager* trackManager)
    : m_track(track)
    , m_trackManager(trackManager)
{
    // Create volume fader (vertical slider)
    m_volumeFader = std::make_shared<AestraUI::Fader>();
    m_volumeFader->setValue(m_track ? m_track->getVolume() : 0.8f);
    m_volumeFader->setOnValueChange([this](double value) {
        if (m_track) {
            float vol = static_cast<float>(value);
            m_track->setVolume(vol);
        }
    });
    addChild(m_volumeFader);
    
    // Create pan knob (horizontal slider for now)
    m_panKnob = std::make_shared<AestraUI::PanKnob>();
    m_panKnob->setValue(0.0f);  // Center
    m_panKnob->setOnValueChange([this](double value) {
        if (m_track) {
            float pan = static_cast<float>(value);
            m_track->setPan(pan);
        }
    });
    addChild(m_panKnob);
    
    // Create mute button
    m_muteButton = std::make_shared<AestraUI::MuteButton>();
    m_muteButton->setOnToggle([this](bool toggled) {
        if (m_track) {
            m_track->setMute(toggled);
            // m_muteButton->setOn(m_track->isMuted()); // Already set by toggle
        }
    });
    // Initialize state
    if (m_track) m_muteButton->setOn(m_track->isMuted());
    addChild(m_muteButton);
    
    // Create solo button
    m_soloButton = std::make_shared<AestraUI::SoloButton>();
    m_soloButton->setOnToggle([this](bool toggled) {
        if (m_track) {
            bool newSolo = toggled;
            
            // If enabling solo, clear all other solos first (exclusive solo)
            if (newSolo && m_trackManager) {
                m_trackManager->clearAllSolos();
            }
            
            m_track->setSolo(newSolo);
            // m_soloButton->setOn(m_track->isSoloed()); // Already set by toggle
        }
    });
    // Initialize state
    if (m_track) m_soloButton->setOn(m_track->isSoloed());
    addChild(m_soloButton);
    
    layoutControls();
}

void ChannelStrip::onRender(AestraUI::NUIRenderer& renderer) {
    AESTRA_ZONE("ChannelStrip_Render");
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    auto bounds = getBounds();
    
    // Background
    auto bgColor = theme.getColor("backgroundSecondary");
    renderer.fillRect(bounds, bgColor);
    
    // Border
    auto borderColor = theme.getColor("border");
    renderer.strokeRect(bounds, 1.0f, borderColor);
    
    // Track name at bottom
    if (m_track) {
        auto textColor = theme.getColor("accentPrimary");
        std::string trackName = m_track->getName();
        float textY = bounds.y + bounds.height - 30.0f;
        renderer.drawText(trackName, AestraUI::NUIPoint(bounds.x + 5, textY), 12.0f, textColor);
    }
    
    // Level meter (simple bar for now) - positioned above track name
    if (m_track) {
        float meterX = bounds.x + bounds.width - 15.0f;
        float meterY = bounds.y + 10.0f;
        float meterWidth = 10.0f;
        float meterHeight = bounds.height - 80.0f;  // Leave space for controls and name
        
        // Meter background
        AestraUI::NUIRect meterBg(meterX, meterY, meterWidth, meterHeight);
        renderer.fillRect(meterBg, AestraUI::NUIColor(0.1f, 0.1f, 0.1f, 1.0f));
        renderer.strokeRect(meterBg, 1.0f, borderColor);
        
        // Get current level from track
        // TODO: Implement proper metering from audio callback
        float level = m_track->getVolume() * 0.5f;  // Placeholder
        
        // Draw level bar (bottom-up)
        if (level > 0.0f) {
            float levelHeight = level * meterHeight;
            AestraUI::NUIRect levelBar(meterX + 1, meterY + meterHeight - levelHeight, meterWidth - 2, levelHeight);
            
            // Color based on level (green -> yellow -> red)
            AestraUI::NUIColor levelColor;
            if (level < 0.7f) {
                levelColor = AestraUI::NUIColor(0.2f, 0.8f, 0.2f, 1.0f);  // Green
            } else if (level < 0.9f) {
                levelColor = AestraUI::NUIColor(0.9f, 0.9f, 0.2f, 1.0f);  // Yellow
            } else {
                levelColor = AestraUI::NUIColor(0.9f, 0.2f, 0.2f, 1.0f);  // Red
            }
            
            renderer.fillRect(levelBar, levelColor);
        }
    }
    
    // Render child controls
    renderChildren(renderer);
}

void ChannelStrip::onResize(int width, int height) {
    AestraUI::NUIComponent::onResize(width, height);
    layoutControls();
}

bool ChannelStrip::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    return AestraUI::NUIComponent::onMouseEvent(event);
}

void ChannelStrip::layoutControls() {
    auto bounds = getBounds();
    float padding = 5.0f;
    float controlWidth = bounds.width - 2 * padding;
    float buttonHeight = 20.0f;
    
    // Layout from top to bottom:
    // - Mute button
    // - Solo button
    // - Pan knob
    // - Volume fader (takes most space)
    // - Track name (rendered in onRender)
    
    float y = bounds.y + padding;
    
    if (m_muteButton) {
        m_muteButton->setBounds(AestraUI::NUIRect(bounds.x + padding, y, controlWidth, buttonHeight));
        y += buttonHeight + padding;
    }
    
    if (m_soloButton) {
        m_soloButton->setBounds(AestraUI::NUIRect(bounds.x + padding, y, controlWidth, buttonHeight));
        y += buttonHeight + padding;
    }
    
    if (m_panKnob) {
        float panHeight = 30.0f;
        m_panKnob->setBounds(AestraUI::NUIRect(bounds.x + padding, y, controlWidth, panHeight));
        y += panHeight + padding;
    }
    
    if (m_volumeFader) {
        float faderHeight = bounds.height - (y - bounds.y) - 30.0f;  // Leave space for track name
        m_volumeFader->setBounds(AestraUI::NUIRect(bounds.x + padding, y, controlWidth - 15.0f, faderHeight));
    }
}

//=============================================================================
// MixerView Implementation
//=============================================================================

MixerView::MixerView(std::shared_ptr<TrackManager> trackManager)
    : m_trackManager(trackManager)
{
    refreshChannels();
}

void MixerView::onRender(AestraUI::NUIRenderer& renderer) {
    AESTRA_ZONE("Mixer_Render");
    
    // Skip rendering if not visible
    if (!isVisible()) return;
    
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    auto bounds = getBounds();
    
    // Background
    auto bgColor = theme.getColor("backgroundPrimary");
    renderer.fillRect(bounds, bgColor);
    
    // Render channel strips
    renderChildren(renderer);
}

void MixerView::onResize(int width, int height) {
    AestraUI::NUIComponent::onResize(width, height);
    layoutChannels();
}

void MixerView::refreshChannels() {
    // Remove old channel strips from parent before clearing
    for (auto& strip : m_channelStrips) {
        removeChild(strip);
    }
    
    // Clear existing channel strips
    m_channelStrips.clear();
    
    if (!m_trackManager) return;
    
    // THREAD-SAFE: Get a snapshot of channels to avoid race with Audio Thread
    auto channelsSnapshot = m_trackManager->getChannelsSnapshot();
    
    // Create channel strip for each track, passing TrackManager for solo coordination
    for (const auto& track : channelsSnapshot) {
        if (track && track->getName() != "Preview") {  // Skip preview track
            auto channelStrip = std::make_shared<ChannelStrip>(track, m_trackManager.get());
            m_channelStrips.push_back(channelStrip);
            addChild(channelStrip);
        }
    }
    
    layoutChannels();
    Log::info("Mixer: Created " + std::to_string(m_channelStrips.size()) + " channel strips");
}

void MixerView::layoutChannels() {
    auto bounds = getBounds();
    float padding = 5.0f;
    float x = bounds.x + padding - m_scrollOffset;
    
    for (auto& strip : m_channelStrips) {
        strip->setBounds(AestraUI::NUIRect(x, bounds.y + padding, m_channelWidth, bounds.height - 2 * padding));
        x += m_channelWidth + padding;
    }
}

} // namespace Audio
} // namespace Aestra
