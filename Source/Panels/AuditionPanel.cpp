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
            if (m_onActiveTrackPathChanged) {
                m_onActiveTrackPathChanged(item.filePath);
            }
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
    detachContextMenu(m_queueContextMenu);
    m_queueContextMenu = nullptr;
    if (m_engine) {
        m_engine->setOnTrackChanged(nullptr);
        m_engine->setOnPlaybackStateChanged(nullptr);
    }
    AestraUI::NUIDragDropManager::getInstance().unregisterDropTarget(this);
}

// ============================================================================
// COMPONENT SETUP
// ============================================================================

void AuditionPanel::setupComponents() {
    // 1. Text Labels
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    const auto& themeProps = theme.getCurrentTheme();
    m_trackTitle = std::make_shared<AestraUI::NUILabel>("No Track Selected");
    m_trackTitle->setFontSize(28.0f);
    m_trackTitle->setAlignment(AestraUI::NUILabel::Alignment::Left);
    m_trackTitle->setTextColor(theme.getColor("textPrimary"));
    addChild(m_trackTitle);
    m_trackTitle->setVisible(false);

    m_trackArtist = std::make_shared<AestraUI::NUILabel>("Drag files to start");
    m_trackArtist->setFontSize(themeProps.fontSizeS);
    m_trackArtist->setTextColor(theme.getColor("textSecondary"));
    m_trackArtist->setAlignment(AestraUI::NUILabel::Alignment::Left);
    addChild(m_trackArtist);
    m_trackArtist->setVisible(false);

    m_currentTime = std::make_shared<AestraUI::NUILabel>("0:00");
    m_currentTime->setFontSize(themeProps.fontSizeXS);
    m_currentTime->setTextColor(theme.getColor("textSecondary"));
    addChild(m_currentTime);

    m_totalTime = std::make_shared<AestraUI::NUILabel>("0:00");
    m_totalTime->setFontSize(themeProps.fontSizeXS);
    m_totalTime->setAlignment(AestraUI::NUILabel::Alignment::Right);
    m_totalTime->setTextColor(theme.getColor("textSecondary"));
    addChild(m_totalTime);
    
    // 2. Transport Buttons (Text cleared for SVG overlap)
    m_playPauseButton = std::make_shared<AestraUI::NUIButton>(""); 
    m_playPauseButton->setStyle(AestraUI::NUIButton::Style::Primary);
    m_playPauseButton->setCornerRadius(28.0f);
    m_playPauseButton->setHoverColor(theme.getColor("accentPrimary").withAlpha(0.86f));
    m_playPauseButton->setPressedColor(theme.getColor("accentPrimary").withAlpha(1.0f));
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
    m_prevButton->setHoverColor(theme.getColor("surfaceRaised").withAlpha(0.92f));
    m_prevButton->setPressedColor(theme.getColor("accentPrimary").withAlpha(0.42f));
    m_prevButton->setOnClick([this]() { if (m_engine) m_engine->previousTrack(); });
    addChild(m_prevButton);
    
    m_nextButton = std::make_shared<AestraUI::NUIButton>("");
    m_nextButton->setStyle(AestraUI::NUIButton::Style::Icon);
    m_nextButton->setHoverColor(theme.getColor("surfaceRaised").withAlpha(0.92f));
    m_nextButton->setPressedColor(theme.getColor("accentPrimary").withAlpha(0.42f));
    m_nextButton->setOnClick([this]() { if (m_engine) m_engine->nextTrack(); });
    addChild(m_nextButton);
    
    // 3. DSP Buttons
    m_dspPresetButton = std::make_shared<AestraUI::NUIButton>("Studio Reference");
    m_dspPresetButton->setStyle(AestraUI::NUIButton::Style::Secondary);
    m_dspPresetButton->setHoverColor(theme.getColor("accentPrimary").withAlpha(0.22f));
    m_dspPresetButton->setPressedColor(theme.getColor("accentPrimary").withAlpha(0.34f));
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
    
    m_abToggleButton = std::make_shared<AestraUI::NUIButton>("A DRY");
    m_abToggleButton->setStyle(AestraUI::NUIButton::Style::Secondary);
    m_abToggleButton->setToggleable(true);
    m_abToggleButton->setHoverColor(theme.getColor("accentPrimary").withAlpha(0.24f));
    m_abToggleButton->setPressedColor(theme.getColor("accentPrimary").withAlpha(0.40f));
    m_abToggleButton->setOnToggle([this](bool active) {
        if (m_engine) m_engine->setABMode(active);
        if (m_abToggleButton) m_abToggleButton->setText(active ? "B WET" : "A DRY");
    });
    addChild(m_abToggleButton);
    
    // 4. Sliders
    m_progressSlider = std::make_shared<AestraUI::NUISlider>();
    m_progressSlider->setTrackColor(theme.getColor("surfaceRaised").withAlpha(0.86f));
    m_progressSlider->setFillColor(theme.getColor("accentPrimary").withAlpha(0.95f));
    m_progressSlider->setThumbColor(theme.getColor("textPrimary").withAlpha(0.94f));
    m_progressSlider->setThumbHoverColor(theme.getColor("accentPrimary").lightened(0.16f));
    m_progressSlider->setValue(0.0f);
    m_progressSlider->setOnValueChange([this](double val) {
        if (m_engine) m_engine->seekNormalized(std::clamp(val, 0.0, 1.0));
    });
    addChild(m_progressSlider);
    
    m_volumeSlider = std::make_shared<AestraUI::NUISlider>();
    m_volumeSlider->setTrackColor(theme.getColor("surfaceRaised").withAlpha(0.86f));
    m_volumeSlider->setFillColor(theme.getColor("accentPrimary").withAlpha(0.92f));
    m_volumeSlider->setThumbColor(theme.getColor("textPrimary").withAlpha(0.94f));
    m_volumeSlider->setThumbHoverColor(theme.getColor("accentPrimary").lightened(0.16f));
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
    
}
// ... [Lines 185-385 unchanged] ...
// ============================================================================
// QUEUE RENDERING (WITH HOVER)
// ============================================================================

