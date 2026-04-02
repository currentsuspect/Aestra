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

namespace {

std::string truncateAuditionText(const std::string& text, size_t maxChars) {
    if (text.size() <= maxChars) return text;
    if (maxChars <= 3) return text.substr(0, maxChars);
    return text.substr(0, maxChars - 3) + "...";
}

float clampf(float value, float lo, float hi) {
    return std::max(lo, std::min(value, hi));
}

} // namespace

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
            std::lock_guard<std::mutex> lock(m_pendingUiMutex);
            m_pendingTrackTitle = item.title;
            m_pendingTrackArtist = item.artist;
            m_pendingTrackUiUpdate = true;
            Log::info("[AuditionPanel] Track changed: " + item.title);
        });
        
        m_engine->setOnPlaybackStateChanged([this](bool isPlaying) {
            (void)isPlaying;
            // Visual update handled by SVG swap in onRender
            std::lock_guard<std::mutex> lock(m_pendingUiMutex);
            m_pendingPlaybackUiUpdate = true;
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
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    m_trackTitle = std::make_shared<AestraUI::NUILabel>("No Track Selected");
    m_trackTitle->setFontSize(28.0f);
    m_trackTitle->setAlignment(AestraUI::NUILabel::Alignment::Left);
    m_trackTitle->setTextColor(theme.getColor("textPrimary"));
    addChild(m_trackTitle);
    m_trackTitle->setVisible(false);
    
    m_trackArtist = std::make_shared<AestraUI::NUILabel>("Drag files to start");
    m_trackArtist->setFontSize(16.0f);
    m_trackArtist->setTextColor(theme.getColor("textSecondary"));
    m_trackArtist->setAlignment(AestraUI::NUILabel::Alignment::Left);
    addChild(m_trackArtist);
    m_trackArtist->setVisible(false);
    
    m_currentTime = std::make_shared<AestraUI::NUILabel>("0:00");
    m_currentTime->setFontSize(12.0f);
    m_currentTime->setTextColor(theme.getColor("textSecondary"));
    addChild(m_currentTime);
    
    m_totalTime = std::make_shared<AestraUI::NUILabel>("0:00");
    m_totalTime->setFontSize(12.0f);
    m_totalTime->setAlignment(AestraUI::NUILabel::Alignment::Right);
    m_totalTime->setTextColor(theme.getColor("textSecondary"));
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
            (void)playing;
            // Button visual update handled in onRender via SVG swap
            std::lock_guard<std::mutex> lock(m_pendingUiMutex);
            m_pendingPlaybackUiUpdate = true;
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
    float colNo = area.x + 12.0f;
    float colTitle = area.x + 58.0f;
    float colTime = area.x + area.width - 54.0f;
    
    // Items
    const auto& queue = m_engine->getQueue();
    auto currentItem = m_engine->getCurrentItem();

    if (queue.empty()) {
        const float centerX = area.x + area.width * 0.5f;
        const float centerY = area.y + area.height * 0.32f;
        renderer.drawText("Build a listening queue from the browser", AestraUI::NUIPoint(centerX - 106.0f, centerY - 8.0f), 13.0f, theme.getColor("textSecondary").withAlpha(0.95f));
        renderer.drawText("Drag files here or use Audition from the timeline menu", AestraUI::NUIPoint(centerX - 142.0f, centerY + 12.0f), 11.0f, theme.getColor("textTertiary").withAlpha(0.92f));
        return;
    }
    
    float y = area.y;
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
        AestraUI::NUIColor titleColor = isCurrent ? theme.getColor("primary") : theme.getColor("textPrimary");
        const std::string title = truncateAuditionText(item.title.empty() ? "Untitled Track" : item.title, 42);
        renderer.drawText(title, AestraUI::NUIPoint(colTitle, y + 8.0f), 13.0f, titleColor);
        
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
    const float contentWidth = std::max(0.0f, bounds.width - padding * 2.0f);
    const float headerHeight = clampf(bounds.height * 0.34f, 180.0f, 228.0f);
    const float waveformHeight = clampf(bounds.height * 0.17f, 108.0f, 140.0f);
    
    AestraUI::NUIRect headerRect(bounds.x + padding, bounds.y + padding, contentWidth, headerHeight);
    AestraUI::NUIRect waveformRect(bounds.x + padding, headerRect.bottom() + gap, contentWidth, waveformHeight);
    AestraUI::NUIRect queueRect(bounds.x + padding, waveformRect.bottom() + gap, contentWidth, bounds.height - waveformRect.bottom() - gap - padding);
    const bool hasCurrentTrack = (m_engine && m_engine->getCurrentItem().has_value());
    
    // === 1. Header Layout ===
    const float artPadding = 18.0f;
    const float artSize = clampf(headerRect.height - artPadding * 2.0f, 120.0f, 168.0f);
    const float artY = headerRect.y + (headerRect.height - artSize) * 0.5f;
    const float infoX = headerRect.x + artPadding + artSize + 28.0f;
    const float infoW = std::max(220.0f, headerRect.width - (artPadding + artSize + 28.0f) - artPadding);
    const float emptyContentCenterX = headerRect.x + headerRect.width * 0.40f;
    const float emptyContentWidth = clampf(infoW * 0.50f, 260.0f, 400.0f);
    const float startY = artY;
    
    // Title
    m_trackTitle->setFontSize(clampf(bounds.width * 0.022f, 22.0f, 30.0f));
    m_trackTitle->setBounds(AestraUI::NUIAbsolute(bounds,
                                                  (hasCurrentTrack ? infoX : (emptyContentCenterX - emptyContentWidth * 0.5f)) - bounds.x,
                                                  (startY + (hasCurrentTrack ? 18.0f : 42.0f)) - bounds.y,
                                                  hasCurrentTrack ? infoW : emptyContentWidth,
                                                  36.0f));
    m_trackTitle->setAlignment(hasCurrentTrack ? AestraUI::NUILabel::Alignment::Left : AestraUI::NUILabel::Alignment::Center);
    
    // Artist
    m_trackArtist->setFontSize(15.0f);
    m_trackArtist->setBounds(AestraUI::NUIAbsolute(bounds,
                                                   (hasCurrentTrack ? infoX : (emptyContentCenterX - emptyContentWidth * 0.5f)) - bounds.x,
                                                   (startY + (hasCurrentTrack ? 58.0f : 74.0f)) - bounds.y,
                                                   hasCurrentTrack ? infoW : emptyContentWidth,
                                                   22.0f));
    m_trackArtist->setAlignment(hasCurrentTrack ? AestraUI::NUILabel::Alignment::Left : AestraUI::NUILabel::Alignment::Center);
    
    // Controls - Bottom aligned in header info area
    float controlsY = hasCurrentTrack ? (headerRect.bottom() - ((headerRect.height - artSize) * 0.5f) - 58.0f)
                                      : (headerRect.y + headerRect.height * 0.71f);
    
    // Play Button Group
    float playSize = 54.0f;
    float navSize = 34.0f;
    float navGap = 10.0f;
    
    m_prevButton->setBounds(AestraUI::NUIAbsolute(bounds, infoX - bounds.x, controlsY + (playSize-navSize)/2 - bounds.y, navSize, navSize));
    // Glass Style for Prev
    m_prevButton->setBackgroundColor(AestraUI::NUIColor(0.1f, 0.1f, 0.15f, 0.4f));
    m_prevButton->setBorderColor(AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.1f));
    m_prevButton->setBorderWidth(1.0f);
    m_prevButton->setCornerRadius(navSize/2.0f);

    m_playPauseButton->setBounds(AestraUI::NUIAbsolute(bounds, infoX + navSize + navGap - bounds.x, controlsY - bounds.y, playSize, playSize));
    // Glass Style for Play (Slightly brighter/different?)
    // Let's make it consistent but maybe slightly more opaque or accented if playing? 
    // For now, consistent Glass Base.
    m_playPauseButton->setBackgroundColor(AestraUI::NUIColor(0.1f, 0.1f, 0.15f, 0.4f));
    m_playPauseButton->setBorderColor(AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.1f));
    m_playPauseButton->setBorderWidth(1.0f);
    m_playPauseButton->setCornerRadius(playSize/2.0f);

    m_nextButton->setBounds(AestraUI::NUIAbsolute(bounds, infoX + navSize + navGap + playSize + navGap - bounds.x, controlsY + (playSize-navSize)/2 - bounds.y, navSize, navSize));
    // Glass Style for Next
    m_nextButton->setBackgroundColor(AestraUI::NUIColor(0.1f, 0.1f, 0.15f, 0.4f));
    m_nextButton->setBorderColor(AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.1f));
    m_nextButton->setBorderWidth(1.0f);
    m_nextButton->setCornerRadius(navSize/2.0f);
    
    // DSP & Volume Group (Right aligned or Next to transport)
    float transportGroupWidth = navSize + navGap + playSize + navGap + navSize;
    float controlsRight = headerRect.right() - artPadding;
    float volumeW = hasCurrentTrack ? clampf(headerRect.width * 0.14f, 76.0f, 112.0f) : 78.0f;
    float abW = 50.0f;
    float dspW = hasCurrentTrack ? clampf(headerRect.width * 0.16f, 104.0f, 154.0f) : 108.0f;
    if (!hasCurrentTrack) {
        const float playCenterX = emptyContentCenterX - 74.0f;
        const float emptyClusterStartX = playCenterX - (navSize + navGap + playSize * 0.5f);
        const float emptyExtraX = emptyClusterStartX + transportGroupWidth + 14.0f;

        m_prevButton->setBounds(AestraUI::NUIAbsolute(bounds, emptyClusterStartX - bounds.x, controlsY + (playSize-navSize)/2 - bounds.y, navSize, navSize));
        m_playPauseButton->setBounds(AestraUI::NUIAbsolute(bounds, emptyClusterStartX + navSize + navGap - bounds.x, controlsY - bounds.y, playSize, playSize));
        m_nextButton->setBounds(AestraUI::NUIAbsolute(bounds, emptyClusterStartX + navSize + navGap + playSize + navGap - bounds.x, controlsY + (playSize-navSize)/2 - bounds.y, navSize, navSize));
        m_dspPresetButton->setBounds(AestraUI::NUIAbsolute(bounds, emptyExtraX - bounds.x, controlsY + 11.0f - bounds.y, dspW, 32.0f));
        m_abToggleButton->setBounds(AestraUI::NUIAbsolute(bounds, emptyExtraX + dspW + 10.0f - bounds.x, controlsY + 11.0f - bounds.y, abW, 32.0f));
        m_volumeSlider->setBounds(AestraUI::NUIAbsolute(bounds, emptyExtraX + dspW + abW + 14.0f - bounds.x, controlsY + 23.0f - bounds.y, volumeW, 6.0f));
    } else {
        float rightClusterWidth = dspW + 10.0f + abW + 14.0f + volumeW;
        float totalControlsWidth = transportGroupWidth + 14.0f + rightClusterWidth;
        float clusterStartX = infoX;
        float desiredStartX = infoX + 360.0f;
        float maxStartX = std::max(infoX, controlsRight - totalControlsWidth);
        clusterStartX = std::max(infoX + 220.0f, std::min(desiredStartX, maxStartX));
        float rowOverflow = (clusterStartX + totalControlsWidth) - controlsRight;
        if (rowOverflow > 0.0f) {
            clusterStartX -= rowOverflow;
        }
        clusterStartX = std::max(infoX + 180.0f, clusterStartX);
        float extraX = clusterStartX + transportGroupWidth + 14.0f;

        m_prevButton->setBounds(AestraUI::NUIAbsolute(bounds, clusterStartX - bounds.x, controlsY + (playSize-navSize)/2 - bounds.y, navSize, navSize));
        m_playPauseButton->setBounds(AestraUI::NUIAbsolute(bounds, clusterStartX + navSize + navGap - bounds.x, controlsY - bounds.y, playSize, playSize));
        m_nextButton->setBounds(AestraUI::NUIAbsolute(bounds, clusterStartX + navSize + navGap + playSize + navGap - bounds.x, controlsY + (playSize-navSize)/2 - bounds.y, navSize, navSize));
        
        m_dspPresetButton->setBounds(AestraUI::NUIAbsolute(bounds, extraX - bounds.x, controlsY + 11.0f - bounds.y, dspW, 32.0f));
        m_abToggleButton->setBounds(AestraUI::NUIAbsolute(bounds, extraX + dspW + 10.0f - bounds.x, controlsY + 11.0f - bounds.y, abW, 32.0f));
        m_volumeSlider->setBounds(AestraUI::NUIAbsolute(bounds, extraX + dspW + abW + 14.0f - bounds.x, controlsY + 23.0f - bounds.y, volumeW, 6.0f));
    }
    // Apply Glass Style to DSP Button
    // Apply Glass Style to DSP Button - Match Transport Style
    m_dspPresetButton->setBackgroundColor(AestraUI::NUIColor(0.12f, 0.12f, 0.22f, 0.4f));
    m_dspPresetButton->setBorderColor(AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.25f));
    m_dspPresetButton->setBorderWidth(1.0f);
    m_dspPresetButton->setCornerRadius(16.0f);

    // Apply Glass Style to A/B Button
    // Apply Glass Style to A/B Button - Match Transport Style
    m_abToggleButton->setBackgroundColor(AestraUI::NUIColor(0.12f, 0.12f, 0.22f, 0.4f));
    m_abToggleButton->setBorderColor(AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.25f));
    m_abToggleButton->setBorderWidth(1.0f);
    m_abToggleButton->setCornerRadius(16.0f);
    
    // Volume - Mini slider
    // === 2. Waveform Info ===
    // Time labels inside waveform panel, bottom corners
    float timeY = waveformRect.bottom() - 24.0f;
    m_currentTime->setBounds(AestraUI::NUIAbsolute(bounds, waveformRect.x + 12.0f - bounds.x, timeY - bounds.y, 60.0f, 16.0f));
    m_totalTime->setBounds(AestraUI::NUIAbsolute(bounds, waveformRect.right() - 72.0f - bounds.x, timeY - bounds.y, 60.0f, 16.0f));

    AestraUI::NUIRect waveformInner = waveformRect;
    waveformInner.x += 20.0f;
    waveformInner.y += 32.0f;
    waveformInner.width -= 40.0f;
    waveformInner.height -= 72.0f;
    m_waveformArea = waveformInner;

    AestraUI::NUIRect queueInner = queueRect;
    queueInner.x += 12.0f;
    queueInner.y += 34.0f;
    queueInner.width -= 24.0f;
    queueInner.height -= 40.0f;
    m_queueArea = queueInner;
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

    {
        std::lock_guard<std::mutex> lock(m_pendingUiMutex);
        if (m_pendingTrackUiUpdate) {
            if (m_trackTitle) m_trackTitle->setText(m_pendingTrackTitle);
            if (m_trackArtist) m_trackArtist->setText(m_pendingTrackArtist);
            m_pendingTrackUiUpdate = false;
            setDirty(true);
        }
        if (m_pendingPlaybackUiUpdate) {
            m_pendingPlaybackUiUpdate = false;
            setDirty(true);
        }
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
                try {
                    unsigned char* rgba = stbi_load_from_memory(item->coverArtData.data(), static_cast<int>(item->coverArtData.size()), &w, &h, &comp, 4);
                    if (rgba && w > 0 && h > 0 && w <= 16384 && h <= 16384) {
                        m_coverArtTextureId = renderer.createTexture(rgba, w, h);
                        m_coverArtWidth = w; m_coverArtHeight = h;
                        size_t sw = static_cast<size_t>(w), sh = static_cast<size_t>(h);
                        size_t idx = (sh / 2 * sw + sw / 2) * 4u;
                        size_t totalBytes = sw * sh * 4u;
                        if (idx + 2 < totalBytes) {
                            float r = rgba[idx] / 255.0f; float g = rgba[idx + 1] / 255.0f; float b = rgba[idx + 2] / 255.0f;
                            m_currentHeaderColor = AestraUI::NUIColor(r * 0.5f, g * 0.5f, b * 0.5f, 1.0f);
                        }
                        if (rgba) stbi_image_free(rgba);
                    } else {
                        Log::warning("[AuditionPanel] stbi_load_from_memory failed for cover art");
                        if (rgba) stbi_image_free(rgba);
                    }
                } catch (const std::exception& e) {
                    Log::error("[AuditionPanel] Exception decoding cover art: " + std::string(e.what()));
                } catch (...) {
                    Log::error("[AuditionPanel] Unknown exception decoding cover art");
                }
            }
        }
        hasCoverArt = (m_coverArtTextureId != 0);
    }
    
    // === 1. Base Background (Void) ===
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    renderer.fillRect(bounds, theme.getColor("backgroundPrimary"));
    renderer.fillRect(bounds, AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.015f));
    
    const float padding = 20.0f;
    const float gap = 16.0f;
    float headerHeight = clampf(bounds.height * 0.34f, 180.0f, 228.0f);
    float waveformHeight = clampf(bounds.height * 0.17f, 108.0f, 140.0f);
    
    AestraUI::NUIRect headerRect(bounds.x + padding, bounds.y + padding, bounds.width - padding*2, headerHeight);
    AestraUI::NUIRect waveformRect(bounds.x + padding, headerRect.bottom() + gap, bounds.width - padding*2, waveformHeight);
    AestraUI::NUIRect queueRect(bounds.x + padding, waveformRect.bottom() + gap, bounds.width - padding*2, bounds.height - waveformRect.bottom() - gap - padding);

    // === 2. Panels ===
    // Header
    renderer.drawShadow(headerRect, 0, 8, 24, AestraUI::NUIColor(0,0,0,0.45f));
    renderer.fillRoundedRect(headerRect, 16.0f, theme.getColor("surfaceTertiary"));
    renderer.strokeRoundedRect(headerRect, 16.0f, 1.0f, theme.getColor("border").withAlpha(0.85f));
    if (m_currentHeaderColor.a > 0.1f) {
         AestraUI::NUIColor tint = m_currentHeaderColor; tint.a = 0.09f;
         renderer.fillRoundedRect(headerRect, 16.0f, tint);
    }
    // Waveform
    renderer.drawShadow(waveformRect, 0, 6, 18, AestraUI::NUIColor(0,0,0,0.4f));
    renderer.fillRoundedRect(waveformRect, 16.0f, theme.getColor("surfaceTertiary"));
    renderer.strokeRoundedRect(waveformRect, 16.0f, 1.0f, theme.getColor("border").withAlpha(0.8f));
    // Queue
    renderer.drawShadow(queueRect, 0, 6, 20, AestraUI::NUIColor(0,0,0,0.42f));
    renderer.fillRoundedRect(queueRect, 16.0f, theme.getColor("surfaceTertiary"));
    renderer.strokeRoundedRect(queueRect, 16.0f, 1.0f, theme.getColor("border").withAlpha(0.8f));
    renderer.fillRoundedRect(AestraUI::NUIRect(headerRect.x, headerRect.y, headerRect.width, 36.0f), 16.0f, AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.018f));
    renderer.fillRoundedRect(AestraUI::NUIRect(waveformRect.x, waveformRect.y, waveformRect.width, 28.0f), 16.0f, AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.018f));
    renderer.fillRoundedRect(AestraUI::NUIRect(queueRect.x, queueRect.y, queueRect.width, 28.0f), 16.0f, AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.018f));
    
    // === 3. Cover Art ===
    if (headerRect.width > 50) {
        float artPadding = 18.0f;
        float artSize = clampf(headerHeight - (artPadding * 2), 120.0f, 168.0f);
        float artY = headerRect.y + (headerRect.height - artSize) * 0.5f;

        AestraUI::NUIRect artRect(headerRect.x + artPadding, artY, artSize, artSize);
        renderer.fillRoundedRect(artRect, 12.0f, AestraUI::NUIColor(0.05f, 0.06f, 0.08f, 0.9f));
        renderer.strokeRoundedRect(artRect, 12.0f, 1.0f, AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.08f));
        
        if (hasCoverArt) {
            AestraUI::NUIRect srcRect(0, 0, static_cast<float>(m_coverArtWidth), static_cast<float>(m_coverArtHeight));
            renderer.setClipRect(artRect);
            renderer.drawTexture(m_coverArtTextureId, artRect, srcRect);
            renderer.clearClipRect();
            renderer.strokeRoundedRect(artRect, 12.0f, 1.0f, AestraUI::NUIColor(1.0f,1.0f,1.0f,0.12f));
        } else {
             AestraUI::NUIColor artFill(m_currentHeaderColor.r * 0.8f, m_currentHeaderColor.g * 0.8f, m_currentHeaderColor.b * 0.8f, 1.0f);
             renderer.fillRoundedRect(artRect, 12.0f, artFill.withAlpha(0.28f));
             renderer.drawText("AESTRA", AestraUI::NUIPoint(artRect.x + artSize/2.0f - 26.0f, artRect.y + artSize/2.0f - 6.0f), 13.0f, AestraUI::NUIColor(0.82f, 0.82f, 0.82f, 0.85f));
        }
    }

    const bool hasCurrentTrack = (m_engine && m_engine->getCurrentItem().has_value());
    const float artPadding = 18.0f;
    const float artSize = clampf(headerHeight - (artPadding * 2), 120.0f, 168.0f);
    const float infoX = headerRect.x + artPadding + artSize + 28.0f;
    const float infoW = std::max(220.0f, headerRect.width - (artPadding + artSize + 28.0f) - artPadding);
    const float emptyContentCenterX = headerRect.x + headerRect.width * 0.46f;
    renderer.drawText("AUDITION", AestraUI::NUIPoint(infoX, headerRect.y + 14.0f), 11.0f, theme.getColor("textSecondary").withAlpha(0.85f));
    renderer.drawText("WAVEFORM", AestraUI::NUIPoint(waveformRect.x + 18.0f, waveformRect.y + 9.0f), 10.5f, theme.getColor("textSecondary").withAlpha(0.8f));
    renderer.drawText("QUEUE", AestraUI::NUIPoint(queueRect.x + 18.0f, queueRect.y + 9.0f), 10.5f, theme.getColor("textSecondary").withAlpha(0.8f));

    const float titleFont = clampf(bounds.width * 0.022f, 22.0f, 30.0f);
    const float titleY = hasCurrentTrack ? (headerRect.y + 92.0f) : (headerRect.y + 108.0f);
    const float subtitleY = titleY + (hasCurrentTrack ? 28.0f : 34.0f);
    const std::string titleText = truncateAuditionText(m_trackTitle ? m_trackTitle->getText() : "No Track Selected", 32);
    const std::string subtitleText = truncateAuditionText(m_trackArtist ? m_trackArtist->getText() : "Drag files to start", 46);
    if (hasCurrentTrack) {
        renderer.drawText(titleText, AestraUI::NUIPoint(infoX, titleY), titleFont, theme.getColor("textPrimary"));
        renderer.drawText(subtitleText, AestraUI::NUIPoint(infoX, subtitleY), 15.0f, theme.getColor("textSecondary"));
    } else {
        const float centerX = emptyContentCenterX;
        renderer.drawText(titleText, AestraUI::NUIPoint(centerX - 122.0f, titleY), titleFont, theme.getColor("textPrimary"));
        renderer.drawText(subtitleText, AestraUI::NUIPoint(centerX - 72.0f, subtitleY), 15.0f, theme.getColor("textSecondary"));
    }
    
    // === 4. Content Calls ===
    AestraUI::NUIRect waveformInner = waveformRect;
    float wInset = 20.0f;
    waveformInner.x += wInset; waveformInner.y += 32.0f; 
    waveformInner.width -= wInset*2; 
    waveformInner.height -= (48.0f + 24.0f);
    renderWaveform(renderer, waveformInner);
    
    AestraUI::NUIRect queueInner = queueRect;
    float qInset = 12.0f;
    queueInner.x += qInset; queueInner.y += 34.0f;
    queueInner.width -= qInset*2; queueInner.height -= (qInset + 28.0f); 
    renderQueue(renderer, queueInner);
    

    // === Render Time Label Backgrounds (Pill shape) in Reserved Bottom Space ===
    // Use the reserved space: waveformRect bottom + padding
    float timePillY = waveformInner.bottom() + 6.0f;
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
        auto& theme = AestraUI::NUIThemeManager::getInstance();
        const float centerX = area.x + area.width * 0.5f;
        const float centerY = area.y + area.height * 0.5f;
        renderer.drawText("Drop a track to analyze its shape", AestraUI::NUIPoint(centerX - 98.0f, centerY - 12.0f), 12.0f, theme.getColor("textSecondary").withAlpha(0.9f));
        renderer.drawText("Scrub here once a source is loaded", AestraUI::NUIPoint(centerX - 92.0f, centerY + 8.0f), 11.0f, theme.getColor("textTertiary").withAlpha(0.9f));
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

    const float pixelStride = 3.0f;
    const float barWidth = 2.0f;
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

    renderer.drawLine(
        AestraUI::NUIPoint(area.x, centerY),
        AestraUI::NUIPoint(area.right(), centerY),
        1.0f,
        theme.getColor("border").withAlpha(0.35f)
    );
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
    
    // 1. Scrubbing
    if (event.pressed) {
        if (m_waveformArea.contains(event.position)) {
            m_isScrubbingWaveform = true;
            float relativeX = event.position.x - m_waveformArea.x;
            float normPos = relativeX / m_waveformArea.width;
            if (m_engine) m_engine->seekNormalized(static_cast<double>(std::clamp(normPos, 0.0f, 1.0f)));
            return true;
        }
    } else if (event.released) {
        m_isScrubbingWaveform = false;
    } else if (m_isScrubbingWaveform) {
        float relativeX = event.position.x - m_waveformArea.x;
        float normPos = relativeX / m_waveformArea.width;
        if (m_engine) m_engine->seekNormalized(static_cast<double>(std::clamp(normPos, 0.0f, 1.0f)));
        return true;
    }
    
    // 2. Queue Hover & Click-to-Play
    if (m_queueArea.contains(event.position)) {
        float relY = event.position.y - m_queueArea.y;
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

    if (event.pressed && event.button == AestraUI::NUIMouseButton::Right) {
        return true;
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
        try {
            addFileToQueue(data.filePath);
            result.accepted = true;
            result.message = "Added to queue";
        } catch (const std::exception& e) {
            Log::error("[AuditionPanel] Exception in addFileToQueue: " + std::string(e.what()));
            result.accepted = false;
            result.message = std::string("Error: ") + e.what();
        } catch (...) {
            Log::error("[AuditionPanel] Unknown exception in addFileToQueue");
            result.accepted = false;
            result.message = "Unknown error";
        }
        m_isHoveringQueue = false;
    }
    return result;
}

AestraUI::NUIRect AuditionPanel::getDropBounds() const {
    return getBounds();
}

} // namespace Aestra
