// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "AuditionPanel.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraCore/include/AestraLog.h"
#include "ClipSource.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <algorithm>

// Forward declare stb_image functions (implementation in AestraUI)
extern "C" {
    unsigned char* stbi_load_from_memory(const unsigned char* buffer, int len, int* x, int* y, int* comp, int req_comp);
    void stbi_image_free(void* retval_from_stbi_load);
}

namespace Aestra {

// ============================================================================
// SVG ICONS
// ============================================================================
const std::string SVG_PLAY = "<svg viewBox=\"0 0 24 24\"><path d=\"M8 5v14l11-7z\"/></svg>";
const std::string SVG_PAUSE = "<svg viewBox=\"0 0 24 24\"><path d=\"M6 19h4V5H6v14zm8-14v14h4V5h-4z\"/></svg>";
const std::string SVG_PREV = "<svg viewBox=\"0 0 24 24\"><path d=\"M6 6h2v12H6zm3.5 6l8.5 6V6z\"/></svg>";
const std::string SVG_NEXT = "<svg viewBox=\"0 0 24 24\"><path d=\"M6 18l8.5-6L6 6v12zM16 6v12h2V6h-2z\"/></svg>";


// ============================================================================
// CONSTRUCTOR
// ============================================================================

AuditionPanel::AuditionPanel(std::shared_ptr<Audio::AuditionEngine> engine)
    : m_engine(std::move(engine))
{
    setId("AuditionPanel");
    
    setupComponents();
    
    // Wire up engine callbacks
    if (m_engine) {
        m_engine->setOnTrackChanged([this](const Audio::AuditionQueueItem& item) {
            if (m_trackTitle) m_trackTitle->setText(item.title);
            if (m_trackArtist) m_trackArtist->setText(item.artist);
            Log::info("[AuditionPanel] Track changed: " + item.title);
        });
        
        m_engine->setOnPlaybackStateChanged([this](bool isPlaying) {
            // Visual update handled by SVG swap in onRender
            // if (m_playPauseButton) {
            //     m_playPauseButton->setText(isPlaying ? "PAUSE" : "PLAY");
            // }
        });
    }
    
    Log::info("[AuditionPanel] Created");
}

AuditionPanel::~AuditionPanel() {
    AestraUI::NUIDragDropManager::getInstance().unregisterDropTarget(this);
}

// ============================================================================
// COMPONENT SETUP
// ============================================================================

void AuditionPanel::setupComponents() {
    // 1. Text Labels
    m_trackTitle = std::make_shared<AestraUI::NUILabel>("No Track Selected");
    m_trackTitle->setFontSize(28.0f);
    m_trackTitle->setAlignment(AestraUI::NUILabel::Alignment::Left);
    m_trackTitle->setTextColor(AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 1.0f));
    addChild(m_trackTitle);
    
    m_trackArtist = std::make_shared<AestraUI::NUILabel>("Drag files to start");
    m_trackArtist->setFontSize(16.0f);
    m_trackArtist->setTextColor(AestraUI::NUIColor(0.7f, 0.7f, 0.7f, 1.0f));
    m_trackArtist->setAlignment(AestraUI::NUILabel::Alignment::Left);
    addChild(m_trackArtist);
    
    m_currentTime = std::make_shared<AestraUI::NUILabel>("0:00");
    m_currentTime->setFontSize(12.0f);
    m_currentTime->setTextColor(AestraUI::NUIColor(0.6f, 0.6f, 0.6f, 1.0f));
    addChild(m_currentTime);
    
    m_totalTime = std::make_shared<AestraUI::NUILabel>("0:00");
    m_totalTime->setFontSize(12.0f);
    m_totalTime->setAlignment(AestraUI::NUILabel::Alignment::Right);
    m_totalTime->setTextColor(AestraUI::NUIColor(0.6f, 0.6f, 0.6f, 1.0f));
    addChild(m_totalTime);
    
    // 2. Transport Buttons (Text cleared for SVG overlap)
    m_playPauseButton = std::make_shared<AestraUI::NUIButton>(""); 
    m_playPauseButton->setStyle(AestraUI::NUIButton::Style::Primary);
    m_playPauseButton->setCornerRadius(28.0f);
    m_playPauseButton->setOnClick([this]() {
        if (m_engine) {
             if (!m_engine->isPlaying()) {
                 if (m_onPlayRequest) m_onPlayRequest(); // Stop external preview
             }
             m_engine->togglePlayPause();
        }
    });
    addChild(m_playPauseButton);
    
    m_prevButton = std::make_shared<AestraUI::NUIButton>("");
    m_prevButton->setStyle(AestraUI::NUIButton::Style::Icon);
    m_prevButton->setOnClick([this]() { if (m_engine) m_engine->previousTrack(); });
    addChild(m_prevButton);
    
    m_nextButton = std::make_shared<AestraUI::NUIButton>("");
    m_nextButton->setStyle(AestraUI::NUIButton::Style::Icon);
    m_nextButton->setOnClick([this]() { if (m_engine) m_engine->nextTrack(); });
    addChild(m_nextButton);
    
