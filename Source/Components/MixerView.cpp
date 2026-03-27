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
    
    // Determine Base Color
    AestraUI::NUIColor baseColor = theme.getColor("backgroundSecondary");
    bool isMaster = false;
    
    if (m_track) {
        // Master Track Check (Name-based "MASTER" or ID 0)
        std::string name = m_track->getName();
        std::transform(name.begin(), name.end(), name.begin(), ::toupper);
        
        if (name == "MASTER" || m_track->getChannelId() == 0) {
            baseColor = theme.getColor("primary"); // Purple
            isMaster = true;
        } else {
            // Priority: Use Playlist Lane Color (Visual Source of Truth)
            bool colorFound = false;
            
            // Try mapped lane
            if (m_laneId.isValid() && m_trackManager) {
                const auto* lane = m_trackManager->getPlaylistModel().getLane(m_laneId);
                // Ensure alpha is fully opaque for the base color extraction
                if (lane && lane->colorRGBA != 0) {
                    baseColor = AestraUI::NUIColor::fromHex(lane->colorRGBA).withAlpha(1.0f);
                    colorFound = true;
                }
            }
            
            // Fallback to internal track color
            if (!colorFound) {
                uint32_t c = m_track->getColor();
                if (c != 0) {
                    baseColor = AestraUI::NUIColor::fromHex(c).withAlpha(1.0f);
                } 
            }
        }
    }

    // 1. Frosted Glass Background (Tinted) - INCREASED OPACITY FOR VISIBILITY
    // Top: Lighter tint, Bottom: Darker tint
    float alphaTop = isMaster ? 0.30f : 0.20f;    
    float alphaBottom = isMaster ? 0.15f : 0.10f;
    
    AestraUI::NUIColor bgTop = baseColor.withAlpha(alphaTop);
    AestraUI::NUIColor bgBottom = baseColor.withAlpha(alphaBottom);
    
    // Mix with deep void to ensure it's not too washed out
    renderer.fillRectGradient(bounds, bgTop, bgBottom, true);
    
    // 2. Glass Border (Tinted)
    float borderAlpha = isMaster ? 0.9f : 0.6f;
    auto borderColor = baseColor.withAlpha(borderAlpha);
    renderer.strokeRect(bounds, 1.0f, borderColor);
    
    // Track name at bottom
    if (m_track) {
        auto textColor = theme.getColor("textSecondary"); // Less distracting name
        std::string trackName = m_track->getName();
        float textY = bounds.y + bounds.height - 25.0f;
        // Center text
        renderer.drawTextCentered(trackName, AestraUI::NUIRect(bounds.x, textY, bounds.width, 20.0f), 12.0f, textColor);
    }
    
    // Level meter (Neon Segmented)
    if (m_track) {
        float meterW = 6.0f;
        float meterRightPad = 8.0f;
        float meterX = bounds.x + bounds.width - meterRightPad - meterW;
        float meterY = bounds.y + 40.0f; // Start below top buttons
        float meterH = bounds.height - 80.0f; // Leave space
        
        // Meter background (Dark Slot)
        AestraUI::NUIRect meterBg(meterX, meterY, meterW, meterH);
        renderer.fillRoundedRect(meterBg, 2.0f, AestraUI::NUIColor(0.05f, 0.05f, 0.08f, 0.8f));
        
        // Get current level (Mock/Placeholder)
        float level = m_track->getVolume(); // Simple mapping
        
        // Draw segments
        int numSegments = 24;
        float segH = (meterH - 2.0f) / numSegments;
        float segGap = 1.0f;
        
        int activeSegments = static_cast<int>(level * numSegments);
        
        for (int i = 0; i < numSegments; ++i) {
            // Index 0 is bottom
            float y = meterY + meterH - ((i + 1) * segH);
            
            bool isActive = i < activeSegments;
            AestraUI::NUIColor segColor;
            
            if (isActive) {
                float n = static_cast<float>(i) / numSegments;
                if (n < 0.7f) segColor = AestraUI::NUIColor::fromHex(0x00ffaa); // Cyan
                else if (n < 0.9f) segColor = AestraUI::NUIColor::fromHex(0xffaa00); // Amber
                else segColor = AestraUI::NUIColor::fromHex(0xff3333); // Red
                segColor = segColor.withAlpha(0.9f);
            } else {
                segColor = AestraUI::NUIColor(0.2f, 0.2f, 0.2f, 0.3f);
            }
            
            renderer.fillRect(AestraUI::NUIRect(meterX + 1.0f, y + segGap, meterW - 2.0f, segH - segGap), segColor);
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
    float padding = 6.0f;
    float controlWidth = bounds.width - 2 * padding;
    
    // Meter takes right side
    float meterWidth = 14.0f; // total meter area
    controlWidth -= meterWidth; // reduce checks
    
    float buttonHeight = 22.0f;
    float y = bounds.y + padding;
    
    // Group: Mute/Solo
    if (m_muteButton && m_soloButton) {
        float btnW = (controlWidth - padding) / 2.0f;
        m_muteButton->setBounds(AestraUI::NUIRect(bounds.x + padding, y, btnW, buttonHeight));
        m_soloButton->setBounds(AestraUI::NUIRect(bounds.x + padding + btnW + padding, y, btnW, buttonHeight));
        y += buttonHeight + padding;
    }
    
    // Pan Knob
    if (m_panKnob) {
        float panHeight = 24.0f; // Compact
        m_panKnob->setBounds(AestraUI::NUIRect(bounds.x + padding, y, controlWidth, panHeight));
        y += panHeight + padding;
    }
    
    // Fader (Vertical, centered in remaining space)
    if (m_volumeFader) {
        float bottomMargin = 30.0f; // Name
        float faderHeight = bounds.height - (y - bounds.y) - bottomMargin;
        // Center fader in control area
        m_volumeFader->setBounds(AestraUI::NUIRect(bounds.x + padding, y, controlWidth, faderHeight));
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
    
    // Deep Void Background
    auto bgColor = AestraUI::NUIColor::fromHex(0x050508); // Matches Playlist
    renderer.fillRect(bounds, bgColor);
    
    // Subtle Top Gradient (Header Shadow)
    renderer.fillRectGradient(
        AestraUI::NUIRect(bounds.x, bounds.y, bounds.width, 20.0f),
        AestraUI::NUIColor(0,0,0,0.4f),
        AestraUI::NUIColor(0,0,0,0.0f),
        true
    );
    
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
    auto& playlist = m_trackManager->getPlaylistModel();
    
    // Create channel strip for each track
    // Assume 1:1 mapping between Playlist Lanes and non-skipped Mixer Channels for now
    // (excluding Master which is special case ID 0)
    size_t laneIndex = 0;
    
    for (const auto& track : channelsSnapshot) {
        if (track && track->getName() != "Preview") {  // Skip preview track
            auto channelStrip = std::make_shared<ChannelStrip>(std::shared_ptr<MixerChannel>(track, [](MixerChannel*){}), m_trackManager.get());
            
            // Map to playlist lane if it's a regular track
            if (track->getChannelId() != 0) {
                if (laneIndex < playlist.getLaneCount()) {
                     channelStrip->setPlaylistLaneID(playlist.getLaneId(laneIndex));
                     laneIndex++;
                }
            }
            
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
