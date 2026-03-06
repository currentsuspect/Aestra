// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UnitRow.h"
#include <chrono>
#include <algorithm>
#include "NUIThemeSystem.h"
#include "NUIRenderer.h"

#include "TrackManager.h"
#include "AudioEngine.h"
#include "../../AestraCore/include/AestraLog.h"

namespace AestraUI {

UnitRow::UnitRow(std::shared_ptr<Aestra::Audio::TrackManager> trackManager, Aestra::Audio::UnitManager& manager, Aestra::Audio::UnitID unitId, Aestra::Audio::PatternID patternId)
    : m_trackManager(trackManager), m_manager(manager), m_unitId(unitId), m_patternId(patternId)
{
    this->updateState();
}

void UnitRow::updateState() {
    auto* unit = m_manager.getUnit(m_unitId);
    if (unit) {
        m_name = unit->name;
        m_color = unit->color;
        m_group = unit->group;
        m_isEnabled = unit->isEnabled;
        m_isArmed = unit->isArmed;
        m_isMuted = unit->isMuted;
        m_isSolo = unit->isSolo;
        m_audioClip = unit->audioClipPath.empty() ? "" : 
                      unit->audioClipPath.substr(unit->audioClipPath.find_last_of("/\\") + 1);
        m_mixerChannel = unit->targetMixerRoute;
    }
    invalidateVisuals();
}

void UnitRow::onRender(NUIRenderer& renderer) {
    // Invalidate cache if flagged (e.g. state change or hover)
    if (m_needsCacheUpdate) {
        renderer.invalidateCache(reinterpret_cast<uint64_t>(this));
        m_needsCacheUpdate = false;
    }

    auto bounds = getBounds();
    
    // Render utilizing the FBO cache for performance (prevents popping/flicker on click)
    // The lambda is only executed if the cache is invalid.
    bool usedCache = renderer.renderCachedOrUpdate(reinterpret_cast<uint64_t>(this), bounds, [this, &renderer, bounds]() {
        // Transform absolute specific drawings to FBO-local space (0,0 based)
        // This ensures drawing at 'bounds.x' works correctly within the FBO texture.
        renderer.pushTransform(-bounds.x, -bounds.y);
        this->drawContent(renderer);
        renderer.popTransform();
    });
    
    // If caching isn't supported by the renderer, fallback to direct draw
    if (!usedCache) {
        drawContent(renderer);
    }
    
    // Render dynamic children (Input Widget, Context Menu) on top
    // Push UnitRow position so children (relative coords) draw correctly
    renderer.pushTransform(bounds.x, bounds.y);
    renderChildren(renderer);
    renderer.popTransform();
}

void UnitRow::drawContent(NUIRenderer& renderer) {
    auto bounds = getBounds();
    auto& theme = NUIThemeManager::getInstance();
    
    // === Premium Panel Style v3 (Deep Glass) ===
    NUIRect cardBounds = bounds;
    cardBounds.height -= 8.0f; // More spacing between rows
    
    float radius = theme.getRadius("radiusL");

    // Shadow/Glow behind the row
    if (m_isHovered || m_isDragging) {
        renderer.drawShadow(cardBounds, 0.0f, 4.0f, 16.0f, theme.getColor("primary").withAlpha(0.2f));
    } else {
        renderer.drawShadow(cardBounds, 0.0f, 2.0f, 8.0f, NUIColor::black().withAlpha(0.5f));
    }

    // Glass Background
    // Glass Background
    // Use 'surfaceTertiary' (Glass Surface) which is 0.15f grey, or 'glassHover' for transparency.
    // For rows, we want them to feel like plates floating on the void.
    // Let's use a custom glass color derived from panel background but with alpha.
    NUIColor cardBg = theme.getColor("surfaceTertiary").withAlpha(0.6f); 
    if (m_isHovered) cardBg = cardBg.lightened(0.1f).withAlpha(0.7f);
    
    renderer.fillRoundedRect(cardBounds, radius, cardBg);
    
    // Glass Border
    // Glass Border
    NUIColor borderColor = theme.getColor("glassBorder"); // 0.09f white
    if (m_isHovered) borderColor = theme.getColor("accentPrimary").withAlpha(0.5f);
    
    renderer.strokeRoundedRect(cardBounds, radius, 1.0f, borderColor);
    
    // === Unit Accent Color (from unit's color) ===
    float r = ((m_color >> 16) & 0xFF) / 255.0f;
    float g = ((m_color >> 8) & 0xFF) / 255.0f;
    float b = (m_color & 0xFF) / 255.0f;
    NUIColor accentColor(r, g, b, 1.0f);
    
    // === Drag Handle Area (left edge) ===
    NUIRect dragRect(cardBounds.x, cardBounds.y, DRAG_HANDLE_WIDTH, cardBounds.height);
    drawDragHandle(renderer, dragRect);
    
    // === Glowing Neon Strip ===
    float stripX = cardBounds.x + DRAG_HANDLE_WIDTH + 4.0f;
    NUIRect stripRect(stripX, cardBounds.y + 10.0f, 4.0f, cardBounds.height - 20.0f);
    
    // Inner light
    renderer.fillRoundedRect(stripRect, 2.0f, accentColor);
    
    // Outer neon glow
    if (m_isEnabled) {
        renderer.drawShadow(stripRect, 0, 0, 8.0f, accentColor.withAlpha(0.8f));
    }
    
    // === Control Block (left side controls) ===
    float controlStartX = stripX + COLOR_STRIP_WIDTH + 8.0f;
    NUIRect controlRect(controlStartX, cardBounds.y, m_controlWidth - DRAG_HANDLE_WIDTH - COLOR_STRIP_WIDTH - 16.0f, cardBounds.height);
    drawControlBlock(renderer, controlRect);
    
    // === Vertical Separator ===
    float separatorX = cardBounds.x + m_controlWidth;
    renderer.drawLine(
        NUIPoint(separatorX, cardBounds.y + 8.0f),
        NUIPoint(separatorX, cardBounds.y + cardBounds.height - 8.0f),
        1.0f, theme.getColor("borderSubtle")
    );
    
    // === Context Block (step sequencer grid) ===
    NUIRect contextRect(separatorX + 6.0f, cardBounds.y, cardBounds.width - m_controlWidth - 12.0f, cardBounds.height);
    drawContextBlock(renderer, contextRect);
}

void UnitRow::drawDragHandle(NUIRenderer& renderer, const NUIRect& bounds) {
    auto& theme = NUIThemeManager::getInstance();
    
    // Draw grip lines (3 horizontal lines)
    float centerY = bounds.y + bounds.height / 2.0f;
    float lineWidth = 8.0f;
    float lineX = bounds.x + (bounds.width - lineWidth) / 2.0f;
    float lineSpacing = 4.0f;
    
    NUIColor gripColor = m_isHovered ? theme.getColor("textSecondary") : theme.getColor("textDisabled");
    
    for (int i = -1; i <= 1; ++i) {
        float y = centerY + (i * lineSpacing);
        renderer.drawLine(
            NUIPoint(lineX, y),
            NUIPoint(lineX + lineWidth, y),
            1.5f, gripColor
        );
    }
}

void UnitRow::drawPowerIcon(NUIRenderer& renderer, const NUIRect& bounds, bool active) {
    auto& theme = NUIThemeManager::getInstance();
    float cx = bounds.x + bounds.width / 2.0f;
    float cy = bounds.y + bounds.height / 2.0f;
    float radius = bounds.width / 2.0f - 2.0f;
    
    NUIColor color = active ? theme.getColor("accentPrimary") : theme.getColor("textDisabled");
    
    // Outer circle
    renderer.strokeCircle(NUIPoint(cx, cy), radius, 1.5f, color);
    
    // Inner dot when active
    if (active) {
        renderer.fillCircle(NUIPoint(cx, cy), radius * 0.4f, color);
        // Glow
        renderer.strokeCircle(NUIPoint(cx, cy), radius + 2.0f, 1.5f, color.withAlpha(0.3f));
    }
}

void UnitRow::drawArmIcon(NUIRenderer& renderer, const NUIRect& bounds, bool active) {
    auto& theme = NUIThemeManager::getInstance();
    float cx = bounds.x + bounds.width / 2.0f;
    float cy = bounds.y + bounds.height / 2.0f;
    float radius = bounds.width / 2.0f - 2.0f;
    
    NUIColor color = active ? theme.getColor("accentRed") : theme.getColor("textSecondary");
    
    // Outer circle (record button style)
    renderer.strokeCircle(NUIPoint(cx, cy), radius, 1.5f, color);
    
    // Inner filled circle when armed
    if (active) {
        renderer.fillCircle(NUIPoint(cx, cy), radius * 0.6f, color);
    }
}

void UnitRow::drawMuteIcon(NUIRenderer& renderer, const NUIRect& bounds, bool active) {
    auto& theme = NUIThemeManager::getInstance();
    
    NUIColor bgColor = active ? NUIColor(1.0f, 0.6f, 0.2f, 1.0f) : theme.getColor("backgroundPrimary");
    NUIColor textColor = active ? NUIColor(0.0f, 0.0f, 0.0f, 1.0f) : theme.getColor("textSecondary");
    NUIColor borderColor = active ? bgColor : theme.getColor("textDisabled");
    
    // Circular Button
    float radius = theme.getRadius("radiusXS") * 2.0f; 
    renderer.fillRoundedRect(bounds, radius, bgColor);
    // Glow if active
    if (active) {
        renderer.drawShadow(bounds, 0, 0, 8.0f, bgColor.withAlpha(0.6f));
    } else {
        renderer.strokeRoundedRect(bounds, radius, 1.0f, borderColor);
    }
    
    // "M" label centered
    renderer.drawTextCentered("M", bounds, 10.0f, textColor);
}

void UnitRow::drawSoloIcon(NUIRenderer& renderer, const NUIRect& bounds, bool active) {
    auto& theme = NUIThemeManager::getInstance();
    
    NUIColor bgColor = active ? NUIColor(1.0f, 0.85f, 0.2f, 1.0f) : theme.getColor("backgroundPrimary");
    NUIColor textColor = active ? NUIColor(0.0f, 0.0f, 0.0f, 1.0f) : theme.getColor("textSecondary");
    NUIColor borderColor = active ? bgColor : theme.getColor("textDisabled");
    
    // Circular Button
    float radius = theme.getRadius("radiusXS") * 2.0f; 
    renderer.fillRoundedRect(bounds, radius, bgColor);
    // Glow if active
    if (active) {
        renderer.drawShadow(bounds, 0, 0, 8.0f, bgColor.withAlpha(0.6f));
    } else {
        renderer.strokeRoundedRect(bounds, radius, 1.0f, borderColor);
    }
    
    // "S" label centered
    renderer.drawTextCentered("S", bounds, 10.0f, textColor);
}

void UnitRow::drawGearIcon(NUIRenderer& renderer, const NUIRect& bounds, bool active) {
    auto& theme = NUIThemeManager::getInstance();
    
    NUIColor color = active ? theme.getColor("accentPrimary") : theme.getColor("textSecondary");
    if (!m_isHovered && !active) color = theme.getColor("textDisabled");
    
    float cx = bounds.x + bounds.width / 2.0f;
    float cy = bounds.y + bounds.height / 2.0f;
    float r = 6.0f;
    
    // Simple "Settings" cog visual
    renderer.strokeCircle(NUIPoint(cx, cy), r, 1.5f, color);
    renderer.fillCircle(NUIPoint(cx, cy), 2.0f, color);
    
    // Teeth (Cardinals)
    renderer.drawLine(NUIPoint(cx, cy - r - 2), NUIPoint(cx, cy + r + 2), 2.0f, color);
    renderer.drawLine(NUIPoint(cx - r - 2, cy), NUIPoint(cx + r + 2, cy), 2.0f, color);
}

void UnitRow::drawControlBlock(NUIRenderer& renderer, const NUIRect& bounds) {
    auto& theme = NUIThemeManager::getInstance();
    float centerY = bounds.y + bounds.height * 0.5f;
    float x = bounds.x;
    
    // === 1. Power Button ===
    NUIRect pwrRect(x, centerY - BUTTON_SIZE/2, BUTTON_SIZE, BUTTON_SIZE);
    drawPowerIcon(renderer, pwrRect, m_isEnabled);
    x += BUTTON_SIZE + BUTTON_SPACING;
    
    // === 2. Arm Button ===
    NUIRect armRect(x, centerY - BUTTON_SIZE/2, BUTTON_SIZE, BUTTON_SIZE);
    drawArmIcon(renderer, armRect, m_isArmed);
    x += BUTTON_SIZE + BUTTON_SPACING;
    
    // === 3. Mute Button ===
    NUIRect muteRect(x, centerY - BUTTON_SIZE/2, BUTTON_SIZE, BUTTON_SIZE);
    drawMuteIcon(renderer, muteRect, m_isMuted);
    x += BUTTON_SIZE + BUTTON_SPACING;
    
    // === 4. Solo Button ===
    NUIRect soloRect(x, centerY - BUTTON_SIZE/2, BUTTON_SIZE, BUTTON_SIZE);
    drawSoloIcon(renderer, soloRect, m_isSolo);
    x += BUTTON_SIZE + BUTTON_SPACING;

    // === 5. Edit Button (New) ===
    NUIRect gearRect(x, centerY - BUTTON_SIZE/2, BUTTON_SIZE, BUTTON_SIZE);
    drawGearIcon(renderer, gearRect, false); // Always "inactive" state unless toggled? Just use clickable style
    x += BUTTON_SIZE + BUTTON_SPACING + 4.0f;
    
    // === 5. Unit Name ===
    // Dynamic width calculation to fill space up to mixer channel
    float mixerRight = bounds.width - DRAG_HANDLE_WIDTH - COLOR_STRIP_WIDTH - 8.0f;
    float mixerBitStart = mixerRight - 50.0f; 
    float nameWidth = std::max(50.0f, mixerBitStart - x);

    if (!m_isEditingName) {
        // Truncate long names to fit available space
        std::string displayName = m_name;
        float maxNameWidth = nameWidth - 10.0f; // Leave some padding
        auto textSize = renderer.measureText(displayName, 13.0f);
        
        if (textSize.width > maxNameWidth && displayName.length() > 3) {
            // Binary search for truncation point
            while (textSize.width > maxNameWidth && displayName.length() > 3) {
                displayName = displayName.substr(0, displayName.length() - 1);
                textSize = renderer.measureText(displayName + "...", 13.0f);
            }
            displayName += "...";
        }
        
        float textY = centerY - (13.0f * 0.38f);
        renderer.drawText(displayName, NUIPoint(x, textY), 13.0f, theme.getColor("textPrimary"));
    }
    
    // === Right-aligned info (Mixer Channel only - sample name is now the unit name) ===
    float rightX = bounds.x + bounds.width - 4.0f;
    
    // Mixer Channel Tag
    if (m_mixerChannel >= 0) {
        std::string mixText = "CH " + std::to_string(m_mixerChannel + 1);
        float w = renderer.measureText(mixText, 10.0f).width + 10.0f;
        
        NUIRect tagRect(rightX - w, centerY - 9.0f, w, 18.0f);
        renderer.fillRoundedRect(tagRect, 4.0f, theme.getColor("backgroundSecondary"));
        renderer.strokeRoundedRect(tagRect, 4.0f, 1.0f, theme.getColor("borderSubtle"));
        
        float textY = renderer.calculateTextY(tagRect, 10.0f);
        renderer.drawText(mixText, NUIPoint(tagRect.x + 5.0f, textY), 10.0f, theme.getColor("textSecondary"));
    }
    // Note: Audio clip name display removed - the unit name already shows the loaded sample
}

void UnitRow::drawContextBlock(NUIRenderer& renderer, const NUIRect& bounds) {
    auto& theme = NUIThemeManager::getInstance();
    
    // === Extract unit's accent color for active steps ===
    float r = ((m_color >> 16) & 0xFF) / 255.0f;
    float g = ((m_color >> 8) & 0xFF) / 255.0f;
    float b = (m_color & 0xFF) / 255.0f;
    NUIColor unitAccent(r, g, b, 1.0f);
    
    // === Step Grid Layout ===
    // Use minimum step width to enable scrolling for high step counts
    float availWidth = bounds.width;
    float stepWidth = std::max(availWidth / static_cast<float>(m_stepCount), 26.0f); // Min 26px
    float totalWidth = stepWidth * m_stepCount;
    
    // Clamp scroll
    float maxScroll = std::max(0.0f, totalWidth - availWidth);
    m_scrollX = std::max(0.0f, std::min(m_scrollX, maxScroll));
    
    float padSize = stepWidth - PAD_SPACING;
    padSize = std::max(padSize, PAD_MIN_SIZE);
    // Cap pad size to height (minus margins)
    padSize = std::min(padSize, bounds.height - 10.0f);
    
    float gridY = bounds.y + (bounds.height - padSize) / 2.0f;
    
    // === Fetch Active Steps ===
    std::vector<int> activeSteps;
    if (m_patternId.isValid()) {
        auto* pattern = m_trackManager->getPatternManager().getPattern(m_patternId);
        if (pattern && pattern->isMidi()) {
            auto& midi = std::get<Aestra::Audio::MidiPayload>(pattern->payload);
            for (const auto& note : midi.notes) {
                if (note.unitId != m_unitId && note.unitId != 0) continue;
                if (note.pitch != 60) continue;
                int step = static_cast<int>(std::floor(note.startBeat / 0.25 + 0.1));
                if (step >= 0 && step < m_stepCount) activeSteps.push_back(step);
            }
        }
    }
    
    // Theme colors
    NUIColor stepInactiveColor = theme.getColor("stepInactive");
    NUIColor stepActiveColor = unitAccent; 
    NUIColor stepGlowColor = unitAccent.withAlpha(0.5f);
    
    // Clip to bounds to handle scrolling
    // Note: If NUIRenderer doesn't support nested clip well, we manually clip logic below.
    // We'll rely on manual visibility check.
    
    // === Render Each Step Pad ===
    // === Render Each Step Pad (Neon Grid) ===
    float padRadius = theme.getRadius("radiusS");
    
    for (int i = 0; i < m_stepCount; ++i) {
        float stepX = bounds.x + (i * stepWidth) + (stepWidth - padSize) / 2.0f - m_scrollX;
        
        // Visibility Culling
        if (stepX + padSize < bounds.x) continue;
        if (stepX > bounds.x + bounds.width) continue;
        
        NUIRect padRect(stepX, gridY, padSize, padSize);
        
        // Visual hierarchy markers
        bool isBarStart = (i % 4 == 0);
        bool isHovered = (i == m_hoveredStep);
        
        bool isActive = std::find(activeSteps.begin(), activeSteps.end(), i) != activeSteps.end();

        if (isActive) {
            // == ACTIVE PAD (ON) ==
            // Filled Neon
            renderer.fillRoundedRect(padRect, padRadius, unitAccent);
            // Inner Highlight
            renderer.fillRoundedRect(NUIAligned(padRect, 2,2,2, padRect.height/2), padRadius-1, 
                                     NUIColor::white().withAlpha(0.3f));
            // Glow
            renderer.drawShadow(padRect, 0, 0, 10.0f, unitAccent.withAlpha(0.6f));
        } else {
            // == INACTIVE PAD (OFF) ==
            // Empty Glass Style
            NUIColor padBg = theme.getColor("surfaceLight").withAlpha(0.3f);
            
            if (isHovered) {
                padBg = unitAccent.withAlpha(0.2f);
                renderer.drawShadow(padRect, 0, 0, 4.0f, unitAccent.withAlpha(0.2f));
            } else if (isBarStart) {
                padBg = theme.getColor("surfaceLight").withAlpha(0.5f);
            }
            
            renderer.fillRoundedRect(padRect, padRadius, padBg);
            
            // Border
            NUIColor borderColor = theme.getColor("border");
            if (isBarStart) borderColor = borderColor.withAlpha(0.3f);
            if (isHovered) borderColor = unitAccent.withAlpha(0.5f);
            
            renderer.strokeRoundedRect(padRect, padRadius, 1.0f, borderColor);
        }
    }
}

bool UnitRow::onMouseEvent(const NUIMouseEvent& event) {
    auto bounds = getBounds();

    // Forward events to active input widget (convert to local space)
    if (m_isEditingName && m_nameInput) {
        NUIMouseEvent localEvent = event;
        localEvent.position.x -= bounds.x;
        localEvent.position.y -= bounds.y;
        
        // Let input widget handle the event (and potentially take focus)
        if (m_nameInput->onMouseEvent(localEvent)) {
            return true;
        }
    }
    
    // === Scroll Handling ===
    if (std::abs(event.wheelDelta) > 0.0f) {
        // Only scroll if hovering over step grid
        if (bounds.contains(event.position) && (event.position.x - bounds.x > m_controlWidth)) {
            // Log for debugging
            std::cout << "[UnitRow] Scroll Delta: " << event.wheelDelta << std::endl;
            // Adjust sensitivity (assuming 1.0 delta usually, but safety clamp)
            float delta = event.wheelDelta;
            if (delta > 10.0f) delta = 1.0f; // Fix for raw 120 vs normalized 1
            if (delta < -10.0f) delta = -1.0f;
            
            m_scrollX -= delta * 40.0f; // 40px per 'tick'
            invalidateVisuals();
            return true;
        }
    }
    
    // === Hover Detection ===
    bool wasHovered = m_isHovered;
    m_isHovered = bounds.contains(event.position);
    
    // === Step Hover Detection (with Scroll) ===
    int oldHoveredStep = m_hoveredStep;
    m_hoveredStep = -1;
    
    if (m_isHovered) {
        float relativeX = event.position.x - bounds.x;
        if (relativeX > m_controlWidth) {
            // In step grid area
            float gridX = relativeX - m_controlWidth;
            
            // Calculate step metrics same as draw
            float availWidth = bounds.width - m_controlWidth;
            float stepWidth = std::max(availWidth / static_cast<float>(m_stepCount), 26.0f);
            
            // gridX is relative to view start. 
            // Pos in content = gridX + m_scrollX
            float contentX = gridX + m_scrollX;
            int stepIndex = static_cast<int>(contentX / stepWidth);
            
            if (stepIndex >= 0 && stepIndex < m_stepCount) {
                m_hoveredStep = stepIndex;
            }
        }
    }
    
    if (wasHovered != m_isHovered || oldHoveredStep != m_hoveredStep) {
        invalidateVisuals();
    }
    
    // === Click Handling ===
    if (event.pressed) {
        // Ensure focus
        if (bounds.contains(event.position)) {
             setFocused(true);
        }
    
        if (event.button == NUIMouseButton::Left) {
            // Note: NUITextInput handles its own focus loss/commit.
            // We just handle clicks on other elements.

            if (bounds.contains(event.position)) {
                float relativeX = event.position.x - bounds.x;
                
                // Drag handle area
                if (relativeX < DRAG_HANDLE_WIDTH) {
                    m_isDragging = true;
                    m_dragStartPos = event.position;
                    if (m_onDragStart) m_onDragStart(m_unitId);
                    invalidateVisuals();
                    return true;
                }
                // Control block
                else if (relativeX < m_controlWidth) {
                    handleControlClick(event, NUIRect(bounds.x, bounds.y, m_controlWidth, bounds.height));
                    return true;
                }
                // Step grid
                else {
                    handleContextClick(event, NUIRect(bounds.x + m_controlWidth, bounds.y, bounds.width - m_controlWidth, bounds.height));
                    return true;
                }
            }
        }
        
        // === Right-click ===
        if (event.button == NUIMouseButton::Right) {
            if (bounds.contains(event.position)) {
                float relativeX = event.position.x - bounds.x;
                // Color strip
                if (relativeX >= DRAG_HANDLE_WIDTH && relativeX < DRAG_HANDLE_WIDTH + COLOR_STRIP_WIDTH + 20.0f) {
                    if (m_onRequestColorPicker) m_onRequestColorPicker();
                    return true;
                }
                // Step Grid - Cycle Steps
                if (relativeX > m_controlWidth) {
                    m_stepCount = (m_stepCount == 16) ? 32 : (m_stepCount == 32) ? 64 : 16;
                    if (m_stepCount == 16) m_scrollX = 0; // Reset scroll for small count
                    invalidateVisuals();
                    return true;
                }
            }
        }
    }
    
    // === Release drag ===
    if (!event.pressed && m_isDragging) {
        m_isDragging = false;
        invalidateVisuals();
    }
    
    return false;
}

void UnitRow::startEditing(const NUIRect& rect) {
    if (m_isEditingName && m_nameInput) return;
    
    m_isEditingName = true;
    m_nameInput = std::make_shared<NUITextInput>(m_name);
    m_nameInput->setBounds(rect);
    // m_nameInput->setFontSize(13.0f); // Not supported
    m_nameInput->setTextColor(NUIThemeManager::getInstance().getColor("textPrimary"));
    m_nameInput->setBackgroundColor(NUIThemeManager::getInstance().getColor("inputBgDefault"));
    m_nameInput->setBorderColor(NUIThemeManager::getInstance().getColor("inputBorderFocus"));
    m_nameInput->setBorderWidth(1.0f);
    m_nameInput->setBorderRadius(3.0f);
    m_nameInput->setFocusedBorderColor(NUIThemeManager::getInstance().getColor("accentPrimary"));
    
    // Callbacks
    m_nameInput->setOnReturnKey([this]() { stopEditing(true); });
    m_nameInput->setOnEscapeKey([this]() { stopEditing(false); });
    // Note: onFocusLost might trigger when clicking grid, handled safely
    m_nameInput->setOnFocusLost([this]() { stopEditing(true); });
    
    addChild(m_nameInput);
    m_nameInput->setFocused(true);
    m_nameInput->setCaretPosition(static_cast<int>(m_name.length()));
    m_nameInput->selectAll();
    
    invalidateVisuals();
}

void UnitRow::stopEditing(bool save) {
    if (!m_nameInput) return;
    
    if (save) {
        std::string newName = m_nameInput->getText();
        if (newName != m_name) {
            m_manager.setUnitName(m_unitId, newName);
            updateState();
        }
    }
    
    removeChild(m_nameInput);
    m_nameInput.reset();
    m_isEditingName = false;
    invalidateVisuals();
}

void UnitRow::handleControlClick(const NUIMouseEvent& event, const NUIRect& bounds) {
    float relativeX = event.position.x - bounds.x - DRAG_HANDLE_WIDTH - COLOR_STRIP_WIDTH - 8.0f;
    
    // Button hit testing based on new layout
    float btnStartX = 0.0f;
    
    // Power: first button
    if (relativeX >= btnStartX && relativeX < btnStartX + BUTTON_SIZE) {
        m_manager.setUnitEnabled(m_unitId, !m_isEnabled);
        updateState();
        invalidateVisuals();
        return;
    }
    btnStartX += BUTTON_SIZE + BUTTON_SPACING;
    
    // Arm: second button
    if (relativeX >= btnStartX && relativeX < btnStartX + BUTTON_SIZE) {
        m_manager.setUnitArmed(m_unitId, !m_isArmed);
        updateState();
        invalidateVisuals();
        return;
    }
    btnStartX += BUTTON_SIZE + BUTTON_SPACING;
    
    // Mute: third button
    if (relativeX >= btnStartX && relativeX < btnStartX + BUTTON_SIZE) {
        m_manager.setUnitMute(m_unitId, !m_isMuted);
        updateState();
        invalidateVisuals();
        return;
    }
    btnStartX += BUTTON_SIZE + BUTTON_SPACING;
    
    // Solo: fourth button
    if (relativeX >= btnStartX && relativeX < btnStartX + BUTTON_SIZE) {
        m_manager.setUnitSolo(m_unitId, !m_isSolo);
        updateState();
        invalidateVisuals();
        return;
    }
    btnStartX += BUTTON_SIZE + BUTTON_SPACING;

    // Edit: fifth button
    if (relativeX >= btnStartX && relativeX < btnStartX + BUTTON_SIZE) {
        if (m_onEditUnit) m_onEditUnit(m_unitId);
        return;
    }
    btnStartX += BUTTON_SIZE + BUTTON_SPACING + 4.0f;
    
    // Name area: double-click to edit
    float mixerBitStart = bounds.width - DRAG_HANDLE_WIDTH - COLOR_STRIP_WIDTH - 8.0f - 50.0f; // Start of mixer channel area
    if (relativeX >= btnStartX && relativeX < mixerBitStart) {
        // Manual double-click detection (fallback if OS event fails)
        auto now = std::chrono::steady_clock::now();
        long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        bool manualDoubleClick = (nowMs - m_lastClickTimeMs < 500);
        
        // Update click time for next check
        m_lastClickTimeMs = nowMs;

        if (manualDoubleClick || event.doubleClick) {
            // Calculate absolute position for input widget
            float xOffset = DRAG_HANDLE_WIDTH + COLOR_STRIP_WIDTH + 8.0f;
            float absoluteX = xOffset + btnStartX;
            float width = mixerBitStart - btnStartX;
            float absoluteY = (bounds.height - 24.0f) / 2.0f + 1.0f;
            
            startEditing(NUIRect(absoluteX, absoluteY, width, 24.0f));
        }
        return;
    }
    
    // Mixer Channel (right side) - Click to cycle channels
    float rightBoundary = bounds.width - DRAG_HANDLE_WIDTH - COLOR_STRIP_WIDTH - 8.0f;
    
    // Clip Name / Waveform Area (Double-click to load sample)
    if (relativeX >= rightBoundary - 120.0f && relativeX <= rightBoundary) {
         float mixerStart = rightBoundary - 50.0f;
         if (relativeX < mixerStart) {
             // Double-click detection for sample loading
             auto now = std::chrono::steady_clock::now();
             long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
             bool isDoubleClick = (nowMs - m_lastClipClickTimeMs < 400);
             m_lastClipClickTimeMs = nowMs;
             
             if (isDoubleClick || event.doubleClick) {
                 // Trigger sample file picker
                 if (m_onLoadUnitSample) m_onLoadUnitSample(m_unitId);
             }
             return;
         }
    }

    if (relativeX >= rightBoundary - 50.0f && relativeX <= rightBoundary - 10.0f) {
        int newChannel = (m_mixerChannel + 1) % 16;
        m_manager.setUnitMixerChannel(m_unitId, newChannel);
        updateState();
        invalidateVisuals();
        return;
    }
}

void UnitRow::handleContextClick(const NUIMouseEvent& event, const NUIRect& bounds) {
    // === Double-click to load sample ===
    auto now = std::chrono::steady_clock::now();
    long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    bool isDoubleClick = (nowMs - m_lastClipClickTimeMs < 400) || event.doubleClick;
    m_lastClipClickTimeMs = nowMs;
    
    if (isDoubleClick) {
        // Trigger sample file picker
        if (m_onLoadUnitSample) m_onLoadUnitSample(m_unitId);
        return;
    }
    
    // === Single click: Grid Interaction - Toggle Steps ===
    float relativeX = event.position.x - bounds.x;
    float availWidth = bounds.width;
    float stepWidth = std::max(availWidth / static_cast<float>(m_stepCount), 26.0f);
    
    float contentX = relativeX + m_scrollX;
    int stepIndex = static_cast<int>(contentX / stepWidth);
    
    if (stepIndex >= 0 && stepIndex < m_stepCount && m_patternId.isValid()) {
        auto& pm = m_trackManager->getPatternManager();
        
        pm.applyPatch(m_patternId, [this, stepIndex](Aestra::Audio::PatternSource& p) {
            if (!p.isMidi()) return;
            auto& midi = std::get<Aestra::Audio::MidiPayload>(p.payload);
            
            // Look for existing note
            double targetStart = stepIndex * 0.25;
            auto it = std::find_if(midi.notes.begin(), midi.notes.end(), 
                [this, targetStart](const Aestra::Audio::MidiNote& n) {
                    return n.unitId == m_unitId && 
                           n.pitch == 60 && 
                           std::abs(n.startBeat - targetStart) < 0.01;
                });
            
            if (it != midi.notes.end()) {
                midi.notes.erase(it);
            } else {
                midi.notes.push_back({60, targetStart, 0.25, 100.0f / 127.0f, m_unitId});
                
                // [NEW] Audition Sound
                auto& engine = Aestra::Audio::AudioEngine::getInstance();
                Aestra::Audio::AudioQueueCommand audCmd;
                audCmd.type = Aestra::Audio::AudioQueueCommandType::AuditionUnit;
                audCmd.trackIndex = m_unitId;
                audCmd.value1 = 0.8f; // Velocity
                engine.commandQueue().push(audCmd);
            }
        });
        
        invalidateVisuals();
    }
}




bool UnitRow::onKeyEvent(const NUIKeyEvent& event) {
    if (m_isEditingName && m_nameInput) {
        // Forward to input widget
        return m_nameInput->onKeyEvent(event);
    }
    return false;
}

//==============================================================================
// IDropTarget Implementation
//==============================================================================

UnitRow::~UnitRow() {
    // Unregister from drop targets
    NUIDragDropManager::getInstance().unregisterDropTarget(this);
}

DropFeedback UnitRow::onDragEnter(const DragData& data, const NUIPoint& position) {
    (void)position;
    // Accept file drags (audio files)
    if (data.type == DragDataType::File) {
        // Check if it's an audio file
        std::string path = data.filePath;
        std::string ext = path.substr(path.find_last_of('.') + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        if (ext == "wav" || ext == "mp3" || ext == "flac" || ext == "ogg" || ext == "aiff") {
            invalidateVisuals();
            return DropFeedback::Copy;
        }
    }
    return DropFeedback::None;
}

DropFeedback UnitRow::onDragOver(const DragData& data, const NUIPoint& position) {
    return onDragEnter(data, position);
}

void UnitRow::onDragLeave() {
    invalidateVisuals();
}

DropResult UnitRow::onDrop(const DragData& data, const NUIPoint& position) {
    (void)position;
    DropResult result;
    
    if (data.type == DragDataType::File) {
        std::string path = data.filePath;
        std::string ext = path.substr(path.find_last_of('.') + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        if (ext == "wav" || ext == "mp3" || ext == "flac" || ext == "ogg" || ext == "aiff") {
            // Extract filename for unit name
            std::string filename = path.substr(path.find_last_of("/\\") + 1);
            // Remove extension
            filename = filename.substr(0, filename.find_last_of('.'));
            
            // Update unit name and load sample
            m_manager.setUnitName(m_unitId, filename);
            m_manager.setUnitAudioClip(m_unitId, path);
            
            // Trigger callback if set
            if (m_onSampleDropped) {
                m_onSampleDropped(m_unitId, path);
            }
            
            updateState();
            invalidateVisuals();
            
            result.accepted = true;
            result.message = "Sample loaded: " + filename;
            Aestra::Log::info("[UnitRow] Sample dropped: " + filename);
        }
    }
    
    return result;
}

NUIRect UnitRow::getDropBounds() const {
    return getBounds();
}

} // namespace AestraUI