    // 3. DSP Buttons
    m_dspPresetButton = std::make_shared<AestraUI::NUIButton>("Studio Reference");
    m_dspPresetButton->setStyle(AestraUI::NUIButton::Style::Secondary);
    m_dspPresetButton->setOnClick([this]() { 
        if (!m_engine) return;
        
        // Define cycling order
        static const std::vector<Aestra::Audio::AuditionDSPPreset> presets = {
            Aestra::Audio::AuditionDSPPreset::Bypass(),
            Aestra::Audio::AuditionDSPPreset::Spotify(),
            Aestra::Audio::AuditionDSPPreset::AppleMusic(),
            Aestra::Audio::AuditionDSPPreset::YouTube(),
            Aestra::Audio::AuditionDSPPreset::SoundCloud(),
            Aestra::Audio::AuditionDSPPreset::CarSpeakers(),
            Aestra::Audio::AuditionDSPPreset::AirPods()
        };
        
        // Find current and set next
        auto current = m_engine->getDSPPreset();
        size_t nextIndex = 0;
        
        for (size_t i = 0; i < presets.size(); ++i) {
            if (presets[i].name == current.name) {
                nextIndex = (i + 1) % presets.size();
                break;
            }
        }
        
        const auto& nextPreset = presets[nextIndex];
        m_engine->setDSPPreset(nextPreset);
        m_dspPresetButton->setText(nextPreset.name);
        
        Log::info("[AuditionPanel] Switched DSP Preset to: " + nextPreset.name);
    });
    addChild(m_dspPresetButton);
    
    m_abToggleButton = std::make_shared<AestraUI::NUIButton>("A/B");
    m_abToggleButton->setStyle(AestraUI::NUIButton::Style::Secondary);
    m_abToggleButton->setToggleable(true);
    m_abToggleButton->setOnToggle([this](bool active) {
        if (m_engine) m_engine->setABMode(active);
    });
    addChild(m_abToggleButton);
    
    // 4. Sliders
    m_progressSlider = std::make_shared<AestraUI::NUISlider>();
    // We do NOT add m_progressSlider as child anymore - Waveform is the scrubber!
    
    m_volumeSlider = std::make_shared<AestraUI::NUISlider>();
    m_volumeSlider->setValue(1.0f);
    m_volumeSlider->setOnValueChange([this](double val) { // Fixed: setOnValueChange, double
        if (m_engine) m_engine->setVolume(static_cast<float>(val));
    });
    addChild(m_volumeSlider);
    
    // 5. Parse SVGs
    m_svgPlay = AestraUI::NUISVGParser::parse(SVG_PLAY);
    m_svgPause = AestraUI::NUISVGParser::parse(SVG_PAUSE);
    m_svgPrev = AestraUI::NUISVGParser::parse(SVG_PREV);
    m_svgNext = AestraUI::NUISVGParser::parse(SVG_NEXT);
    
    // Registrations
    if (m_engine) {
        m_engine->setOnPlaybackStateChanged([this](bool playing) {
            // Button visual update handled in onRender via SVG swap
            // No text update here to avoid cluttering the SVG
        });
    }
}
// ... [Lines 185-385 unchanged] ...
// ============================================================================
// QUEUE RENDERING (WITH HOVER)
// ============================================================================

