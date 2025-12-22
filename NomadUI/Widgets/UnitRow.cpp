// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UnitRow.h"
#include "../Core/NUIThemeSystem.h"
#include "../Graphics/NUIRenderer.h"

#include "../../NomadAudio/include/TrackManager.h"
#include "../../NomadCore/include/NomadLog.h"

namespace NomadUI {

UnitRow::UnitRow(std::shared_ptr<Nomad::Audio::TrackManager> trackManager, Nomad::Audio::UnitManager& manager, Nomad::Audio::UnitID unitId, Nomad::Audio::PatternID patternId)
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
}

void UnitRow::onRender(NUIRenderer& renderer) {
    auto bounds = getBounds();
    auto& theme = NUIThemeManager::getInstance();
    
    // "Premium Card" Style
    // Instead of a flat row, we draw a floating card with rounded corners.
    // We shrink the bounds slightly to create spacing/gap between rows.
    NUIRect cardBounds = bounds;
    cardBounds.height -= 4.0f; // 4px gap at bottom
    // cardBounds.x += 4.0f;
    // cardBounds.width -= 8.0f; // Side margins
    
    // Background: Deep Dark Grey/Black Card
    NUIColor cardBg = theme.getColor("surfaceRaised"); // slightly lighter than backgroundPrimary
    if (m_isHovered) {
        cardBg = cardBg.lightened(0.05f);
    }
    
    renderer.fillRoundedRect(cardBounds, 6.0f, cardBg);
    renderer.strokeRoundedRect(cardBounds, 6.0f, 1.0f, theme.getColor("borderSubtle"));
    
    // Glowing Color Strip (Left Edge)
    // Extract RGB from ARGB
    float r = ((m_color >> 16) & 0xFF) / 255.0f;
    float g = ((m_color >> 8) & 0xFF) / 255.0f;
    float b = (m_color & 0xFF) / 255.0f;
    NUIColor accentColor(r, g, b, 1.0f);
    
    // Draw a "Pill" strip on the left
    NUIRect stripRect(cardBounds.x + 4.0f, cardBounds.y + 4.0f, 4.0f, cardBounds.height - 8.0f);
    renderer.fillRoundedRect(stripRect, 2.0f, accentColor);
    
    // Shadow/Glow from strip
    if (m_isEnabled) {
        renderer.strokeRoundedRect(stripRect, 2.0f, 2.0f, accentColor.withAlpha(0.3f));
    }
    
    // Divide into Control (Left) and Context (Right)
    NUIRect controlRect(cardBounds.x + 12.0f, cardBounds.y, m_controlWidth - 8.0f, cardBounds.height);
    NUIRect contextRect(cardBounds.x + m_controlWidth + 4.0f, cardBounds.y, cardBounds.width - m_controlWidth - 8.0f, cardBounds.height);
    
    drawControlBlock(renderer, controlRect);
    
    // Separator (vertical line)
    renderer.drawLine(
        NUIPoint(contextRect.x - 2.0f, cardBounds.y + 6.0f),
        NUIPoint(contextRect.x - 2.0f, cardBounds.y + cardBounds.height - 6.0f),
        1.0f, theme.getColor("borderSubtle")
    );
    
    drawContextBlock(renderer, contextRect);
}