void AuditionPanel::renderQueue(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& area) {
    if (!m_engine) return;

    auto& theme = AestraUI::NUIThemeManager::getInstance();
    const auto& themeProps = theme.getCurrentTheme();
    constexpr float rowH = 42.0f;
    constexpr float rowPitch = rowH;
    constexpr float headerH = 24.0f;
    const float listY = area.y + headerH + 2.0f;

    float colNo = area.x + 8.0f;
    float colTitle = area.x + 44.0f;
    float colTime = area.x + area.width - 56.0f;

    renderer.drawText("Queue", AestraUI::NUIPoint(area.x + 2.0f, area.y + 4.0f), themeProps.fontSizeXS, theme.getColor("textSecondary").withAlpha(0.95f));
    if (m_clearQueueHovered) {
        renderer.fillRoundedRect(m_clearQueueButtonBounds, themeProps.radiusM, theme.getColor("accentPrimary").withAlpha(0.16f));
        renderer.strokeRoundedRect(m_clearQueueButtonBounds, themeProps.radiusM, 1.0f, theme.getColor("accentPrimary").withAlpha(0.58f));
    }
    renderer.drawText("Clear", AestraUI::NUIPoint(m_clearQueueButtonBounds.x + 3.0f, m_clearQueueButtonBounds.y + 1.0f), themeProps.fontSizeXS,
                      m_clearQueueHovered ? theme.getColor("textPrimary") : theme.getColor("textSecondary").withAlpha(0.90f));
    renderer.drawLine(
        AestraUI::NUIPoint(area.x, listY - 2.0f),
        AestraUI::NUIPoint(area.right(), listY - 2.0f),
        1.0f,
        theme.getColor("textPrimary").withAlpha(0.08f));
    
    // Items
    const auto& queue = m_engine->getQueue();
    auto currentItem = m_engine->getCurrentItem();

    if (queue.empty()) {
        const float centerX = area.x + area.width * 0.5f;
        const float centerY = listY + (area.height - headerH) * 0.32f;
        renderer.drawText("Build a listening queue from the browser", AestraUI::NUIPoint(centerX - 106.0f, centerY - 8.0f), 13.0f, theme.getColor("textSecondary").withAlpha(0.92f));
        renderer.drawText("Drag files here or use Audition from the timeline menu", AestraUI::NUIPoint(centerX - 142.0f, centerY + 12.0f), 11.0f, theme.getColor("textMuted").withAlpha(0.90f));
        return;
    }
    
    float y = listY;
    
    for (size_t i = 0; i < queue.size(); ++i) {
        if (y + rowH > area.y + area.height) break; // Clip
        
        const auto& item = queue[i];
        bool isCurrent = (m_engine->isPlaying() && currentItem && currentItem->id == item.id);
        bool isHovered = (static_cast<int>(i) == m_hoveredQueueIndex);
        
        AestraUI::NUIRect rowRect(area.x, y, area.width, rowH);
        
        // Subtle row backgrounds; no boxed queue container treatment.
        if (isCurrent) {
            renderer.fillRect(rowRect, theme.getColor("accentPrimary").withAlpha(0.24f));
            renderer.fillRect(AestraUI::NUIRect(rowRect.x, rowRect.y, 4.0f, rowRect.height), theme.getColor("accentPrimary").withAlpha(0.96f));
            renderer.strokeRect(rowRect, 1.0f, theme.getColor("accentPrimary").withAlpha(0.52f));
        } else if (isHovered) {
            renderer.fillRect(rowRect, theme.getColor("surfaceRaised").withAlpha(0.05f));
        }
        renderer.drawLine(
            AestraUI::NUIPoint(rowRect.x, rowRect.bottom()),
            AestraUI::NUIPoint(rowRect.right(), rowRect.bottom()),
            1.0f,
            theme.getColor("textPrimary").withAlpha(0.08f));

        // Left edge: hover drag handle or active equalizer icon, otherwise track number.
        const bool showHandle = isHovered && !isCurrent;
        if (isCurrent) {
            const float baseX = colNo + 1.0f;
            const float baseY = y + 12.0f;
            for (int b = 0; b < 3; ++b) {
                const float phase = m_animationTime * 8.0f + static_cast<float>(b) * 0.75f;
                const float h = 5.0f + (std::sin(phase) * 0.5f + 0.5f) * 8.0f;
                const float bx = baseX + static_cast<float>(b) * 4.5f;
                renderer.fillRoundedRect(AestraUI::NUIRect(bx, baseY + (14.0f - h), 3.0f, h), 1.0f, theme.getColor("accentPrimary").withAlpha(0.95f));
            }
        } else if (showHandle) {
            const float dotX = colNo + 2.0f;
            const float dotY = y + 11.0f;
            const bool handleHovered = (m_queueHandleHoverIndex == static_cast<int>(i));
            if (handleHovered) {
                renderer.fillRoundedRect(AestraUI::NUIRect(colNo - 3.0f, y + 8.0f, 16.0f, 20.0f), themeProps.radiusS + 1.0f, theme.getColor("accentPrimary").withAlpha(0.22f));
            }
            const auto dotColor = handleHovered
                ? theme.getColor("textPrimary")
                : theme.getColor("textSecondary").withAlpha(0.82f);
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 2; ++c) {
                    renderer.fillRoundedRect(AestraUI::NUIRect(dotX + c * 4.0f, dotY + r * 4.0f, 1.8f, 1.8f), 0.9f, dotColor);
                }
            }
        } else {
            const AestraUI::NUIColor numberColor = theme.getColor("textMuted").withAlpha(0.92f);
            renderer.drawText(std::to_string(i + 1), AestraUI::NUIPoint(colNo, y + 12.0f), themeProps.fontSizeXS, numberColor);
        }

        // Track name (middle, top line)
        AestraUI::NUIColor titleColor = isCurrent ? theme.getColor("textPrimary") : theme.getColor("textPrimary").withAlpha(0.96f);
        const std::string title = truncateAuditionText(item.title.empty() ? "Untitled Track" : item.title, 42);
        renderer.drawText(title, AestraUI::NUIPoint(colTitle, y + 8.0f), 13.0f, titleColor);

        // Artist (middle, second line)
        const std::string artist = truncateAuditionText(item.artist.empty() ? "Unknown Artist" : item.artist, 46);
        renderer.drawText(artist, AestraUI::NUIPoint(colTitle, y + 24.0f), 11.0f, theme.getColor("textSecondary").withAlpha(0.86f));

        // Duration (right)
        std::string timeStr = (item.durationSeconds > 0.0) ? formatTime(item.durationSeconds) : "--:--";
        renderer.drawText(timeStr, AestraUI::NUIPoint(colTime, y + 14.0f), themeProps.fontSizeXS, theme.getColor("textSecondary").withAlpha(0.94f));

        // Hover-only remove control before duration.
        if (isHovered) {
            if (m_queueRemoveHoverIndex == static_cast<int>(i)) {
                renderer.fillRoundedRect(AestraUI::NUIRect(colTime - 25.0f, y + 10.0f, 17.0f, 17.0f), themeProps.radiusM + 0.5f, theme.getColor("error").withAlpha(0.24f));
            }
            const auto removeColor = (m_queueRemoveHoverIndex == static_cast<int>(i))
                ? theme.getColor("error")
                : theme.getColor("textSecondary").withAlpha(0.80f);
            renderer.drawText("✕", AestraUI::NUIPoint(colTime - 22.0f, y + 14.0f), themeProps.fontSizeXS, removeColor);
        }
        
        y += rowPitch;
    }

    if (m_isDraggingQueueItem && m_queueDragInsertIndex >= 0) {
        const int lineIdx = m_queueDragInsertIndex;
        const float lineY = listY + static_cast<float>(lineIdx) * rowPitch;
        renderer.fillRect(
            AestraUI::NUIRect(area.x + 6.0f, lineY - 1.0f, area.width - 12.0f, 2.0f),
            theme.getColor("accentPrimary").withAlpha(0.95f));
    }
}
// ============================================================================
// LAYOUT - Called from onResize
// ============================================================================

// ============================================================================
// LAYOUT - Called from onResize
// ============================================================================