void AuditionPanel::renderQueue(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& area) {
    if (!m_engine) return;
    
    auto& theme = AestraUI::NUIThemeManager::getInstance();

    // Headers
    float headerH = 28.0f;
    AestraUI::NUIColor headerTxtColor(0.5f, 0.5f, 0.5f, 1.0f);
    float colNo = area.x + 12.0f;
    float colTitle = area.x + 60.0f;
    float colTime = area.x + area.width - 60.0f;
    
    // Header divider
    // renderer.drawText("#", AestraUI::NUIPoint(colNo, area.y + 8.0f), 11.0f, headerTxtColor);
    // renderer.drawText("TITLE", AestraUI::NUIPoint(colTitle, area.y + 8.0f), 11.0f, headerTxtColor);
    // renderer.drawText("TIME", AestraUI::NUIPoint(colTime, area.y + 8.0f), 11.0f, headerTxtColor);
    // renderer.drawLine(AestraUI::NUIPoint(area.x, area.y + headerH), AestraUI::NUIPoint(area.x + area.width, area.y + headerH), 1.0f, AestraUI::NUIColor(1.0f,1.0f,1.0f,0.05f));
    
    // Items
    const auto& queue = m_engine->getQueue();
    auto currentItem = m_engine->getCurrentItem();
    
    // Scroll handling would go here (offset)
    float y = area.y; // + headerH;
    float rowH = 32.0f;
    float spacing = 4.0f;
    
    for (size_t i = 0; i < queue.size(); ++i) {
        if (y + rowH > area.y + area.height) break; // Clip
        
        const auto& item = queue[i];
        bool isCurrent = (currentItem && currentItem->id == item.id);
        bool isHovered = (static_cast<int>(i) == m_hoveredQueueIndex);
        
        AestraUI::NUIRect rowRect(area.x, y, area.width, rowH);
        
        // Background
        if (isCurrent) {
            // Active Glass Gradient
            AestraUI::NUIColor start = AestraUI::NUIColor(0.1f, 0.1f, 0.15f, 0.9f);
            AestraUI::NUIColor end = AestraUI::NUIColor(0.15f, 0.15f, 0.22f, 0.8f);
            
            // Draw gradient background
             for(int j=0; j<4; ++j) {
                 float f = j/3.0f;
                 AestraUI::NUIRect r = rowRect;
                 r.y += j * (rowRect.height/4.0f);
                 r.height = rowRect.height/4.0f;
                 renderer.fillRoundedRect(r, 6.0f, AestraUI::NUIColor::lerp(start, end, f));
             }
             
            // Neon Border
            renderer.strokeRoundedRect(rowRect, 6.0f, 1.0f, theme.getColor("primary")); 
            
            // Inner Highlight
            AestraUI::NUIRect innerRect = rowRect;
            innerRect.x += 1.0f; innerRect.y += 1.0f;
            innerRect.width -= 2.0f; innerRect.height -= 2.0f;
            renderer.strokeRoundedRect(innerRect, 5.0f, 1.0f, theme.getColor("primary").withAlpha(0.3f));
            
        } else if (isHovered) {
             renderer.fillRoundedRect(rowRect, 6.0f, AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.08f)); // Hover light
        } else {
             // Alternating subtle
             if (i % 2 == 0) renderer.fillRoundedRect(rowRect, 6.0f, AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.01f));
        }

        // Icon Column Logic
        if (isHovered) {
             renderer.drawText("▶", AestraUI::NUIPoint(colNo - 3.0f, y + 8.0f), 11.0f, theme.getColor("textPrimary"));
        } else if (isCurrent) {
             // Speaker icon or waveform icon
             renderer.drawText("ılı", AestraUI::NUIPoint(colNo - 1.0f, y + 8.0f), 11.0f, theme.getColor("primary")); 
        } else {
             renderer.drawText(std::to_string(i + 1), AestraUI::NUIPoint(colNo, y + 8.0f), 11.0f, theme.getColor("textTertiary"));
        }
        
        // Title
        AestraUI::NUIColor titleColor = isCurrent ? 
            theme.getColor("primary") : 
            theme.getColor("textPrimary");
        renderer.drawText(item.title, AestraUI::NUIPoint(colTitle, y + 8.0f), 13.0f, titleColor);
        
        // Time
        std::string timeStr = (item.durationSeconds > 0.0) ? formatTime(item.durationSeconds) : "--:--";
        renderer.drawText(timeStr, AestraUI::NUIPoint(colTime, y + 8.0f), 11.0f, theme.getColor("textSecondary"));
        
        y += rowH + spacing;
    }
}
// ============================================================================
// LAYOUT - Called from onResize
// ============================================================================

// ============================================================================
// LAYOUT - Called from onResize
// ============================================================================