void UnitRow::drawControlBlock(NUIRenderer& renderer, const NUIRect& bounds) {
    auto& theme = NUIThemeManager::getInstance();
    float centerY = bounds.y + bounds.height * 0.5f;
    
    // Layout Cursor
    float x = bounds.x;
    
    // 1. Power (Toggle Switch Style)
    float iconSize = 14.0f;
    // NUIColor powerColor = m_isEnabled ? theme.getColor("accentPrimary") : theme.getColor("textDisabled");
    // renderer.fillCircle(NUIPoint(x + iconSize/2, centerY), 4.0f, powerColor);
    // renderer.strokeCircle(NUIPoint(x + iconSize/2, centerY), iconSize/2 + 2.0f, 1.0f, powerColor.withAlpha(0.5f));
    
    // Cleaner Power Icon
    NUIRect pwrRect(x, centerY - iconSize/2, iconSize, iconSize);
    NUIColor pwrColor = m_isEnabled ? theme.getColor("accentPrimary") : theme.getColor("textDisabled");
    renderer.drawText("P", NUIPoint(x+3, pwrRect.y+2), 10.0f, pwrColor); // Placeholder for proper icon if needed, or simple circle?
    // Let's stick to the filled circle for power, it's classic
    renderer.fillCircle(NUIPoint(x + 6.0f, centerY), 4.0f, pwrColor);
    if (m_isEnabled) renderer.strokeCircle(NUIPoint(x + 6.0f, centerY), 7.0f, 1.0f, pwrColor.withAlpha(0.4f));

    x += 24.0f; // Gap
    
    // 2. Arm (Record)
    NUIRect armRect(x, centerY - 8.0f, 16.0f, 16.0f);
    NUIColor armColor = m_isArmed ? theme.getColor("accentRed") : theme.getColor("textSecondary");
    renderer.strokeCircle(NUIPoint(x + 8.0f, centerY), 6.0f, 1.5f, armColor);
    if (m_isArmed) renderer.fillCircle(NUIPoint(x + 8.0f, centerY), 4.0f, armColor);
    
    x += 24.0f;
    
    // 3. Mute / Solo (Rounded Rect Buttons)
    float btnSize = 18.0f;
    
    // Mute
    NUIRect muteRect(x, centerY - btnSize/2, btnSize, btnSize);
    bool muteActive = m_isMuted;
    NUIColor muteBg = muteActive ? theme.getColor("accentOrange") : theme.getColor("backgroundPrimary");
    NUIColor muteText = muteActive ? theme.getColor("textPrimary") : theme.getColor("textSecondary");
    // renderer.fillRoundedRect(muteRect, 4.0f, muteBg);
    // renderer.strokeRoundedRect(muteRect, 4.0f, 1.0f, theme.getColor("borderSubtle"));
    if (muteActive) renderer.fillRoundedRect(muteRect, 4.0f, muteBg);
    else renderer.strokeRoundedRect(muteRect, 4.0f, 1.0f, theme.getColor("textDisabled"));
    
    renderer.drawText("M", NUIPoint(x+4, muteRect.y+4), 10.0f, muteText);
    
    x += 24.0f;
    
    // Solo
    NUIRect soloRect(x, centerY - btnSize/2, btnSize, btnSize);
    bool soloActive = m_isSolo;
    NUIColor soloBg = soloActive ? theme.getColor("accentYellow") : theme.getColor("backgroundPrimary");
    NUIColor soloText = soloActive ? NUIColor(0,0,0,1) : theme.getColor("textSecondary"); // Black text on yellow active
    
    if (soloActive) renderer.fillRoundedRect(soloRect, 4.0f, soloBg);
    else renderer.strokeRoundedRect(soloRect, 4.0f, 1.0f, theme.getColor("textDisabled"));

    renderer.drawText("S", NUIPoint(x+5, soloRect.y+4), 10.0f, soloText);
    
    x += 28.0f;
    
    // 4. Name Label (Stronger Font)
    renderer.drawText(m_name, NUIPoint(x, centerY - 7.0f), 14.0f, theme.getColor("textPrimary"));
    
    // 5. Info (Right Aligned in Control Block)
    float rightX = bounds.x + bounds.width - 4.0f;
    
    // Mixer Channel Tag
    if (m_mixerChannel >= 0) {
        std::string mixText = "CH " + std::to_string(m_mixerChannel + 1);
        float w = renderer.measureText(mixText, 9.0f).width + 8.0f;
        NUIRect tagRect(rightX - w, centerY - 8.0f, w, 16.0f);
        renderer.fillRoundedRect(tagRect, 3.0f, theme.getColor("backgroundSecondary"));
        renderer.strokeRoundedRect(tagRect, 3.0f, 1.0f, theme.getColor("borderSubtle"));
        renderer.drawText(mixText, NUIPoint(tagRect.x + 4.0f, centerY - 5.0f), 9.0f, theme.getColor("textSecondary"));
        rightX -= (w + 6.0f);
    }
    
    // Audio Clip Name
    if (!m_audioClip.empty()) {
        std::string clipText = m_audioClip;
        float w = renderer.measureText(clipText, 10.0f).width; // approximate
        // truncation logic omitted for brevity
        renderer.drawText(clipText, NUIPoint(rightX - w - 10.0f, centerY - 6.0f), 10.0f, theme.getColor("accentCyan"));
    }
}

