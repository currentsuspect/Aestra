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
            if (m_playPauseButton) {
                m_playPauseButton->setText(isPlaying ? "PAUSE" : "PLAY");
            }
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
        if (m_engine) m_engine->togglePlayPause();
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
    
    // Headers
    float headerH = 30.0f;
    AestraUI::NUIColor headerTxtColor(0.7f, 0.7f, 0.7f, 1.0f);
    float colNo = area.x + 10.0f;
    float colTitle = area.x + 50.0f;
    float colTime = area.x + area.width - 60.0f;
    
    renderer.drawText("#", AestraUI::NUIPoint(colNo, area.y + 10.0f), 12.0f, headerTxtColor);
    renderer.drawText("Title", AestraUI::NUIPoint(colTitle, area.y + 10.0f), 12.0f, headerTxtColor);
    renderer.drawText("Time", AestraUI::NUIPoint(colTime, area.y + 10.0f), 12.0f, headerTxtColor);
    renderer.drawLine(AestraUI::NUIPoint(area.x, area.y + headerH), AestraUI::NUIPoint(area.x + area.width, area.y + headerH), 1.0f, AestraUI::NUIColor(1.0f,1.0f,1.0f,0.1f));
    
    // Items
    const auto& queue = m_engine->getQueue();
    auto currentItem = m_engine->getCurrentItem();
    
    float y = area.y + headerH + 10.0f;
    float rowH = 35.0f;
    
    for (size_t i = 0; i < queue.size(); ++i) {
        if (y + rowH > area.y + area.height) break;
        
        const auto& item = queue[i];
        bool isCurrent = (currentItem && currentItem->id == item.id);
        bool isHovered = (static_cast<int>(i) == m_hoveredQueueIndex);
        
        // Selection/Hover bg
        if (isCurrent || isHovered) {
             AestraUI::NUIColor hlColor = isCurrent ? 
                AestraUI::NUIColor(0.55f, 0.35f, 0.85f, 0.2f) : 
                AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.05f);
             renderer.fillRoundedRect(AestraUI::NUIRect(area.x + 5.0f, y, area.width - 10.0f, rowH - 2.0f), 4.0f, hlColor);
        }

        // Icon Column Logic:
        // Hover -> Play Icon
        // Not Hover -> Number (Green if current, Grey if not)
        if (isHovered) {
             renderer.drawText("▶", AestraUI::NUIPoint(colNo - 5.0f, y + 10.0f), 12.0f, AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 1.0f));
        } else {
             AestraUI::NUIColor numColor = isCurrent ? 
                AestraUI::NUIColor(0.1f, 0.8f, 0.3f, 1.0f) : 
                AestraUI::NUIColor(0.6f, 0.6f, 0.6f, 1.0f);
             renderer.drawText(std::to_string(i + 1), AestraUI::NUIPoint(colNo, y + 10.0f), 12.0f, numColor);
        }
        
        // Title
        AestraUI::NUIColor titleColor = isCurrent ? 
            AestraUI::NUIColor(0.1f, 0.8f, 0.3f, 1.0f) : 
            AestraUI::NUIColor(0.9f, 0.9f, 0.9f, 1.0f);
        renderer.drawText(item.title, AestraUI::NUIPoint(colTitle, y + 10.0f), 13.0f, titleColor);
        
        // Time
        std::string timeStr = (item.durationSeconds > 0.0) ? formatTime(item.durationSeconds) : "--:--";
        renderer.drawText(timeStr, AestraUI::NUIPoint(colTime, y + 10.0f), 12.0f, AestraUI::NUIColor(0.6f, 0.6f, 0.6f, 1.0f));
        
        y += rowH;
    }
}
// ============================================================================
// LAYOUT - Called from onResize
// ============================================================================