void AuditionPanel::layoutComponents() {
    auto bounds = getBounds();
    const float padding = 20.0f;
    const float gap = 16.0f;
    
    // Fixed heights matching onRender
    float headerHeight = 220.0f;
    float waveformHeight = 120.0f;
    
    AestraUI::NUIRect headerRect(bounds.x + padding, bounds.y + padding, bounds.width - padding*2, headerHeight);
    AestraUI::NUIRect waveformRect(bounds.x + padding, headerRect.bottom() + gap, bounds.width - padding*2, waveformHeight);
    
    // === 1. Header Layout ===
    float artPadding = 20.0f;
    float artSize = headerHeight - (artPadding * 2);
    if (artSize > 180.0f) artSize = 180.0f;
    
    float infoX = headerRect.x + artPadding + artSize + 30.0f;
    float infoW = headerRect.width - (artPadding + artSize + 30.0f) - artPadding;
    float startY = headerRect.y + artPadding + 10.0f;
    
    // Title
    m_trackTitle->setFontSize(32.0f); // clean size
    m_trackTitle->setBounds(AestraUI::NUIAbsolute(bounds, infoX, startY, infoW, 40.0f));
    
    // Artist
    m_trackArtist->setFontSize(18.0f);
    // m_trackArtist->setFontWeight(AestraUI::FontWeight::Light); // Not supported
    m_trackArtist->setBounds(AestraUI::NUIAbsolute(bounds, infoX, startY + 45.0f, infoW, 24.0f));
    
    // Controls - Bottom aligned in header info area
    float controlsY = headerRect.bottom() - artPadding - 64.0f - 12.0f; // Shift UP by 12px due to user request (was clipping bottom)
    
    // Play Button Group
    float playSize = 56.0f;
    float navSize = 36.0f;
    float navGap = 12.0f;
    
    m_prevButton->setBounds(AestraUI::NUIAbsolute(bounds, infoX, controlsY + (playSize-navSize)/2, navSize, navSize));
    // Glass Style for Prev
    m_prevButton->setBackgroundColor(AestraUI::NUIColor(0.1f, 0.1f, 0.15f, 0.4f));
    m_prevButton->setBorderColor(AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.1f));
    m_prevButton->setBorderWidth(1.0f);
    m_prevButton->setCornerRadius(navSize/2.0f);

    m_playPauseButton->setBounds(AestraUI::NUIAbsolute(bounds, infoX + navSize + navGap, controlsY, playSize, playSize));
    // Glass Style for Play (Slightly brighter/different?)
    // Let's make it consistent but maybe slightly more opaque or accented if playing? 
    // For now, consistent Glass Base.
    m_playPauseButton->setBackgroundColor(AestraUI::NUIColor(0.1f, 0.1f, 0.15f, 0.4f));
    m_playPauseButton->setBorderColor(AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.1f));
    m_playPauseButton->setBorderWidth(1.0f);
    m_playPauseButton->setCornerRadius(playSize/2.0f);

    m_nextButton->setBounds(AestraUI::NUIAbsolute(bounds, infoX + navSize + navGap + playSize + navGap, controlsY + (playSize-navSize)/2, navSize, navSize));
    // Glass Style for Next
    m_nextButton->setBackgroundColor(AestraUI::NUIColor(0.1f, 0.1f, 0.15f, 0.4f));
    m_nextButton->setBorderColor(AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.1f));
    m_nextButton->setBorderWidth(1.0f);
    m_nextButton->setCornerRadius(navSize/2.0f);
    
    // DSP & Volume Group (Right aligned or Next to transport)
    float extraX = infoX + navSize + navGap + playSize + navGap + navSize + 40.0f;
    
    m_dspPresetButton->setBounds(AestraUI::NUIAbsolute(bounds, extraX, controlsY + 12.0f, 120.0f, 32.0f));
    // Apply Glass Style to DSP Button
    // Apply Glass Style to DSP Button - Match Transport Style
    m_dspPresetButton->setBackgroundColor(AestraUI::NUIColor(0.12f, 0.12f, 0.22f, 0.4f));
    m_dspPresetButton->setBorderColor(AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.25f));
    m_dspPresetButton->setBorderWidth(1.0f);
    m_dspPresetButton->setCornerRadius(16.0f);

    m_abToggleButton->setBounds(AestraUI::NUIAbsolute(bounds, extraX + 130.0f, controlsY + 12.0f, 50.0f, 32.0f));
    // Apply Glass Style to A/B Button
    // Apply Glass Style to A/B Button - Match Transport Style
    m_abToggleButton->setBackgroundColor(AestraUI::NUIColor(0.12f, 0.12f, 0.22f, 0.4f));
    m_abToggleButton->setBorderColor(AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.25f));
    m_abToggleButton->setBorderWidth(1.0f);
    m_abToggleButton->setCornerRadius(16.0f);
    
    // Volume - Mini slider
    m_volumeSlider->setBounds(AestraUI::NUIAbsolute(bounds, extraX + 200.0f, controlsY + 24.0f, 80.0f, 6.0f));
    
    // === 2. Waveform Info ===
    // Time labels inside waveform panel, bottom corners
    float timeY = waveformRect.bottom() - 24.0f;
    m_currentTime->setBounds(AestraUI::NUIAbsolute(bounds, waveformRect.x + 12.0f, timeY, 60.0f, 16.0f));
    m_totalTime->setBounds(AestraUI::NUIAbsolute(bounds, waveformRect.right() - 72.0f, timeY, 60.0f, 16.0f));
}

// ============================================================================
// LIFECYCLE OVERRIDES
// ============================================================================

void AuditionPanel::onResize(int width, int height) {
    NUIComponent::onResize(width, height);
    layoutComponents();
}

void AuditionPanel::onUpdate(double deltaTime) {
    m_animationTime += static_cast<float>(deltaTime);
    
    // Drops
    if (!m_dropTargetRegistered) {
        try {
            auto sharedThis = std::dynamic_pointer_cast<AestraUI::IDropTarget>(shared_from_this());
            if (sharedThis) {
                AestraUI::NUIDragDropManager::getInstance().registerDropTarget(sharedThis);
                m_dropTargetRegistered = true;
            }
        } catch (const std::bad_weak_ptr&) {}
    }
    
    // Update time
    if (m_engine) {
        double pos = m_engine->getPositionSeconds();
        double dur = m_engine->getDurationSeconds();
        m_currentTime->setText(formatTime(pos));
        m_totalTime->setText(formatTime(dur));
    }
    
    NUIComponent::onUpdate(deltaTime);
}