void UnitRow::drawContextBlock(NUIRenderer& renderer, const NUIRect& bounds) {
    auto& theme = NUIThemeManager::getInstance();
    
    // "LED Pad" Grid - 16 Steps
    float stepWidth = bounds.width / 16.0f;
    // Center the pads vertically
    float padSize = std::min(stepWidth - 4.0f, bounds.height - 8.0f);
    float gridY = bounds.y + (bounds.height - padSize) / 2.0f;
    
    // Fetch active steps from pattern
    std::vector<int> activeSteps;
    if (m_patternId.isValid()) {
        auto* pattern = m_trackManager->getPatternManager().getPattern(m_patternId);
        if (pattern && pattern->isMidi()) {
            auto& midi = std::get<Nomad::Audio::MidiPayload>(pattern->payload);
            for (const auto& note : midi.notes) {
                if (note.unitId != m_unitId && note.unitId != 0) continue;
                if (note.pitch != 60) continue;
                int step = static_cast<int>(std::floor(note.startBeat / 0.25 + 0.1));
                if (step >= 0 && step < 16) activeSteps.push_back(step);
            }
        }
    }
    
    // Theme-based colors for step sequencer
    NUIColor stepInactiveColor = theme.getColor("stepInactive");
    NUIColor stepActiveColor = theme.getColor("stepActive");
    NUIColor stepBeatMarkerColor = theme.getColor("stepBeatMarker");
    NUIColor stepBarMarkerColor = theme.getColor("stepBarMarker");
    NUIColor stepGlowColor = theme.getColor("stepTriggerGlow");
    
    for (int i = 0; i < 16; ++i) {
        float stepX = bounds.x + (i * stepWidth) + (stepWidth - padSize) / 2.0f;
        NUIRect padRect(stepX, gridY, padSize, padSize);
        
        // Determine if this step is on a bar/beat boundary for visual hierarchy
        bool isBarStart = (i == 0 || i == 4 || i == 8 || i == 12);  // Every 4 beats (1 bar in 4/4)
        bool isBeatStart = (i % 4 == 0);
        
        // Background for inactive pads - subtle hierarchy
        NUIColor bgColor = stepInactiveColor;
        if (isBarStart) {
            bgColor = bgColor.lightened(0.08f);  // Bar markers slightly brighter
        } else if (isBeatStart) {
            bgColor = bgColor.lightened(0.04f);  // Beat markers subtle
        }
        
        renderer.fillRoundedRect(padRect, 3.0f, bgColor);
        
        // Border with hierarchy - bars get accent color border
        NUIColor borderColor = isBarStart ? stepBarMarkerColor.withAlpha(0.4f) : 
                               isBeatStart ? stepBeatMarkerColor : 
                               theme.getColor("borderSubtle").withAlpha(0.3f);
        renderer.strokeRoundedRect(padRect, 3.0f, 1.0f, borderColor);
        
        // Check if step is active
        bool isActive = false;
        for (int s : activeSteps) { if (s == i) { isActive = true; break; } }
        
        if (isActive) {
            // Glowing Active Pad - Premium Look
            renderer.fillRoundedRect(padRect, 3.0f, stepActiveColor);
            
            // Inner highlight (center shine)
            NUIRect innerRect(padRect.x + 2, padRect.y + 2, padSize - 4, padSize - 4);
            renderer.fillRoundedRect(innerRect, 2.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.25f));
            
            // Outer glow effect
            NUIRect glowRect(padRect.x - 2, padRect.y - 2, padSize + 4, padSize + 4);
            renderer.strokeRoundedRect(glowRect, 4.0f, 2.0f, stepGlowColor);
        }
    }
}