void AuditionPanel::layoutComponents() {
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    auto bounds = getBounds();
    const float padding = 18.0f;
    const float gap = 8.0f;
    const float contentWidth = std::max(0.0f, bounds.width - padding * 2.0f);
    const float headerHeight = clampf(bounds.height * 0.44f, 220.0f, 280.0f);
    const float waveformHeight = 80.0f;
    
    AestraUI::NUIRect headerRect(bounds.x + padding, bounds.y + padding, contentWidth, headerHeight);
    AestraUI::NUIRect waveformRect(bounds.x + padding, headerRect.bottom() + gap, contentWidth, waveformHeight);
    AestraUI::NUIRect queueRect(bounds.x + padding, waveformRect.bottom() + gap, contentWidth, bounds.height - waveformRect.bottom() - gap - padding);
    const bool hasCurrentTrack = (m_engine && m_engine->getCurrentItem().has_value());
    
    // === 1. Player block layout ===
    const float innerPad = 24.0f;
    const float artSize = 140.0f;
    const float topRowH = artSize;
    const float controlsH = 62.0f;
    const float progressRowH = 22.0f;
    const float utilityH = 28.0f;
    const float rowGap1 = 8.0f;
    const float rowGap2 = 14.0f;
    const float rowGap3 = 14.0f;
    const float rightColH = 34.0f + rowGap1 + 22.0f + rowGap1 + controlsH;
    const float topBlockH = std::max(artSize, rightColH);

    const float utilityY = headerRect.bottom() - innerPad - utilityH;
    const float progressY = utilityY - rowGap3 - progressRowH;
    const float upperAreaTop = headerRect.y + innerPad;
    const float upperAreaBottom = progressY - rowGap2;
    const float upperAreaH = std::max(0.0f, upperAreaBottom - upperAreaTop);
    const float topRowY = upperAreaTop + std::max(0.0f, (upperAreaH - topBlockH) * 0.5f);

    const float artX = headerRect.x + innerPad;
    const float artY = topRowY + (topBlockH - artSize) * 0.5f;
    const float infoX = artX + artSize + 24.0f;
    const float infoW = std::max(220.0f, headerRect.right() - innerPad - infoX);
    const float rightColY = topRowY + (topBlockH - rightColH) * 0.5f;
    const float titleY = rightColY;
    const float subtitleY = titleY + 34.0f + rowGap1;
    const float controlsInlineY = subtitleY + 22.0f + rowGap1;

    m_trackTitle->setFontSize(clampf(bounds.width * 0.021f, 22.0f, 30.0f));
    m_trackTitle->setBounds(AestraUI::NUIAbsolute(bounds, infoX - bounds.x, titleY - bounds.y, infoW, 34.0f));
    m_trackTitle->setAlignment(AestraUI::NUILabel::Alignment::Left);
    m_trackArtist->setFontSize(15.0f);
    m_trackArtist->setBounds(AestraUI::NUIAbsolute(bounds, infoX - bounds.x, subtitleY - bounds.y, infoW, 22.0f));
    m_trackArtist->setAlignment(AestraUI::NUILabel::Alignment::Left);

    // Centered transport row
    const float playSize = 62.0f;
    const float navSize = 40.0f;
    const float navGap = 14.0f;
    const auto navButtonBg = theme.getColor("buttonBgDefault").withAlpha(0.66f);
    const auto navButtonBorder = theme.getColor("borderSubtle").withAlpha(0.92f);
    const auto utilityButtonBg = theme.getColor("buttonBgDefault").withAlpha(0.72f);
    const auto utilityButtonBorder = theme.getColor("border").withAlpha(0.92f);

    const float transportWidth = navSize + navGap + playSize + navGap + navSize;
    const float transportStartX = hasCurrentTrack
        ? infoX
        : (headerRect.x + headerRect.width * 0.5f - transportWidth * 0.5f);
    m_prevButton->setBounds(AestraUI::NUIAbsolute(bounds, transportStartX - bounds.x, controlsInlineY + (playSize - navSize) * 0.5f - bounds.y, navSize, navSize));
    m_prevButton->setBackgroundColor(navButtonBg);
    m_prevButton->setBorderColor(navButtonBorder);
    m_prevButton->setBorderWidth(1.0f);
    m_prevButton->setCornerRadius(navSize * 0.5f);

    m_playPauseButton->setBounds(AestraUI::NUIAbsolute(bounds, transportStartX + navSize + navGap - bounds.x, controlsInlineY - bounds.y, playSize, playSize));
    m_playPauseButton->setBackgroundColor(navButtonBg);
    m_playPauseButton->setBorderColor(theme.getColor("accentPrimary").withAlpha(0.95f));
    m_playPauseButton->setBorderWidth(1.0f);
    m_playPauseButton->setCornerRadius(playSize * 0.5f);

    m_nextButton->setBounds(AestraUI::NUIAbsolute(bounds, transportStartX + navSize + navGap + playSize + navGap - bounds.x, controlsInlineY + (playSize - navSize) * 0.5f - bounds.y, navSize, navSize));
    m_nextButton->setBackgroundColor(navButtonBg);
    m_nextButton->setBorderColor(navButtonBorder);
    m_nextButton->setBorderWidth(1.0f);
    m_nextButton->setCornerRadius(navSize * 0.5f);

    // Full-width progress row with timestamps at ends.
    const float progressLeft = headerRect.x + innerPad;
    const float progressRight = headerRect.right() - innerPad;
    const float timeW = 48.0f;
    const float sliderX = progressLeft + timeW + 8.0f;
    const float sliderW = std::max(120.0f, progressRight - sliderX - timeW - 8.0f);
    m_currentTime->setBounds(AestraUI::NUIAbsolute(bounds, progressLeft - bounds.x, progressY + 3.0f - bounds.y, timeW, 16.0f));
    m_currentTime->setAlignment(AestraUI::NUILabel::Alignment::Left);
    m_totalTime->setBounds(AestraUI::NUIAbsolute(bounds, progressRight - timeW - bounds.x, progressY + 3.0f - bounds.y, timeW, 16.0f));
    m_totalTime->setAlignment(AestraUI::NUILabel::Alignment::Right);
    m_progressSlider->setBounds(AestraUI::NUIAbsolute(bounds, sliderX - bounds.x, progressY + 8.0f - bounds.y, sliderW, 6.0f));

    // Bottom utility row: pills left, volume right on one line.
    const float dspW = 124.0f;
    const float abW = 64.0f;
    const float volumeWDefault = clampf(headerRect.width * 0.20f, 120.0f, 180.0f);
    const float pillsEndX = progressLeft + dspW + 8.0f + abW;
    const float minGap = 16.0f;
    const float maxVolumeW = std::max(80.0f, progressRight - (pillsEndX + minGap));
    const float volumeW = std::min(volumeWDefault, maxVolumeW);
    const float volumeX = progressRight - volumeW;

    m_dspPresetButton->setBounds(AestraUI::NUIAbsolute(bounds, progressLeft - bounds.x, utilityY - bounds.y, dspW, 26.0f));
    m_abToggleButton->setBounds(AestraUI::NUIAbsolute(bounds, progressLeft + dspW + 8.0f - bounds.x, utilityY - bounds.y, abW, 26.0f));
    m_dspPresetButton->setBackgroundColor(utilityButtonBg);
    m_dspPresetButton->setBorderColor(utilityButtonBorder);
    m_dspPresetButton->setBorderWidth(1.0f);
    m_dspPresetButton->setCornerRadius(13.0f);
    m_abToggleButton->setBackgroundColor(utilityButtonBg);
    m_abToggleButton->setBorderColor(utilityButtonBorder);
    m_abToggleButton->setBorderWidth(1.0f);
    m_abToggleButton->setCornerRadius(13.0f);
    m_volumeSlider->setBounds(AestraUI::NUIAbsolute(bounds, volumeX - bounds.x, utilityY + 10.0f - bounds.y, volumeW, 8.0f));

    m_waveformArea = waveformRect;
    m_queueArea = queueRect;
    m_clearQueueButtonBounds = AestraUI::NUIRect(queueRect.right() - 44.0f, queueRect.y + 2.0f, 42.0f, 18.0f);
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
            if (m_pendingTrackTitle != m_displayTrackTitle || m_pendingTrackArtist != m_displayTrackArtist) {
                m_nextTrackTitle = m_pendingTrackTitle;
                m_nextTrackArtist = m_pendingTrackArtist;
                m_trackTextTransitionActive = true;
                m_trackTextTransitionSwapped = false;
                m_trackTextTransitionTime = 0.0f;
            }
            m_pendingTrackUiUpdate = false;
            setDirty(true);
        }
        if (m_pendingPlaybackUiUpdate) {
            m_pendingPlaybackUiUpdate = false;
            setDirty(true);
        }
    }

    if (m_engine) {
        bool hasCurrent = m_engine->getCurrentItem().has_value();
        if (hasCurrent != m_hadCurrentItem) {
            m_hadCurrentItem = hasCurrent;
            if (!hasCurrent) {
                if (m_trackTitle && m_trackTitle->getText() != "No Track Selected") {
                    m_trackTitle->setText("No Track Selected");
                }
                if (m_trackArtist && m_trackArtist->getText() != "Drag files to start") {
                    m_trackArtist->setText("Drag files to start");
                }
                m_displayTrackTitle = "No Track Selected";
                m_displayTrackArtist = "Drag files to start";
                m_trackTextTransitionActive = false;
                m_trackTextTransitionSwapped = false;
                m_trackTextTransitionTime = 0.0f;
                m_waveformRevealTrackId.clear();
                if (m_onActiveTrackPathChanged) m_onActiveTrackPathChanged("");
            }
            layoutComponents();
            setDirty(true);
        }

        const auto& theme = AestraUI::NUIThemeManager::getInstance();
        const bool isPlaying = m_engine->isPlaying();
        if (m_playPauseButton && isPlaying != m_lastPlayingVisualState) {
            const auto playBg = isPlaying
                ? theme.getColor("accentPrimary").withAlpha(0.96f)
                : theme.getColor("buttonBgDefault").withAlpha(0.72f);
            const auto playBorder = isPlaying
                ? theme.getColor("accentPrimary").lightened(0.22f).withAlpha(0.98f)
                : theme.getColor("accentPrimary").withAlpha(0.95f);
            m_playPauseButton->setBackgroundColor(playBg);
            m_playPauseButton->setBorderColor(playBorder);
            m_playPauseButton->setHoverColor(theme.getColor("accentPrimary").withAlpha(isPlaying ? 1.0f : 0.88f));
            m_playPauseButton->setPressedColor(theme.getColor("accentPrimary").withAlpha(1.0f));
            m_lastPlayingVisualState = isPlaying;
        }

        const bool abWet = m_engine->isABWetMode();
        if (m_abToggleButton && abWet != m_lastABWetVisualState) {
            m_abToggleButton->setToggled(abWet);
            m_abToggleButton->setText(abWet ? "B WET" : "A DRY");
            if (abWet) {
                m_abToggleButton->setBackgroundColor(theme.getColor("accentPrimary").withAlpha(0.38f));
                m_abToggleButton->setBorderColor(theme.getColor("accentPrimary").withAlpha(0.95f));
                m_abToggleButton->setTextColor(theme.getColor("textPrimary"));
            } else {
                m_abToggleButton->setBackgroundColor(theme.getColor("buttonBgDefault").withAlpha(0.72f));
                m_abToggleButton->setBorderColor(theme.getColor("border").withAlpha(0.92f));
                m_abToggleButton->setTextColor(theme.getColor("textSecondary").withAlpha(0.98f));
            }
            m_lastABWetVisualState = abWet;
        }
    }

    bool sourceReady = false;
    std::string currentTrackId;
    if (m_engine) {
        auto source = m_engine->getCurrentSource();
        sourceReady = (source && source->isReady());
        auto item = m_engine->getCurrentItem();
        if (item) currentTrackId = item->id;
    }

    if (m_isDropLoading) {
        const bool loadTimedOut = (m_animationTime - m_loadingStateStart) > 2.0f;
        if (sourceReady || loadTimedOut) {
            m_isDropLoading = false;
            setDirty(true);
        }
    }

    if (sourceReady && !currentTrackId.empty() && currentTrackId != m_waveformRevealTrackId) {
        m_waveformRevealTrackId = currentTrackId;
        m_waveformRevealProgress = 0.0f;
        m_waveformRevealActive = true;
        setDirty(true);
    }
    if (m_waveformRevealActive) {
        m_waveformRevealProgress = std::min(1.0f, m_waveformRevealProgress + static_cast<float>(deltaTime / 0.4));
        if (m_waveformRevealProgress >= 0.999f) {
            m_waveformRevealProgress = 1.0f;
            m_waveformRevealActive = false;
        }
        setDirty(true);
    }

    if (m_trackTextTransitionActive) {
        constexpr float transitionDuration = 0.15f;
        constexpr float halfDuration = transitionDuration * 0.5f;
        m_trackTextTransitionTime += static_cast<float>(deltaTime);
        if (!m_trackTextTransitionSwapped && m_trackTextTransitionTime >= halfDuration) {
            m_displayTrackTitle = m_nextTrackTitle;
            m_displayTrackArtist = m_nextTrackArtist;
            m_trackTextTransitionSwapped = true;
        }
        if (m_trackTextTransitionTime >= transitionDuration) {
            m_trackTextTransitionActive = false;
            m_trackTextTransitionSwapped = false;
            m_trackTextTransitionTime = 0.0f;
        }
        setDirty(true);
    }
    
    // Update time
    if (m_engine) {
        double pos = m_engine->getPositionSeconds();
        double dur = m_engine->getDurationSeconds();
        m_currentTime->setText(formatTime(pos));
        m_totalTime->setText(formatTime(dur));
        const double norm = (dur > 1e-6) ? std::clamp(pos / dur, 0.0, 1.0) : 0.0;
        if (m_progressSlider && std::abs(m_progressSlider->getValue() - norm) > 0.002) {
            m_progressSlider->setValue(norm);
        }
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
                }
            }
        } else if (!item) {
            m_currentTrackId.clear();
            if (m_coverArtTextureId != 0) {
                renderer.deleteTexture(m_coverArtTextureId);
                m_coverArtTextureId = 0;
                m_coverArtWidth = 0;
                m_coverArtHeight = 0;
            }
        }
        hasCoverArt = (m_coverArtTextureId != 0);
    }
    
    // === 1. Base Background (Void) ===
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    const auto& themeProps = theme.getCurrentTheme();
    renderer.fillRect(bounds, theme.getColor("backgroundPrimary"));
    renderer.fillRect(bounds, theme.getColor("backgroundSecondary").withAlpha(0.22f));
    
    const float padding = 18.0f;
    const float gap = 8.0f;
    const float headerHeight = clampf(bounds.height * 0.44f, 220.0f, 280.0f);
    const float waveformHeight = 80.0f;
    
    AestraUI::NUIRect headerRect(bounds.x + padding, bounds.y + padding, bounds.width - padding * 2.0f, headerHeight);
    AestraUI::NUIRect waveformRect(bounds.x + padding, headerRect.bottom() + gap, bounds.width - padding * 2.0f, waveformHeight);
    AestraUI::NUIRect queueRect(bounds.x + padding, waveformRect.bottom() + gap, bounds.width - padding * 2.0f, bounds.height - waveformRect.bottom() - gap - padding);

    // One continuous audition surface with subtle section flow.
    AestraUI::NUIRect surfaceRect(bounds.x + padding, bounds.y + padding, bounds.width - padding * 2.0f, bounds.height - padding * 2.0f);
    const float surfaceRadius = std::max(themeProps.radiusL + 4.0f, 0.0f);
    renderer.fillRoundedRect(surfaceRect, surfaceRadius, theme.getColor("surfaceTertiary").withAlpha(0.86f));
    renderer.fillRoundedRect(
        AestraUI::NUIRect(surfaceRect.x + 1.0f, surfaceRect.y + 1.0f, surfaceRect.width - 2.0f, surfaceRect.height * 0.30f),
        std::max(themeProps.radiusL + 3.0f, 0.0f),
        theme.getColor("surfaceRaised").withAlpha(0.34f));
    renderer.strokeRoundedRect(surfaceRect, surfaceRadius, 1.0f, theme.getColor("borderSubtle").withAlpha(0.42f));

    renderer.drawLine(
        AestraUI::NUIPoint(surfaceRect.x + 8.0f, waveformRect.y - 4.0f),
        AestraUI::NUIPoint(surfaceRect.right() - 8.0f, waveformRect.y - 4.0f),
        1.0f,
        theme.getColor("textPrimary").withAlpha(0.10f));
    renderer.drawLine(
        AestraUI::NUIPoint(surfaceRect.x + 8.0f, queueRect.y - 4.0f),
        AestraUI::NUIPoint(surfaceRect.right() - 8.0f, queueRect.y - 4.0f),
        1.0f,
        theme.getColor("textPrimary").withAlpha(0.10f));

    if (m_isHoveringQueue) {
        const float pulse = 0.5f + 0.5f * std::sin(m_animationTime * 6.0f);
        const auto accent = theme.getColor("accentPrimary").withAlpha(0.35f + pulse * 0.35f);
        renderer.fillRoundedRect(surfaceRect, 16.0f, AestraUI::NUIColor(0, 0, 0, 0.18f));
        renderer.drawGlow(headerRect, 20.0f, 0.55f + pulse * 0.35f, accent);
        renderer.strokeRoundedRect(headerRect, 16.0f, 2.0f, accent);
        const auto center = headerRect.center();
        renderer.drawText("⬇", AestraUI::NUIPoint(center.x - 9.0f, center.y - 28.0f), 30.0f, theme.getColor("textPrimary").withAlpha(0.95f));
        renderer.drawText("Drop to audition", AestraUI::NUIPoint(center.x - 62.0f, center.y + 4.0f), 16.0f, theme.getColor("textPrimary").withAlpha(0.95f));
    }
    
    const bool hasCurrentTrack = (m_engine && m_engine->getCurrentItem().has_value());

    // === 3. Cover Art ===
    if (headerRect.width > 50 && (hasCurrentTrack || hasCoverArt || m_isDropLoading)) {
        const float innerPad = 24.0f;
        const float artSize = 140.0f;
        const float topRowH = artSize;
        const float controlsH = 62.0f;
        const float progressRowH = 22.0f;
        const float utilityH = 28.0f;
        const float rowGap1 = 8.0f;
        const float rowGap2 = 14.0f;
        const float rowGap3 = 14.0f;
        const float rightColH = 34.0f + rowGap1 + 22.0f + rowGap1 + controlsH;
        const float topBlockH = std::max(artSize, rightColH);
        const float utilityY = headerRect.bottom() - innerPad - utilityH;
        const float progressY = utilityY - rowGap3 - progressRowH;
        const float upperAreaTop = headerRect.y + innerPad;
        const float upperAreaBottom = progressY - rowGap2;
        const float upperAreaH = std::max(0.0f, upperAreaBottom - upperAreaTop);
        const float topRowY = upperAreaTop + std::max(0.0f, (upperAreaH - topBlockH) * 0.5f);
        const float artY = topRowY + (topBlockH - artSize) * 0.5f;

        AestraUI::NUIRect artRect(headerRect.x + innerPad, artY, artSize, artSize);
        renderer.fillRoundedRect(artRect, themeProps.radiusL, theme.getColor("backgroundSecondary").withAlpha(0.94f));
        renderer.fillRoundedRect(
            AestraUI::NUIRect(artRect.x + 1.0f, artRect.y + 1.0f, artRect.width - 2.0f, artRect.height * 0.48f),
            std::max(themeProps.radiusL - 1.0f, 0.0f),
            theme.getColor("surfaceRaised").withAlpha(0.28f));
        renderer.strokeRoundedRect(artRect, themeProps.radiusL, 1.0f, theme.getColor("borderSubtle").withAlpha(0.78f));
        
        if (m_isDropLoading) {
            const float cx = artRect.x + artRect.width * 0.5f;
            const float cy = artRect.y + artRect.height * 0.5f;
            const float r0 = 14.0f;
            const float r1 = 22.0f;
            const int segments = 12;
            const float phase = m_animationTime * 10.0f;
            for (int i = 0; i < segments; ++i) {
                const float a = (static_cast<float>(i) / static_cast<float>(segments)) * 6.2831853f;
                const float alpha = 0.15f + 0.75f * ((std::sin(phase - static_cast<float>(i) * 0.45f) + 1.0f) * 0.5f);
                const auto col = theme.getColor("accentPrimary").withAlpha(alpha);
                const AestraUI::NUIPoint p0(cx + std::cos(a) * r0, cy + std::sin(a) * r0);
                const AestraUI::NUIPoint p1(cx + std::cos(a) * r1, cy + std::sin(a) * r1);
                renderer.drawLine(p0, p1, 2.0f, col);
            }
        } else if (hasCoverArt) {
            AestraUI::NUIRect srcRect(0, 0, static_cast<float>(m_coverArtWidth), static_cast<float>(m_coverArtHeight));
            // Keep image fully visible (contain fit) and inset so rounded-corner shell clips cleanly.
            AestraUI::NUIRect dstRect = AestraUI::NUIRect(artRect.x + 6.0f, artRect.y + 6.0f, artRect.width - 12.0f, artRect.height - 12.0f);
            if (m_coverArtWidth > 0 && m_coverArtHeight > 0) {
                const float srcAspect = static_cast<float>(m_coverArtWidth) / static_cast<float>(m_coverArtHeight);
                const float dstAspect = dstRect.width / dstRect.height;
                if (srcAspect > dstAspect) {
                    const float fittedH = dstRect.width / srcAspect;
                    dstRect.y = dstRect.y + (dstRect.height - fittedH) * 0.5f;
                    dstRect.height = fittedH;
                } else {
                    const float fittedW = dstRect.height * srcAspect;
                    dstRect.x = dstRect.x + (dstRect.width - fittedW) * 0.5f;
                    dstRect.width = fittedW;
                }
            }
            renderer.setClipRect(artRect);
            renderer.drawTexture(m_coverArtTextureId, dstRect, srcRect);
            renderer.clearClipRect();
            renderer.strokeRoundedRect(artRect, themeProps.radiusL, 1.0f, theme.getColor("border").withAlpha(0.92f));
        } else {
             AestraUI::NUIColor artFill(m_currentHeaderColor.r * 0.8f, m_currentHeaderColor.g * 0.8f, m_currentHeaderColor.b * 0.8f, 1.0f);
             renderer.fillRoundedRect(artRect, themeProps.radiusL, artFill.withAlpha(0.14f));
             renderer.drawText("♪", AestraUI::NUIPoint(artRect.x + artSize * 0.5f - 6.0f, artRect.y + artSize * 0.5f - 10.0f), 22.0f, theme.getColor("textMuted").withAlpha(0.40f));
        }
    }

    const float innerPad = 24.0f;
    const float artSize = 140.0f;
    const float topRowH = artSize;
    const float controlsH = 62.0f;
    const float progressRowH = 22.0f;
    const float utilityH = 28.0f;
    const float rowGap1 = 8.0f;
    const float rowGap2 = 14.0f;
    const float rowGap3 = 14.0f;
    const float rightColH = 34.0f + rowGap1 + 22.0f + rowGap1 + controlsH;
    const float topBlockH = std::max(artSize, rightColH);
    const float utilityY = headerRect.bottom() - innerPad - utilityH;
    const float progressY = utilityY - rowGap3 - progressRowH;
    const float upperAreaTop = headerRect.y + innerPad;
    const float upperAreaBottom = progressY - rowGap2;
    const float upperAreaH = std::max(0.0f, upperAreaBottom - upperAreaTop);
    const float topRowY = upperAreaTop + std::max(0.0f, (upperAreaH - topBlockH) * 0.5f);
    const float infoX = headerRect.x + innerPad + artSize + 24.0f;
    const float infoW = std::max(200.0f, headerRect.right() - innerPad - infoX);

    const float titleFont = clampf(bounds.width * 0.022f, 22.0f, 30.0f);
    const float rightColY = topRowY + (topBlockH - rightColH) * 0.5f;
    const float titleY = rightColY;
    const float subtitleY = titleY + 34.0f + rowGap1;
    const float playSize = 62.0f;
    const float navSize = 40.0f;
    const float navGap = 14.0f;
    const float transportWidth = navSize + navGap + playSize + navGap + navSize;
    const float transportCenterX = hasCurrentTrack
        ? (infoX + transportWidth * 0.5f)
        : (headerRect.x + headerRect.width * 0.5f);
    float titleAlpha = 1.0f;
    if (m_trackTextTransitionActive) {
        constexpr float transitionDuration = 0.15f;
        constexpr float halfDuration = transitionDuration * 0.5f;
        if (m_trackTextTransitionTime < halfDuration) {
            titleAlpha = clampf(1.0f - (m_trackTextTransitionTime / halfDuration), 0.0f, 1.0f);
        } else {
            titleAlpha = clampf((m_trackTextTransitionTime - halfDuration) / halfDuration, 0.0f, 1.0f);
        }
    }

    const std::string titleText = m_isDropLoading
        ? "Loading..."
        : truncateAuditionText(m_displayTrackTitle, 32);
    const std::string subtitleText = m_isDropLoading
        ? "Preparing audio preview"
        : truncateAuditionText(m_displayTrackArtist, 46);
    if (hasCurrentTrack) {
        renderer.drawText(titleText, AestraUI::NUIPoint(infoX, titleY), titleFont, theme.getColor("textPrimary").withAlpha(titleAlpha));
        renderer.drawText(subtitleText, AestraUI::NUIPoint(infoX, subtitleY), 15.0f, theme.getColor("textSecondary").withAlpha(0.95f * titleAlpha));
    } else {
        const float textW = std::min(520.0f, headerRect.width - innerPad * 2.0f);
        renderer.drawTextCentered(
            titleText,
            AestraUI::NUIRect(transportCenterX - textW * 0.5f, titleY, textW, 34.0f),
            titleFont,
            theme.getColor("textPrimary").withAlpha(titleAlpha));
        renderer.drawTextCentered(
            subtitleText,
            AestraUI::NUIRect(transportCenterX - textW * 0.5f, subtitleY, textW, 22.0f),
            15.0f,
            theme.getColor("textMuted").withAlpha(0.95f * titleAlpha));
    }
    
    // === 4. Content Calls ===
    AestraUI::NUIRect waveformInner = m_waveformArea;
    renderWaveform(renderer, waveformInner);
    
    AestraUI::NUIRect queueInner = m_queueArea;
    renderQueue(renderer, queueInner);
    
    // === Overlay SVGs on Buttons ===
    // Force White/Primary color for visibility against filled buttons
    AestraUI::NUIColor iconColor = AestraUI::NUIColor::white();
    
    // Manual background fill for Icon buttons REMOVED (Handled by NUIButton now with renderChildren)
    
    // Note: We still render icons manually because NUIButton's internal setIcon might not be used here yet.
    // If we call renderChildren(), it will draw the NUIButton background/border.
    // Then we draw the icon on top.
    
    // RENDER CHILD COMPONENTS (Buttons, Labels, Slider) on top of panels
    renderChildren(renderer);

    if (m_isDropLoading && m_progressSlider) {
        const auto pb = m_progressSlider->getBounds();
        const float shimmerW = std::max(36.0f, pb.width * 0.24f);
        const float travel = pb.width + shimmerW;
        const float x = pb.x + std::fmod(m_animationTime * 220.0f, travel) - shimmerW;
        renderer.fillRoundedRect(
            AestraUI::NUIRect(x, pb.y - 1.0f, shimmerW, pb.height + 2.0f),
            std::max(1.0f, pb.height * 0.5f),
            theme.getColor("accentPrimary").withAlpha(0.32f));
    }

    if (m_progressSlider && m_progressSlider->isHovered()) {
        const auto pb = m_progressSlider->getBounds();
        renderer.strokeRoundedRect(
            AestraUI::NUIRect(pb.x - 2.0f, pb.y - 3.0f, pb.width + 4.0f, pb.height + 6.0f),
            std::max(4.0f, pb.height * 0.9f),
            1.0f,
            theme.getColor("accentPrimary").withAlpha(0.62f));
    }
    if (m_volumeSlider && m_volumeSlider->isHovered()) {
        const auto vb = m_volumeSlider->getBounds();
        renderer.strokeRoundedRect(
            AestraUI::NUIRect(vb.x - 2.0f, vb.y - 3.0f, vb.width + 4.0f, vb.height + 6.0f),
            std::max(4.0f, vb.height * 0.9f),
            1.0f,
            theme.getColor("accentPrimary").withAlpha(0.62f));
    }

    // Volume icon to the left of the volume slider.
    if (m_volumeSlider) {
        const auto v = m_volumeSlider->getBounds();
        const float iconCx = v.x - 12.0f;
        const float iconCy = v.y + v.height * 0.5f;
        const bool volumeHovered = m_volumeSlider->isHovered();
        const AestraUI::NUIColor volColor = volumeHovered
            ? theme.getColor("accentPrimary").withAlpha(0.96f)
            : theme.getColor("textSecondary").withAlpha(0.90f);

        // Speaker body
        renderer.fillRect(AestraUI::NUIRect(iconCx - 6.0f, iconCy - 3.0f, 4.0f, 6.0f), volColor);
        // Cone
        renderer.drawLine(AestraUI::NUIPoint(iconCx - 2.0f, iconCy - 3.0f), AestraUI::NUIPoint(iconCx + 2.0f, iconCy - 6.0f), 1.5f, volColor);
        renderer.drawLine(AestraUI::NUIPoint(iconCx - 2.0f, iconCy + 3.0f), AestraUI::NUIPoint(iconCx + 2.0f, iconCy + 6.0f), 1.5f, volColor);
        renderer.drawLine(AestraUI::NUIPoint(iconCx + 2.0f, iconCy - 6.0f), AestraUI::NUIPoint(iconCx + 2.0f, iconCy + 6.0f), 1.5f, volColor);
        // Waves
        renderer.drawLine(AestraUI::NUIPoint(iconCx + 5.0f, iconCy - 3.0f), AestraUI::NUIPoint(iconCx + 7.0f, iconCy), 1.0f, volColor.withAlpha(0.78f));
        renderer.drawLine(AestraUI::NUIPoint(iconCx + 7.0f, iconCy), AestraUI::NUIPoint(iconCx + 5.0f, iconCy + 3.0f), 1.0f, volColor.withAlpha(0.78f));
    }

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
        const auto& themeProps = theme.getCurrentTheme();
        const float centerX = area.x + area.width * 0.5f;
        const float centerY = area.y + area.height * 0.5f;
        renderer.drawText("Drop a track to analyze its shape", AestraUI::NUIPoint(centerX - 98.0f, centerY - 12.0f), themeProps.fontSizeXS, theme.getColor("textSecondary").withAlpha(0.9f));
        renderer.drawText("Scrub here once a source is loaded", AestraUI::NUIPoint(centerX - 92.0f, centerY + 8.0f), themeProps.fontSizeXS, theme.getColor("textMuted").withAlpha(0.95f));
        return;
    }

    auto buffer = source->getBuffer();
    if (!buffer) return;

    auto& theme = AestraUI::NUIThemeManager::getInstance();

    // Neon Colors
    AestraUI::NUIColor playedColorStart = theme.getColor("primary"); // Purple
    AestraUI::NUIColor playedColorEnd = theme.getColor("secondary"); // Cyan (or mapped secondary)
    AestraUI::NUIColor unplayedColor = theme.getColor("surfaceRaised");
    unplayedColor.a = 0.68f;

    const float pixelStride = 2.0f;
    const float barWidth = 2.0f;
    const uint32_t numBars = static_cast<uint32_t>(area.width / pixelStride);
    if (numBars == 0) return;
    
    const size_t totalFrames = buffer->numFrames;
    const uint32_t channels = buffer->numChannels;
    const auto& data = buffer->interleavedData;
    
    float centerY = area.y + area.height / 2.0f;
    float halfHeight = area.height / 2.0f;
    double progress = m_engine->getPositionNormalized();
    float playheadX = area.x + static_cast<float>(progress) * area.width;
    const float easedReveal = m_waveformRevealProgress * m_waveformRevealProgress * (3.0f - 2.0f * m_waveformRevealProgress);
    const float revealX = area.x + area.width * std::clamp(easedReveal, 0.0f, 1.0f);
    
    for (uint32_t i = 0; i < numBars; ++i) {
        float maxAmp = 0.0f;
        const size_t startFrame = static_cast<size_t>((static_cast<double>(i) / static_cast<double>(numBars)) * static_cast<double>(totalFrames));
        const size_t endFrame = std::max(startFrame + 1,
                                         static_cast<size_t>((static_cast<double>(i + 1) / static_cast<double>(numBars)) * static_cast<double>(totalFrames)));
        const size_t spanFrames = std::max<size_t>(1, endFrame - startFrame);
        const size_t step = spanFrames > 100 ? (spanFrames / 100) : 1;
        
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
            if (x > revealX) break;
            
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
    // A hidden Audition workspace must never intercept input belonging to the
    // active one (#671).
    //
    // This panel is laid out even while hidden, and its bounds cover the whole
    // content area, so with Timeline showing its `m_queueArea` lands squarely
    // over two playlist lanes. Any press there used to reach the queue branch
    // below and be swallowed by `if (queueSize <= 0) return true;` — the lane
    // never saw the click, and because motion events are never marked handled
    // the rows still hovered normally, which made it look like a dead zone
    // rather than an interception.
    //
    // Verified by controlled toggle on the live process: visible_ is 1 with
    // Audition active and 0 with Timeline active, so visibility is the correct
    // predicate for "this workspace is active".
    //
    // NUIComponent's parent-level filter (#672) already skips hidden children,
    // making this redundant for the child-dispatch path. It is kept because
    // this override may also be invoked directly, and because the base guard
    // this method eventually delegates to is only reached on some paths — the
    // queue branch returns long before it.
    // The captured-cursor exemption must match the parent's contract exactly:
    // this panel keeps interaction state that deliberately survives the pointer
    // leaving its bounds (`m_isScrubbingWaveform`, see the early-out below), so
    // a panel hidden mid-scrub still needs its terminating move/release or the
    // scrub stays latched. Guarding on visibility alone would strand it —
    // the same class of defect this change exists to remove, one file over.
    if (!isVisible() && !event.cursorCaptured) {
        return false;
    }

    auto bounds = getBounds();
    constexpr float queueHeaderH = 24.0f;
    constexpr float queueRowPitch = 42.0f;
    auto queueRowAt = [&](const AestraUI::NUIPoint& p) -> int {
        if (!m_queueArea.contains(p)) return -1;
        const float listY = m_queueArea.y + queueHeaderH + 2.0f;
        if (p.y < listY) return -1;
        const int idx = static_cast<int>((p.y - listY) / queueRowPitch);
        if (!m_engine) return -1;
        const int size = static_cast<int>(m_engine->getQueue().size());
        return (idx >= 0 && idx < size) ? idx : -1;
    };
    auto isOnQueueHandle = [&](const AestraUI::NUIPoint& p, int row) -> bool {
        if (row < 0) return false;
        const float y = m_queueArea.y + queueHeaderH + 2.0f + static_cast<float>(row) * queueRowPitch;
        return AestraUI::NUIRect(m_queueArea.x + 8.0f, y + 10.0f, 12.0f, 16.0f).contains(p);
    };
    auto isOnQueueRemove = [&](const AestraUI::NUIPoint& p, int row) -> bool {
        if (row < 0) return false;
        const float y = m_queueArea.y + queueHeaderH + 2.0f + static_cast<float>(row) * queueRowPitch;
        return AestraUI::NUIRect(m_queueArea.right() - 78.0f, y + 9.0f, 18.0f, 18.0f).contains(p);
    };
    auto queueInsertAt = [&](const AestraUI::NUIPoint& p) -> int {
        if (!m_engine) return -1;
        const int size = static_cast<int>(m_engine->getQueue().size());
        const float listY = m_queueArea.y + queueHeaderH + 2.0f;
        const float raw = (p.y - listY) / queueRowPitch;
        return std::clamp(static_cast<int>(std::floor(raw + 0.5f)), 0, size);
    };
    auto scrubNormFromX = [this](float x) -> double {
        if (m_waveformArea.width <= 1.0f) return 0.0;
        const float relativeX = x - m_waveformArea.x;
        const float normPos = relativeX / m_waveformArea.width;
        // Avoid hard-locking at exact track end while dragging.
        return static_cast<double>(std::clamp(normPos, 0.0f, 0.9995f));
    };

    // Give the queue context menu first chance to consume input.
    if (m_queueContextMenu) {
        const bool handled = m_queueContextMenu->onMouseEvent(event);
        if (handled) {
            return true;
        }
        if (event.pressed &&
            (event.button == AestraUI::NUIMouseButton::Left || event.button == AestraUI::NUIMouseButton::Right)) {
            detachContextMenu(m_queueContextMenu);
            m_queueContextMenu = nullptr;
            setDirty(true);
        }
    }
    
    // Early exit if mouse is outside our bounds entirely
    if (!bounds.contains(event.position) && !m_isScrubbingWaveform) {
        m_hoveredQueueIndex = -1;
        m_queueRemoveHoverIndex = -1;
        m_queueHandleHoverIndex = -1;
        m_clearQueueHovered = false;
        return NUIComponent::onMouseEvent(event);
    }
    
    // 1. Scrubbing (left-click only)
    if (event.pressed && event.button == AestraUI::NUIMouseButton::Left) {
        if (m_waveformArea.contains(event.position)) {
            m_isScrubbingWaveform = true;
            if (m_engine) m_engine->seekNormalized(scrubNormFromX(event.position.x));
            return true;
        }
    } else if (event.released) {
        m_isScrubbingWaveform = false;
    } else if (m_isScrubbingWaveform) {
        if (m_engine) m_engine->seekNormalized(scrubNormFromX(event.position.x));
        return true;
    }
    
    // 2. Queue interactions (clear, remove, reorder, click-to-play)
    const bool clearHover = m_clearQueueButtonBounds.contains(event.position);
    if (clearHover != m_clearQueueHovered) {
        m_clearQueueHovered = clearHover;
        setDirty(true);
    }

    if (event.pressed && event.button == AestraUI::NUIMouseButton::Left && clearHover) {
        if (m_engine) m_engine->clearQueue();
        m_hoveredQueueIndex = -1;
        m_queueRemoveHoverIndex = -1;
        m_queueHandleHoverIndex = -1;
        m_clearQueueHovered = false;
        m_isDraggingQueueItem = false;
        m_queueDragFromIndex = -1;
        m_queueDragInsertIndex = -1;
        setDirty(true);
        return true;
    }

    if (!event.pressed && !event.released && m_isDraggingQueueItem) {
        m_queueDragInsertIndex = queueInsertAt(event.position);
        setDirty(true);
        return true;
    }

    if (m_queueArea.contains(event.position)) {
        const int prevHover = m_hoveredQueueIndex;
        const int prevHandleHover = m_queueHandleHoverIndex;
        const int prevRemoveHover = m_queueRemoveHoverIndex;
        const int index = queueRowAt(event.position);
        m_hoveredQueueIndex = index;
        m_queueHandleHoverIndex = (index >= 0 && isOnQueueHandle(event.position, index)) ? index : -1;
        m_queueRemoveHoverIndex = (index >= 0 && isOnQueueRemove(event.position, index)) ? index : -1;
        if (prevHover != m_hoveredQueueIndex || prevHandleHover != m_queueHandleHoverIndex || prevRemoveHover != m_queueRemoveHoverIndex) {
            setDirty(true);
        }

        if (event.pressed && event.button == AestraUI::NUIMouseButton::Left && index >= 0) {
            if (m_queueRemoveHoverIndex == index) {
                if (m_engine) m_engine->removeFromQueue(static_cast<size_t>(index));
                m_hoveredQueueIndex = -1;
                m_queueRemoveHoverIndex = -1;
                m_queueHandleHoverIndex = -1;
                setDirty(true);
                return true;
            }
            if (m_queueHandleHoverIndex == index) {
                m_isDraggingQueueItem = true;
                m_queueDragFromIndex = index;
                m_queueDragInsertIndex = index;
                setDirty(true);
                return true;
            }
            if (m_engine) {
                m_engine->jumpToTrack(static_cast<size_t>(index));
                m_engine->play();
                Log::info("[AuditionPanel] Clicked queue item: " + std::to_string(index));
                return true;
            }
        }

        if (event.pressed && event.button == AestraUI::NUIMouseButton::Right && index >= 0 && m_engine) {
            const auto& queue = m_engine->getQueue();
            const int queueSize = static_cast<int>(queue.size());
            if (queueSize <= 0) return true;

            detachContextMenu(m_queueContextMenu);
            m_queueContextMenu = std::make_shared<AestraUI::NUIContextMenu>();
            auto menu = m_queueContextMenu;

            const bool canMoveUp = index > 0;
            const bool canMoveDown = index < queueSize - 1;
            const bool canMoveTop = index > 0;
            const bool canMoveBottom = index < queueSize - 1;

            auto playNowItem = std::make_shared<AestraUI::NUIContextMenuItem>("Play Now");
            playNowItem->setOnClick([this, index]() {
                if (!m_engine) return;
                m_engine->jumpToTrack(static_cast<size_t>(index));
                m_engine->play();
            });
            menu->addItem(playNowItem);
            menu->addSeparator();

            auto moveTopItem = std::make_shared<AestraUI::NUIContextMenuItem>("Move to Top");
            moveTopItem->setEnabled(canMoveTop);
            moveTopItem->setOnClick([this, index]() {
                if (!m_engine || index <= 0) return;
                m_engine->moveQueueItem(static_cast<size_t>(index), 0);
            });
            menu->addItem(moveTopItem);

            auto moveUpItem = std::make_shared<AestraUI::NUIContextMenuItem>("Move Up");
            moveUpItem->setEnabled(canMoveUp);
            moveUpItem->setOnClick([this, index]() {
                if (!m_engine || index <= 0) return;
                m_engine->moveQueueItem(static_cast<size_t>(index), static_cast<size_t>(index - 1));
            });
            menu->addItem(moveUpItem);

            auto moveDownItem = std::make_shared<AestraUI::NUIContextMenuItem>("Move Down");
            moveDownItem->setEnabled(canMoveDown);
            moveDownItem->setOnClick([this, index, queueSize]() {
                if (!m_engine || index >= queueSize - 1) return;
                m_engine->moveQueueItem(static_cast<size_t>(index), static_cast<size_t>(index + 1));
            });
            menu->addItem(moveDownItem);

            auto moveBottomItem = std::make_shared<AestraUI::NUIContextMenuItem>("Move to Bottom");
            moveBottomItem->setEnabled(canMoveBottom);
            moveBottomItem->setOnClick([this, index, queueSize]() {
                if (!m_engine || index >= queueSize - 1) return;
                m_engine->moveQueueItem(static_cast<size_t>(index), static_cast<size_t>(queueSize - 1));
            });
            menu->addItem(moveBottomItem);
            menu->addSeparator();

            auto deleteItem = std::make_shared<AestraUI::NUIContextMenuItem>("Delete");
            deleteItem->setOnClick([this, index]() {
                if (!m_engine) return;
                m_engine->removeFromQueue(static_cast<size_t>(index));
            });
            menu->addItem(deleteItem);

            menu->setOnHide([this]() {
                detachContextMenu(m_queueContextMenu);
                m_queueContextMenu = nullptr;
                setDirty(true);
            });
            attachAndShowContextMenu(this, menu, event.position);
            setDirty(true);
            return true;
        }

        if (event.released && event.button == AestraUI::NUIMouseButton::Left && m_isDraggingQueueItem) {
            const int from = m_queueDragFromIndex;
            const int insert = std::max(0, m_queueDragInsertIndex);
            if (m_engine && from >= 0) {
                int destination = insert;
                if (destination > from) destination -= 1;
                const int size = static_cast<int>(m_engine->getQueue().size());
                destination = std::clamp(destination, 0, std::max(0, size - 1));
                if (destination != from) {
                    m_engine->moveQueueItem(static_cast<size_t>(from), static_cast<size_t>(destination));
                }
            }
            m_isDraggingQueueItem = false;
            m_queueDragFromIndex = -1;
            m_queueDragInsertIndex = -1;
            setDirty(true);
            return true;
        }
    } else {
        if (m_hoveredQueueIndex != -1 || m_queueRemoveHoverIndex != -1 || m_queueHandleHoverIndex != -1) {
            m_hoveredQueueIndex = -1;
            m_queueRemoveHoverIndex = -1;
            m_queueHandleHoverIndex = -1;
            setDirty(true);
        }
        if (event.released && event.button == AestraUI::NUIMouseButton::Left && m_isDraggingQueueItem) {
            m_isDraggingQueueItem = false;
            m_queueDragFromIndex = -1;
            m_queueDragInsertIndex = -1;
            setDirty(true);
            return true;
        }
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
        if (event.keyCode == AestraUI::NUIKeyCode::Left) {
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
        m_isHoveringQueue = true;
        return AestraUI::DropFeedback::Copy;
    }
    return AestraUI::DropFeedback::None;
}

void AuditionPanel::onDragLeave() {
    m_isHoveringQueue = false;
}

AestraUI::DropResult AuditionPanel::onDrop(const AestraUI::DragData& data, const AestraUI::NUIPoint& position) {
    AestraUI::DropResult result;
    if (data.type == AestraUI::DragDataType::File) {
        Log::info("[AuditionPanel] Dropped file: " + data.filePath);
        try {
            addFileToQueue(data.filePath);
            m_isDropLoading = true;
            m_loadingStateStart = m_animationTime;
            m_waveformRevealTrackId.clear();
            result.accepted = true;
            result.message = "Added to queue";
        } catch (const std::exception& e) {
            Log::error("[AuditionPanel] Exception in addFileToQueue: " + std::string(e.what()));
            result.accepted = false;
            result.message = std::string("Error: ") + e.what();
        }
        m_isHoveringQueue = false;
    }
    return result;
}

AestraUI::NUIRect AuditionPanel::getDropBounds() const {
    return getBounds();
}

} // namespace Aestra