void AuditionPanel::onRender(AestraUI::NUIRenderer& renderer) {
    auto bounds = getBounds();

    // === 0. Update Logic for Cover Art ===
    bool hasCoverArt = false;
    if (m_engine) {
        auto item = m_engine->getCurrentItem();
        if (item && item->id != m_currentTrackId) {
            m_currentTrackId = item->id;
            if (m_coverArtTextureId != 0) {
                renderer.deleteTexture(m_coverArtTextureId);
                m_coverArtTextureId = 0;
            }
            if (!item->coverArtData.empty()) {
                int w, h, comp;
                unsigned char* rgba = stbi_load_from_memory(item->coverArtData.data(), static_cast<int>(item->coverArtData.size()), &w, &h, &comp, 4);
                if (rgba) {
                    m_coverArtTextureId = renderer.createTexture(rgba, w, h);
                    m_coverArtWidth = w; m_coverArtHeight = h;
                    int cx = w / 2; int cy = h / 2; int idx = (cy * w + cx) * 4;
                    float r = rgba[idx] / 255.0f; float g = rgba[idx + 1] / 255.0f; float b = rgba[idx + 2] / 255.0f;
                    m_currentHeaderColor = AestraUI::NUIColor(r * 0.5f, g * 0.5f, b * 0.5f, 1.0f);
                    stbi_image_free(rgba);
                }
            }
        }
        hasCoverArt = (m_coverArtTextureId != 0);
    }
    
    // === 1. Base Background (Void) ===
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    renderer.fillRect(bounds, theme.getColor("backgroundPrimary"));
    
    const float padding = 20.0f;
    const float gap = 16.0f;
    float headerHeight = 220.0f;
    float waveformHeight = 120.0f;
    
    AestraUI::NUIRect headerRect(bounds.x + padding, bounds.y + padding, bounds.width - padding*2, headerHeight);
    AestraUI::NUIRect waveformRect(bounds.x + padding, headerRect.bottom() + gap, bounds.width - padding*2, waveformHeight);
    AestraUI::NUIRect queueRect(bounds.x + padding, waveformRect.bottom() + gap, bounds.width - padding*2, bounds.height - waveformRect.bottom() - gap - padding);

    // === 2. Panels ===
    // Header
    renderer.drawShadow(headerRect, 0, 4, 16, AestraUI::NUIColor(0,0,0,0.5f));
    renderer.fillRoundedRect(headerRect, 12.0f, theme.getColor("surfaceTertiary"));
    renderer.strokeRoundedRect(headerRect, 12.0f, 1.0f, theme.getColor("border"));
    if (m_currentHeaderColor.a > 0.1f) {
         AestraUI::NUIColor tint = m_currentHeaderColor; tint.a = 0.05f;
         renderer.fillRoundedRect(headerRect, 12.0f, tint);
    }
    // Waveform
    renderer.drawShadow(waveformRect, 0, 4, 16, AestraUI::NUIColor(0,0,0,0.5f));
    renderer.fillRoundedRect(waveformRect, 12.0f, theme.getColor("surfaceTertiary"));
    renderer.strokeRoundedRect(waveformRect, 12.0f, 1.0f, theme.getColor("border"));
    // Queue
    renderer.drawShadow(queueRect, 0, 4, 16, AestraUI::NUIColor(0,0,0,0.5f));
    renderer.fillRoundedRect(queueRect, 12.0f, theme.getColor("surfaceTertiary"));
    renderer.strokeRoundedRect(queueRect, 12.0f, 1.0f, theme.getColor("border"));
    
    // === 3. Cover Art ===
    if (headerRect.width > 50) {
        float artPadding = 20.0f;
        float artSize = headerHeight - (artPadding * 2);
        if (artSize > 180.0f) artSize = 180.0f;
        
        AestraUI::NUIRect artRect(headerRect.x + artPadding, headerRect.y + artPadding, artSize, artSize);
        
        if (hasCoverArt) {
            AestraUI::NUIRect srcRect(0, 0, static_cast<float>(m_coverArtWidth), static_cast<float>(m_coverArtHeight));
            renderer.setClipRect(artRect);
            renderer.drawTexture(m_coverArtTextureId, artRect, srcRect);
            renderer.clearClipRect();
            renderer.strokeRoundedRect(artRect, 8.0f, 1.0f, AestraUI::NUIColor(1.0f,1.0f,1.0f,0.1f));
        } else {
             AestraUI::NUIColor artFill(m_currentHeaderColor.r * 0.8f, m_currentHeaderColor.g * 0.8f, m_currentHeaderColor.b * 0.8f, 1.0f);
             renderer.fillRoundedRect(artRect, 8.0f, artFill);
             renderer.drawText("AESTRA", AestraUI::NUIPoint(artRect.x + artSize/2.0f - 24.0f, artRect.y + artSize/2.0f - 6.0f), 14.0f, AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.5f));
        }
    }
    
    // === 4. Content Calls ===
    AestraUI::NUIRect waveformInner = waveformRect;
    float wInset = 20.0f;
    waveformInner.x += wInset; waveformInner.y += wInset; 
    waveformInner.width -= wInset*2; 
    // Reserve space for time pills at the bottom (extra 24px)
    waveformInner.height -= (wInset + 24.0f);
    renderWaveform(renderer, waveformInner);
    
    AestraUI::NUIRect queueInner = queueRect;
    float qInset = 12.0f;
    queueInner.x += qInset; queueInner.y += qInset;
    queueInner.width -= qInset*2; queueInner.height -= qInset*2; 
    renderQueue(renderer, queueInner);
    

    // === Render Time Label Backgrounds (Pill shape) in Reserved Bottom Space ===
    // Use the reserved space: waveformRect bottom + padding
    float timePillY = waveformInner.bottom() + 2.0f; // Moved up by 4px (was +6.0f)
    float pillW = 70.0f;
    float pillH = 22.0f;
    
    // Calculate precise center based on layout
    float totalTimeWidth = (pillW * 2) + 40.0f; // 2 pills + gap
    float startX = waveformRect.x + (waveformRect.width - totalTimeWidth) / 2.0f;
    
    AestraUI::NUIRect currentPillRest(startX, timePillY, pillW, pillH);
    AestraUI::NUIRect totalPillRect(startX + pillW + 40.0f, timePillY, pillW, pillH);
    
    if (m_currentTime) {
        std::string timeStr = "0:00";
        double pos = 0.0;
        if (m_engine) pos = m_engine->getPositionSeconds();
        int mins = static_cast<int>(pos / 60.0);
        int secs = static_cast<int>(pos) % 60;
        std::stringstream ss;
        ss << mins << ":" << std::setfill('0') << std::setw(2) << secs;
        m_currentTime->setText(ss.str());
        
        // Use full pill bounds and Center alignment
        m_currentTime->setBounds(currentPillRest);
        m_currentTime->setAlignment(AestraUI::NUILabel::Alignment::Center);
        m_currentTime->setBackgroundVisible(false); // We draw manually for glass effect
        
        // Draw Glass Pill Background
        renderer.fillRoundedRect(currentPillRest, 11.0f, AestraUI::NUIColor(0,0,0,0.5f)); // Dark glass
        renderer.strokeRoundedRect(currentPillRest, 11.0f, 1.0f, AestraUI::NUIColor(1.0f,1.0f,1.0f,0.1f)); // Subtle border
    }

    if (m_totalTime) {
        std::string timeStr = "0:00";
        double dur = 0.0;
        if (m_engine) dur = m_engine->getDurationSeconds();
        int mins = static_cast<int>(dur / 60.0);
        int secs = static_cast<int>(dur) % 60;
        std::stringstream ss;
        ss << mins << ":" << std::setfill('0') << std::setw(2) << secs;
        m_totalTime->setText(ss.str());
        
        m_totalTime->setBounds(totalPillRect);
        m_totalTime->setAlignment(AestraUI::NUILabel::Alignment::Center);
         m_totalTime->setBackgroundVisible(false);
        
        // Draw Glass Pill Background
        renderer.fillRoundedRect(totalPillRect, 11.0f, AestraUI::NUIColor(0,0,0,0.5f));
        renderer.strokeRoundedRect(totalPillRect, 11.0f, 1.0f, AestraUI::NUIColor(1.0f,1.0f,1.0f,0.1f));
    }
    

     // m_currentTime->onRender(renderer); // REMOVED: Handled by renderChildren()
     // m_totalTime->onRender(renderer);   // REMOVED: Handled by renderChildren()

    // === Overlay SVGs on Buttons ===
    // Force White/Primary color for visibility against filled buttons
    AestraUI::NUIColor iconColor(1.0f, 1.0f, 1.0f, 1.0f);
    
    // Manual background fill for Icon buttons REMOVED (Handled by NUIButton now with renderChildren)
    
    // Note: We still render icons manually because NUIButton's internal setIcon might not be used here yet.
    // If we call renderChildren(), it will draw the NUIButton background/border.
    // Then we draw the icon on top.
    
    // RENDER CHILD COMPONENTS (Buttons, Labels, Slider) on top of panels
    renderChildren(renderer);

    // Prev
    {
        auto btnBounds = m_prevButton->getBounds();
        if (m_svgPrev) {
            float iconSize = btnBounds.width * 0.5f;
            AestraUI::NUIRect iconRect(btnBounds.x + (btnBounds.width - iconSize)/2, btnBounds.y + (btnBounds.height - iconSize)/2, iconSize, iconSize);
            AestraUI::NUISVGRenderer::render(renderer, *m_svgPrev, iconRect, iconColor);
        }
    }
    // Next
    {
        auto btnBounds = m_nextButton->getBounds();
         if (m_svgNext) {
            float iconSize = btnBounds.width * 0.5f;
            AestraUI::NUIRect iconRect(btnBounds.x + (btnBounds.width - iconSize)/2, btnBounds.y + (btnBounds.height - iconSize)/2, iconSize, iconSize);
            AestraUI::NUISVGRenderer::render(renderer, *m_svgNext, iconRect, iconColor);
        }
    }
    // Play
    {
        bool isPlaying = m_engine && m_engine->isPlaying();
        auto& playIcon = isPlaying ? m_svgPause : m_svgPlay;
        if (playIcon) {
            auto btnBounds = m_playPauseButton->getBounds();
            float iconSize = btnBounds.width * 0.5f;
            AestraUI::NUIRect iconRect(
                btnBounds.x + (btnBounds.width - iconSize)/2,
                btnBounds.y + (btnBounds.height - iconSize)/2,
                iconSize, iconSize
            );
            AestraUI::NUISVGRenderer::render(renderer, *playIcon, iconRect, iconColor);
        }
    }
}