bool UnitRow::onMouseEvent(const NUIMouseEvent& event) {
    // Hover Logic
    bool wasHovered = m_isHovered;
    m_isHovered = getBounds().contains(event.position);
    if (wasHovered != m_isHovered) repaint();
    
    if (event.pressed && event.button == NUIMouseButton::Left) {
        auto bounds = getBounds();
        // Check if click is inside bounds
        if (bounds.contains(event.position)) {
            float relativeX = event.position.x - bounds.x;
            
            if (relativeX < m_controlWidth) {
                handleControlClick(event, NUIRect(bounds.x, bounds.y, m_controlWidth, bounds.height));
                return true;
            } else {
                handleContextClick(event, NUIRect(bounds.x + m_controlWidth, bounds.y, bounds.width - m_controlWidth, bounds.height));
                return true;
            }
        }
    }
    
    return false;
}

void UnitRow::handleControlClick(const NUIMouseEvent& event, const NUIRect& bounds) {
    float relativeX = event.position.x - bounds.x;
    
    // Simple hit testing based on fixed layout
    // Power: 8-20px
    if (relativeX >= 8.0f && relativeX <= 28.0f) {
        m_manager.setUnitEnabled(m_unitId, !m_isEnabled);
        updateState();
        repaint();
    }
    // Arm: 32-44px
    else if (relativeX >= 32.0f && relativeX <= 52.0f) {
        m_manager.setUnitArmed(m_unitId, !m_isArmed);
        updateState();
        repaint();
    }
    // Mute: 56-70px
    else if (relativeX >= 56.0f && relativeX <= 70.0f) {
        m_manager.setUnitMute(m_unitId, !m_isMuted);
        updateState();
        repaint();
    }
    // Solo: 74-88px
    else if (relativeX >= 74.0f && relativeX <= 88.0f) {
        m_manager.setUnitSolo(m_unitId, !m_isSolo);
        updateState();
        repaint();
        return;
    }
    
    // Mixer Channel (right side) - Click to cycle channels
    if (relativeX >= m_controlWidth - 40.0f && relativeX <= m_controlWidth - 10.0f) {
        int newChannel = (m_mixerChannel + 1) % 16;
        m_manager.setUnitMixerChannel(m_unitId, newChannel);
        updateState();
        repaint();
        return;
    }
    
    // Audio Clip area - Click to clear
    if (relativeX >= m_controlWidth - 160.0f && relativeX <= m_controlWidth - 50.0f && !m_audioClip.empty()) {
        m_manager.setUnitAudioClip(m_unitId, "");
        updateState();
        repaint();
        return;
    }
}

void UnitRow::handleContextClick(const NUIMouseEvent& event, const NUIRect& bounds) {
    // Grid Interaction: Toggle Steps
    float relativeX = event.position.x - bounds.x;
    float stepWidth = bounds.width / 16.0f;
    int stepIndex = static_cast<int>(relativeX / stepWidth);
    
    if (stepIndex >= 0 && stepIndex < 16 && m_patternId.isValid()) {
        Nomad::Log::info("Toggling Step: " + std::to_string(stepIndex));
        
        auto& pm = m_trackManager->getPatternManager();
        
        // Modify the shared pattern
        pm.applyPatch(m_patternId, [this, stepIndex](Nomad::Audio::PatternSource& p) {
            if (!p.isMidi()) return;
            auto& midi = std::get<Nomad::Audio::MidiPayload>(p.payload);
            
            // Look for existing note at this step for THIS unit
            double targetStart = stepIndex * 0.25;
            auto it = std::find_if(midi.notes.begin(), midi.notes.end(), 
                [this, targetStart](const Nomad::Audio::MidiNote& n) {
                    return n.unitId == m_unitId && 
                           n.pitch == 60 && 
                           std::abs(n.startBeat - targetStart) < 0.01;
                });
            
            if (it != midi.notes.end()) {
                // Remove note
                midi.notes.erase(it);
            } else {
                // Add new note (C3 / pitch 60, tagged with this unit's ID)
                midi.notes.push_back({targetStart, 0.25, 60, 100, m_unitId});
            }
        });
        
        repaint();
    }
}

} // namespace NomadUI