void AuditionPanel::layoutComponents() {
    auto bounds = getBounds();
    const float padding = 30.0f; // More breathing room
    
    // === 1. Header Area (Top 35%) ===
    float headerHeight = bounds.height * 0.35f;
    
    // Cover Art (Square within Header)
    float artSize = headerHeight - (padding * 2);
    if (artSize > 250.0f) artSize = 250.0f; // Max size
    
    // Info Area (Right of Art)
    float infoX = padding + artSize + 30.0f;
    float infoWidth = bounds.width - infoX - padding;
    float infoStartY = padding + 20.0f;
    
    // Title (Big!)
    m_trackTitle->setFontSize(48.0f); // Huge font
    m_trackTitle->setAlignment(AestraUI::NUILabel::Alignment::Left); // Left align
    m_trackTitle->setBounds(AestraUI::NUIAbsolute(bounds, infoX, infoStartY, infoWidth, 60.0f));
    
    // Artist (Medium)
    m_trackArtist->setFontSize(24.0f);
    m_trackArtist->setAlignment(AestraUI::NUILabel::Alignment::Left);
    m_trackArtist->setBounds(AestraUI::NUIAbsolute(bounds, infoX, infoStartY + 60.0f, infoWidth, 30.0f));
    
    // Controls (Below Artist)
    float controlsY = infoStartY + 60.0f + 30.0f + 20.0f;
    
    // Play Button (Big and prominent)
    float playSize = 64.0f;
    m_playPauseButton->setBounds(AestraUI::NUIAbsolute(bounds, infoX, controlsY, playSize, playSize));
    m_playPauseButton->setCornerRadius(playSize / 2.0f); // Circle
    
    // Transport Neighbors
    float btnSize = 40.0f;
    float gap = 15.0f;
    float txX = infoX + playSize + 20.0f;
    float txY = controlsY + (playSize - btnSize) / 2.0f;
    
    m_prevButton->setBounds(AestraUI::NUIAbsolute(bounds, txX, txY, btnSize, btnSize));
    m_nextButton->setBounds(AestraUI::NUIAbsolute(bounds, txX + btnSize + gap, txY, btnSize, btnSize));

    // DSP / Volume (Next to transport or new row?)
    // Let's put DSP next to transport
    float dspX = txX + (btnSize + gap) * 2 + 20.0f;
    m_dspPresetButton->setBounds(AestraUI::NUIAbsolute(bounds, dspX, txY + 5.0f, 100.0f, 30.0f)); 
    m_abToggleButton->setBounds(AestraUI::NUIAbsolute(bounds, dspX + 110.0f, txY + 5.0f, 50.0f, 30.0f));
    
    // Volume (Far right or right of DSP)
    m_volumeSlider->setBounds(AestraUI::NUIAbsolute(bounds, dspX + 180.0f, txY + 17.0f, 100.0f, 6.0f));
    
    
    // === 2. Waveform Area (Middle Strip 20%) ===
    float waveformY = headerHeight;
    float waveformHeight = bounds.height * 0.20f;
    
    // Progress Bar (Overlaid on bottom of waveform or between?)
    // Let's put it at the bottom of the Header for "Timeline" feel?
    // Or just top of Waveform.
    float progressY = waveformY - 10.0f; // Overlap slightly or just above
    // m_progressSlider->setBounds(AestraUI::NUIAbsolute(bounds, padding, progressY, bounds.width - padding*2, 6.0f));
    
    // Time Labels
    float timeY = waveformY + waveformHeight - 20.0f;
    m_currentTime->setBounds(AestraUI::NUIAbsolute(bounds, padding + 5.0f, timeY, 60.0f, 16.0f));
    m_totalTime->setBounds(AestraUI::NUIAbsolute(bounds, bounds.width - padding - 65.0f, timeY, 60.0f, 16.0f));
    
    
    // === 3. Queue Area (Bottom Rest) ===
    float queueY = waveformY + waveformHeight;
    float queueHeight = bounds.height - queueY - 10.0f;
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
    
    // === 1. Base Background (Black-ish) ===
    AestraUI::NUIColor bgColor(0.05f, 0.05f, 0.05f, 1.0f); // Deep dark
    renderer.fillRect(bounds, bgColor);
    
    // === 2. Adaptive Header Gradient ===
    float headerHeight = bounds.height * 0.35f;
    AestraUI::NUIRect headerRect(bounds.x, bounds.y, bounds.width, headerHeight);
    
    // Dynamic color from track title hash (inspo: Spotify adaptive header)
    m_currentHeaderColor = AestraUI::NUIColor(0.2f, 0.1f, 0.1f, 1.0f); // Fallback: Dark Red
    if (m_engine) {
        auto item = m_engine->getCurrentItem();
        if (item && !item->title.empty()) {
            size_t hash = std::hash<std::string>{}(item->title);
            float r = 0.2f + ((hash & 0xFF) / 255.0f) * 0.3f;
            float g = 0.1f + (((hash >> 8) & 0xFF) / 255.0f) * 0.2f;
            float b = 0.1f + (((hash >> 16) & 0xFF) / 255.0f) * 0.2f;
            m_currentHeaderColor = AestraUI::NUIColor(r, g, b, 1.0f);
        }
    }
    
    // Render Gradient (Color -> BaseColor)
    renderer.fillRectGradient(headerRect, m_currentHeaderColor, bgColor, true);
    
    // === 3. Cover Art Box ===
    const float padding = 30.0f;
    float artSize = headerHeight - (padding * 2);
    if (artSize > 250.0f) artSize = 250.0f;
    
    AestraUI::NUIRect artRect(bounds.x + padding, bounds.y + padding, artSize, artSize);
    
    // Shadow
    renderer.drawShadow(artRect, 0.0f, 10.0f, 20.0f, AestraUI::NUIColor(0.0f, 0.0f, 0.0f, 0.5f));
    
    // Check if we need to load new cover art
    bool hasCoverArt = false;
    if (m_engine) {
        auto item = m_engine->getCurrentItem();
        if (item && item->id != m_currentTrackId) {
            // Track changed, update cover art
            m_currentTrackId = item->id;
            
            // Clean up old texture
            if (m_coverArtTextureId != 0) {
                renderer.deleteTexture(m_coverArtTextureId);
                m_coverArtTextureId = 0;
            }
            
            // Load new cover art if available
            if (!item->coverArtData.empty()) {
                int w, h, comp;
                unsigned char* rgba = stbi_load_from_memory(
                    item->coverArtData.data(),
                    static_cast<int>(item->coverArtData.size()),
                    &w, &h, &comp, 4  // Request RGBA
                );
                
                if (rgba) {
                    m_coverArtTextureId = renderer.createTexture(rgba, w, h);
                    m_coverArtWidth = w;
                    m_coverArtHeight = h;
                    
                    // Extract dominant color for gradient (Sample center pixel)
                    int cx = w / 2;
                    int cy = h / 2;
                    int idx = (cy * w + cx) * 4;
                    float r = rgba[idx] / 255.0f;
                    float g = rgba[idx + 1] / 255.0f;
                    float b = rgba[idx + 2] / 255.0f;
                    // Darken for better gradient
                    m_currentHeaderColor = AestraUI::NUIColor(r * 0.5f, g * 0.5f, b * 0.5f, 1.0f);
                    
                    stbi_image_free(rgba);
                    Log::info("[AuditionPanel] Loaded cover art: " + std::to_string(w) + "x" + std::to_string(h));
                }
            }
        }
        hasCoverArt = (m_coverArtTextureId != 0);
    }
    
    // Draw Cover Art
    if (hasCoverArt) {
        // Draw the loaded texture
        AestraUI::NUIRect srcRect(0, 0, static_cast<float>(m_coverArtWidth), static_cast<float>(m_coverArtHeight));
        renderer.drawTexture(m_coverArtTextureId, artRect, srcRect);
    } else {
        // Placeholder Art Fill
        AestraUI::NUIColor artFill(m_currentHeaderColor.r * 1.2f, m_currentHeaderColor.g * 1.2f, m_currentHeaderColor.b * 1.2f, 1.0f);
        renderer.fillRoundedRect(artRect, 4.0f, artFill);
        renderer.drawText("AESTRA", AestraUI::NUIPoint(artRect.x + artSize/2.0f - 20.0f, artRect.y + artSize/2.0f), 12.0f, AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.3f));
    }

    
    
    // === 4. Waveform Area ===
    float waveformY = headerHeight;
    float waveformHeight = bounds.height * 0.20f;
    AestraUI::NUIRect waveformArea(bounds.x + padding, bounds.y + waveformY + 20.0f, bounds.width - padding*2, waveformHeight - 40.0f);
    
    renderWaveform(renderer, waveformArea);
    
    // === 5. Queue Area ===
    float queueY = waveformY + waveformHeight;
    float queueHeight = bounds.height - queueY;
    AestraUI::NUIRect queueArea(bounds.x + padding, bounds.y + queueY, bounds.width - padding*2, queueHeight - padding);
    
    renderQueue(renderer, queueArea);
    
    // Render children (Buttons, etc.)
    NUIComponent::onRender(renderer);

    // === Overlay SVGs on Buttons ===
    AestraUI::NUIColor iconColor(1.0f, 1.0f, 1.0f, 1.0f);
    
    // Play/Pause
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
    
    // Prev
    if (m_svgPrev) {
        auto btnBounds = m_prevButton->getBounds();
        float iconSize = btnBounds.width * 0.5f;
        AestraUI::NUIRect iconRect(
            btnBounds.x + (btnBounds.width - iconSize)/2,
            btnBounds.y + (btnBounds.height - iconSize)/2,
            iconSize, iconSize
        );
        AestraUI::NUISVGRenderer::render(renderer, *m_svgPrev, iconRect, iconColor);
    }
    
    // Next
    if (m_svgNext) {
        auto btnBounds = m_nextButton->getBounds();
        float iconSize = btnBounds.width * 0.5f;
        AestraUI::NUIRect iconRect(
            btnBounds.x + (btnBounds.width - iconSize)/2,
            btnBounds.y + (btnBounds.height - iconSize)/2,
            iconSize, iconSize
        );
        AestraUI::NUISVGRenderer::render(renderer, *m_svgNext, iconRect, iconColor);
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

    // Gradient Colors
    AestraUI::NUIColor playedColor = m_currentHeaderColor; 
    playedColor = AestraUI::NUIColor(playedColor.r + 0.2f, playedColor.g + 0.2f, playedColor.b + 0.2f, 1.0f);
    AestraUI::NUIColor unplayedColor(0.4f, 0.4f, 0.45f, 0.5f);

    const float pixelStride = 3.0f; // More spacing
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
            float h = maxAmp * halfHeight;
            if (h < 2.0f) h = 2.0f;
            if (h > halfHeight) h = halfHeight;
            
            float x = area.x + (i * pixelStride);
            AestraUI::NUIColor barColor = (x < playheadX) ? playedColor : unplayedColor;
            renderer.fillRoundedRect(AestraUI::NUIRect(x, centerY - h, 2.0f, h * 2.0f), 1.0f, barColor);
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