// ============================================================================
// WAVEFORM (SOUNDCLOUD SCRUB)
// ============================================================================

void AuditionPanel::renderWaveform(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& area) {
    if (!m_engine) return;
    
    auto source = m_engine->getCurrentSource();
    if (!source || !source->isReady()) {
        renderer.drawText("Load track to see waveform", AestraUI::NUIPoint(area.x + area.width/2 - 60, area.y + area.height/2), 12.0f, AestraUI::NUIColor(0.5f, 0.5f, 0.5f, 0.5f));
        return;
    }
    
    auto buffer = source->getBuffer();
    if (!buffer) return;

    auto& theme = AestraUI::NUIThemeManager::getInstance();

    // Neon Colors
    AestraUI::NUIColor playedColorStart = theme.getColor("primary"); // Purple
    AestraUI::NUIColor playedColorEnd = theme.getColor("secondary"); // Cyan (or mapped secondary)
    AestraUI::NUIColor unplayedColor = theme.getColor("surfaceRaised");
    unplayedColor.a = 0.5f;

    const float pixelStride = 4.0f; // Wider spacing for cleaner look
    const float barWidth = 3.0f; // Thicker bars (less gap)
    const uint32_t numBars = static_cast<uint32_t>(area.width / pixelStride);
    if (numBars == 0) return;
    
    const size_t totalFrames = buffer->numFrames;
    const uint32_t channels = buffer->numChannels;
    const size_t framesPerBar = totalFrames / numBars;
    const auto& data = buffer->interleavedData;
    
    float centerY = area.y + area.height / 2.0f;
    float halfHeight = area.height / 2.0f;
    double progress = m_engine->getPositionNormalized();
    float playheadX = area.x + static_cast<float>(progress) * area.width;
    
    for (uint32_t i = 0; i < numBars; ++i) {
        float maxAmp = 0.0f;
        size_t startFrame = i * framesPerBar;
        size_t endFrame = std::min(startFrame + framesPerBar, totalFrames);
        size_t step = framesPerBar > 100 ? framesPerBar / 100 : 1;
        
        for (size_t f = startFrame; f < endFrame; f += step) {
            size_t idx = f * channels;
            if (idx < data.size()) {
                float s = std::abs(data[idx]);
                if (s > maxAmp) maxAmp = s;
            }
        }
        
        if (maxAmp > 0.001f) {
            // Apply log scaling or boost for better visuals
            float h = maxAmp * halfHeight * 1.5f; 
            if (h < 2.0f) h = 2.0f;
            if (h > halfHeight) h = halfHeight;
            
            float x = area.x + (i * pixelStride);
            
            AestraUI::NUIRect barRect(x, centerY - h, barWidth, h * 2.0f);
            
            if (x < playheadX) {
                // Vertical Gradient for Played Bars: Purple (Bottom) -> Cyan (Top)
                // Since we render from CenterY, we can just use a vibrant vertical gradient
                // Top Color
                AestraUI::NUIColor colTop = theme.getColor("secondary"); // Cyan
                AestraUI::NUIColor colBot = theme.getColor("primary");   // Purple
                
                // Draw in 2 segments for simple gradient simulation
                // Top Half
                renderer.fillRoundedRect(AestraUI::NUIRect(x, centerY - h, barWidth, h), 1.0f, colTop);
                // Bottom Half
                renderer.fillRoundedRect(AestraUI::NUIRect(x, centerY, barWidth, h), 1.0f, colBot);
                // Or better: manual lerp loop?
                // Let's stick to a solid vibrant color that matches the Slider gradient midpoint?
                // Or just use the Vertical Logic:
                // We'll draw 4 segments vertically
                for (int s=0; s<4; ++s) {
                    float t = s/3.0f;
                    AestraUI::NUIColor segCol = AestraUI::NUIColor::lerp(colTop, colBot, t);
                    renderer.fillRoundedRect(
                        AestraUI::NUIRect(x, (centerY-h) + (s*(h*2.0f)/4.0f), barWidth, (h*2.0f)/4.0f + 0.5f), 
                        0.5f, 
                        segCol
                    );
                }
            } else {
                // Unplayed: Darker, subtle
                renderer.fillRoundedRect(barRect, 1.0f, unplayedColor);
            }
        }
    }
}



