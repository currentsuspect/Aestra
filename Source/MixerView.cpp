// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "MixerView.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadCore/include/NomadLog.h"
#include <algorithm>

namespace Nomad {
namespace Audio {

//=============================================================================
// ChannelStrip Implementation
//=============================================================================

ChannelStrip::ChannelStrip(std::shared_ptr<Track> track, TrackManager* trackManager)
    : m_track(track)
    , m_trackManager(trackManager)
{
    // Create volume fader (vertical slider)
    m_volumeFader = std::make_shared<NomadUI::NUISlider>();
    m_volumeFader->setOrientation(NomadUI::NUISlider::Orientation::Vertical);
    m_volumeFader->setValue(m_track ? m_track->getVolume() : 0.8f);
    m_volumeFader->setOnValueChange([this](double value) {
        if (m_track) {
            m_track->setVolume(static_cast<float>(value));
        }
    });
    addChild(m_volumeFader);
    
    // Create pan knob (horizontal slider for now)
    m_panKnob = std::make_shared<NomadUI::NUISlider>();
    m_panKnob->setOrientation(NomadUI::NUISlider::Orientation::Horizontal);
    m_panKnob->setValue(0.5f);  // Center
    m_panKnob->setOnValueChange([this](double value) {
        if (m_track) {
            m_track->setPan(static_cast<float>((value - 0.5) * 2.0));  // Convert 0..1 to -1..1
        }
    });
    addChild(m_panKnob);
    
    // Create mute button
    m_muteButton = std::make_shared<NomadUI::NUIButton>("M");
    m_muteButton->setToggleable(true);
    m_muteButton->setOnClick([this]() {
        if (m_track) {
            m_track->setMute(!m_track->isMuted());
            m_muteButton->setToggled(m_track->isMuted());
        }
    });
    addChild(m_muteButton);
    
    // Create solo button
    m_soloButton = std::make_shared<NomadUI::NUIButton>("S");
    m_soloButton->setToggleable(true);
    m_soloButton->setOnClick([this]() {
        if (m_track) {
            bool newSolo = !m_track->isSoloed();
            
            // If enabling solo, clear all other solos first (exclusive solo)
            if (newSolo && m_trackManager) {
                m_trackManager->clearAllSolos();
            }
            
            m_track->setSolo(newSolo);
            m_soloButton->setToggled(m_track->isSoloed());
        }
    });
    addChild(m_soloButton);
    
    layoutControls();
}

void ChannelStrip::onRender(NomadUI::NUIRenderer& renderer) {
    auto& theme = NomadUI::NUIThemeManager::getInstance();
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
        renderer.drawText(trackName, NomadUI::NUIPoint(bounds.x + 5, textY), 18.0f, textColor);
    }
    
    // Level meter (simple bar for now) - positioned above track name
    if (m_track) {
        float meterX = bounds.x + bounds.width - 15.0f;
        float meterY = bounds.y + 10.0f;
        float meterWidth = 10.0f;
        float meterHeight = bounds.height - 80.0f;  // Leave space for controls and name
        
        // Meter background
        NomadUI::NUIRect meterBg(meterX, meterY, meterWidth, meterHeight);
        renderer.fillRect(meterBg, NomadUI::NUIColor(0.1f, 0.1f, 0.1f, 1.0f));
        renderer.strokeRect(meterBg, 1.0f, borderColor);
        
        // Get current level from track
        // TODO: Implement proper metering from audio callback
        float level = m_track->getVolume() * 0.5f;  // Placeholder
        
        // Draw level bar (bottom-up)
        if (level > 0.0f) {
            float levelHeight = level * meterHeight;
            NomadUI::NUIRect levelBar(meterX + 1, meterY + meterHeight - levelHeight, meterWidth - 2, levelHeight);
            
            // Color based on level (green -> yellow -> red)
            NomadUI::NUIColor levelColor;
            if (level < 0.7f) {
                levelColor = NomadUI::NUIColor(0.2f, 0.8f, 0.2f, 1.0f);  // Green
            } else if (level < 0.9f) {
                levelColor = NomadUI::NUIColor(0.9f, 0.9f, 0.2f, 1.0f);  // Yellow
            } else {
                levelColor = NomadUI::NUIColor(0.9f, 0.2f, 0.2f, 1.0f);  // Red
            }
            
            renderer.fillRect(levelBar, levelColor);
        }
    }
    
    // Render child controls
    renderChildren(renderer);
}

void ChannelStrip::onResize(int width, int height) {
    NomadUI::NUIComponent::onResize(width, height);
    layoutControls();
}

bool ChannelStrip::onMouseEvent(const NomadUI::NUIMouseEvent& event) {
    return NomadUI::NUIComponent::onMouseEvent(event);
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
        m_muteButton->setBounds(NomadUI::NUIRect(bounds.x + padding, y, controlWidth, buttonHeight));
        y += buttonHeight + padding;
    }
    
    if (m_soloButton) {
        m_soloButton->setBounds(NomadUI::NUIRect(bounds.x + padding, y, controlWidth, buttonHeight));
        y += buttonHeight + padding;
    }
    
    if (m_panKnob) {
        float panHeight = 30.0f;
        m_panKnob->setBounds(NomadUI::NUIRect(bounds.x + padding, y, controlWidth, panHeight));
        y += panHeight + padding;
    }
    
    if (m_volumeFader) {
        float faderHeight = bounds.height - (y - bounds.y) - 30.0f;  // Leave space for track name
        m_volumeFader->setBounds(NomadUI::NUIRect(bounds.x + padding, y, controlWidth - 15.0f, faderHeight));
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

void MixerView::onRender(NomadUI::NUIRenderer& renderer) {
    auto& theme = NomadUI::NUIThemeManager::getInstance();
    auto bounds = getBounds();
    
    // Background
    auto bgColor = theme.getColor("backgroundPrimary");
    renderer.fillRect(bounds, bgColor);
    
    // Render channel strips
    renderChildren(renderer);
}

void MixerView::onResize(int width, int height) {
    NomadUI::NUIComponent::onResize(width, height);
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
    
    // Create channel strip for each track, passing TrackManager for solo coordination
    for (size_t i = 0; i < m_trackManager->getTrackCount(); ++i) {
        auto track = m_trackManager->getTrack(i);
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
        strip->setBounds(NomadUI::NUIRect(x, bounds.y + padding, m_channelWidth, bounds.height - 2 * padding));
        x += m_channelWidth + padding;
    }
}

} // namespace Audio
} // namespace Nomad