// ============================================================================
// MOUSE HANDLING
// ============================================================================



bool AuditionPanel::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    auto bounds = getBounds();
    
    // Early exit if mouse is outside our bounds entirely
    if (!bounds.contains(event.position)) {
        m_hoveredQueueIndex = -1;
        return NUIComponent::onMouseEvent(event);
    }
    
    const float padding = 30.0f;
    float headerHeight = bounds.height * 0.35f;
    float waveformY = headerHeight;
    float waveformHeight = bounds.height * 0.20f;
    AestraUI::NUIRect waveformArea(bounds.x + padding, bounds.y + waveformY + 20.0f, bounds.width - padding*2, waveformHeight - 40.0f);
    
    // 1. Scrubbing
    if (event.pressed) {
        if (waveformArea.contains(event.position)) {
            m_isScrubbingWaveform = true;
            float relativeX = event.position.x - waveformArea.x;
            float normPos = relativeX / waveformArea.width;
            if (m_engine) m_engine->seekNormalized(static_cast<double>(std::clamp(normPos, 0.0f, 1.0f)));
            return true;
        }
    } else if (event.released) {
        m_isScrubbingWaveform = false;
    } else if (m_isScrubbingWaveform) {
        float relativeX = event.position.x - waveformArea.x;
        float normPos = relativeX / waveformArea.width;
        if (m_engine) m_engine->seekNormalized(static_cast<double>(std::clamp(normPos, 0.0f, 1.0f)));
        return true;
    }
    
    // 2. Queue Hover & Click-to-Play
    float queueY = waveformY + waveformHeight;
    float headerH = 30.0f;
    float startY = bounds.y + queueY + headerH + 10.0f;
    
    // Define queue area with proper X bounds
    AestraUI::NUIRect queueArea(bounds.x + padding, startY, bounds.width - padding * 2, bounds.height - (queueY + headerH + 10.0f) - padding);
    
    if (queueArea.contains(event.position)) {
        float relY = event.position.y - startY;
        int index = static_cast<int>(relY / 35.0f);
        if (m_engine && index >= 0 && index < static_cast<int>(m_engine->getQueue().size())) {
            m_hoveredQueueIndex = index;
            
            // Click to play
            if (event.pressed && event.button == AestraUI::NUIMouseButton::Left) {
                m_engine->jumpToTrack(static_cast<size_t>(index));
                m_engine->play();
                Log::info("[AuditionPanel] Clicked queue item: " + std::to_string(index));
                return true;
            }
        } else {
            m_hoveredQueueIndex = -1;
        }
    } else {
        m_hoveredQueueIndex = -1;
    }
    
    return NUIComponent::onMouseEvent(event);
}

// ============================================================================
// QUEUE MANAGEMENT
// ============================================================================

void AuditionPanel::addFileToQueue(const std::string& filePath, bool isReference) {
    if (m_engine) {
        m_engine->addToQueue(filePath, isReference);
    }
}

void AuditionPanel::addTimelineTrack(uint32_t trackId, const std::string& trackName) {
    if (m_engine) {
        m_engine->addTimelineTrack(trackId, trackName);
    }
}

bool AuditionPanel::isInDropZone(float x, float y) const {
    auto bounds = getBounds();
    // The queue area starts at bounds.height - queueHeight - padding.
    // With queueHeight = 0.30f * bounds.height and padding = 20.0f,
    // this is roughly bounds.height * 0.70f - 20.0f.
    // So, a simple check for y >= bounds.height * 0.70f is a good approximation for the drop zone.
    return (x >= 0.0f && x <= bounds.width && 
            y >= bounds.height * 0.70f && y <= bounds.height);
}

// ============================================================================
// HELPERS
// ============================================================================

std::string AuditionPanel::formatTime(double seconds) const {
    if (seconds < 0.0 || std::isnan(seconds) || std::isinf(seconds)) {
        return "0:00";
    }
    
    int totalSeconds = static_cast<int>(seconds);
    int minutes = totalSeconds / 60;
    int secs = totalSeconds % 60;
    
    std::ostringstream oss;
    oss << minutes << ":" << std::setfill('0') << std::setw(2) << secs;
    return oss.str();
}

// ============================================================================
// KEYBOARD HANDLING
// ============================================================================

bool AuditionPanel::onKeyEvent(const AestraUI::NUIKeyEvent& event) {
    if (event.pressed) {
        if (event.keyCode == AestraUI::NUIKeyCode::Space) {
            if (m_engine) {
                if (!m_engine->isPlaying()) {
                    if (m_onPlayRequest) m_onPlayRequest();
                }
                m_engine->togglePlayPause();
                return true;
            }
        }
        else if (event.keyCode == AestraUI::NUIKeyCode::Left) {
            if (m_engine) m_engine->previousTrack();
            return true;
        }
        else if (event.keyCode == AestraUI::NUIKeyCode::Right) {
            if (m_engine) m_engine->nextTrack();
            return true;
        }
    }
    return NUIComponent::onKeyEvent(event);
}

// ============================================================================
// DRAG AND DROP
// ============================================================================

AestraUI::DropFeedback AuditionPanel::onDragEnter(const AestraUI::DragData& data, const AestraUI::NUIPoint& position) {
    if (data.type == AestraUI::DragDataType::File) {
        m_isHoveringQueue = true; // Force queue hover effect
        return AestraUI::DropFeedback::Copy;
    }
    return AestraUI::DropFeedback::None;
}

AestraUI::DropFeedback AuditionPanel::onDragOver(const AestraUI::DragData& data, const AestraUI::NUIPoint& position) {
    if (data.type == AestraUI::DragDataType::File) {
        return AestraUI::DropFeedback::Copy;
    }
    return AestraUI::DropFeedback::None;
}

void AuditionPanel::onDragLeave() {
    // Only reset if we were hovering queue
    if (m_isHoveringQueue) {
        // m_isHoveringQueue = false; // Optional, might want to keep it if mouse is over
    }
}

AestraUI::DropResult AuditionPanel::onDrop(const AestraUI::DragData& data, const AestraUI::NUIPoint& position) {
    AestraUI::DropResult result;
    if (data.type == AestraUI::DragDataType::File) {
        Log::info("[AuditionPanel] Dropped file: " + data.filePath);
        addFileToQueue(data.filePath);
        result.accepted = true;
        result.message = "Added to queue";
        m_isHoveringQueue = false;
    }
    return result;
}

AestraUI::NUIRect AuditionPanel::getDropBounds() const {
    return getBounds();
}

} // namespace Aestra
