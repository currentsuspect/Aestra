// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UnitRow.h"
#include "PluginBrowserPanel.h"
#include <chrono>
#include <algorithm>
#include <cmath>
#include "NUIThemeSystem.h"
#include "NUIRenderer.h"

#include "TrackManager.h"
#include "AudioEngine.h"
#include "../../AestraCore/include/AestraLog.h"

namespace AestraUI {

namespace {
bool usesStepSequencerForGroup(Aestra::Audio::UnitGroup group)
{
    return group == Aestra::Audio::UnitGroup::Drums || group == Aestra::Audio::UnitGroup::Audio;
}

bool usesStepSequencerForType(Aestra::Audio::UnitType type)
{
    return type == Aestra::Audio::UnitType::Sampler || type == Aestra::Audio::UnitType::PitchedSampler;
}

bool usesPianoRollForType(Aestra::Audio::UnitType type)
{
    return type == Aestra::Audio::UnitType::Instrument;
}

void drawUnitRowChip(NUIRenderer& renderer,
                     const NUIRect& rect,
                     const std::string& text,
                     const NUIColor& fill,
                     const NUIColor& stroke,
                     const NUIColor& textColor,
                     float fontSize = 9.0f)
{
    renderer.fillRoundedRect(rect, 5.0f, fill);
    renderer.strokeRoundedRect(rect, 5.0f, 1.0f, stroke);
    renderer.drawTextCentered(text, rect, fontSize, textColor);
}
}

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
        m_type = unit->type;
        m_isEnabled = unit->isEnabled;
        m_isArmed = unit->isArmed;
        m_isMuted = unit->isMuted;
        m_isSolo = unit->isSolo;
        m_audioClip = unit->audioClipPath.empty() ? "" : 
                      unit->audioClipPath.substr(unit->audioClipPath.find_last_of("/\\") + 1);
        m_pluginId = unit->pluginId;
        m_mixerChannel = unit->targetMixerRoute;
        m_audioDurationSeconds = unit->audioDurationSeconds;
        m_audioPreviewWaveform = unit->audioPreviewWaveform;

        switch (m_type) {
        case Aestra::Audio::UnitType::Sampler:
            m_groupLabel = "Sampler";
            break;
        case Aestra::Audio::UnitType::PitchedSampler:
            m_groupLabel = "808";
            break;
        case Aestra::Audio::UnitType::Instrument:
            m_groupLabel = "Instrument";
            break;
        case Aestra::Audio::UnitType::Audio:
            m_groupLabel = "Audio";
            break;
        default:
            m_groupLabel = "Sampler";
            break;
        }

        if (!m_pluginId.empty()) {
            m_sourceSummary = "Plugin";
        } else if (!m_audioClip.empty()) {
            m_sourceSummary = "Sample";
        } else {
            m_sourceSummary = "Empty";
        }
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

    // Render live for now. The widget cache path is currently producing
    // duplicate/local-space ghosts for Arsenal rows on Linux.
    drawContent(renderer);
    
    // Render dynamic children (Input Widget, Context Menu) on top
    // Push UnitRow position so children (relative coords) draw correctly
    renderer.pushTransform(bounds.x, bounds.y);
    renderChildren(renderer);
    renderer.popTransform();
}

void UnitRow::drawContent(NUIRenderer& renderer) {
    auto bounds = getBounds();
    auto& theme = NUIThemeManager::getInstance();

    NUIRect cardBounds = bounds;
    cardBounds.height = 56.0f;

    const float radius = 7.0f;
    NUIColor cardBg(0.101f, 0.101f, 0.141f, 1.0f);
    NUIColor border(1.0f, 1.0f, 1.0f, 0.08f);
    if (m_isDropHighlighted) {
        cardBg = theme.getColor("accentPrimary").withAlpha(0.18f);
        border = theme.getColor("accentPrimary").withAlpha(0.95f);
    } else if (m_isSelected) {
        cardBg = theme.getColor("accentPrimary").withAlpha(0.14f);
        border = theme.getColor("accentPrimary").withAlpha(0.55f);
    } else if (m_isHovered) {
        cardBg = NUIColor(0.12f, 0.12f, 0.17f, 1.0f);
        border = theme.getColor("accentPrimary").withAlpha(0.22f);
    }

    renderer.fillRoundedRect(cardBounds, radius, cardBg);
    renderer.strokeRoundedRect(cardBounds, radius, m_isDropHighlighted ? 2.0f : 1.0f, border);

    NUIRect dragRect(cardBounds.x + 6.0f, cardBounds.y + 6.0f, 32.0f, cardBounds.height - 12.0f);
    drawDragHandle(renderer, dragRect);

    NUIRect controlRect(cardBounds.x + 42.0f, cardBounds.y, std::min(310.0f, cardBounds.width * 0.42f), cardBounds.height);
    drawControlBlock(renderer, controlRect);

    float separatorX = controlRect.right() + 6.0f;
    renderer.drawLine(NUIPoint(separatorX, cardBounds.y + 8.0f),
                      NUIPoint(separatorX, cardBounds.y + cardBounds.height - 8.0f),
                      1.0f,
                      theme.getColor("borderSubtle").withAlpha(0.55f));

    NUIRect contextRect(separatorX + 8.0f, cardBounds.y + 6.0f, cardBounds.right() - separatorX - 14.0f, cardBounds.height - 12.0f);
    renderer.setClipRect(cardBounds);
    drawContextBlock(renderer, contextRect);
    renderer.clearClipRect();
}

void UnitRow::drawDragHandle(NUIRenderer& renderer, const NUIRect& bounds) {
    auto& theme = NUIThemeManager::getInstance();
    renderer.drawText("⠿", NUIPoint(bounds.x + 1.0f, bounds.y + 7.0f), 12.0f, theme.getColor("textSecondary").withAlpha(0.7f));
    renderer.drawText(std::to_string(static_cast<unsigned long long>(m_unitId)),
                      NUIPoint(bounds.x + 2.0f, bounds.y + 24.0f),
                      9.0f,
                      theme.getColor("textSecondary").withAlpha(0.56f));
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
    bool hasContent = !m_pluginId.empty() || !m_audioClip.empty();
    if (!hasContent && m_patternId.isValid()) {
        if (auto* pattern = m_trackManager->getPatternManager().getPattern(m_patternId); pattern && pattern->isMidi()) {
            const auto& midi = std::get<Aestra::Audio::MidiPayload>(pattern->payload);
            hasContent = std::any_of(midi.notes.begin(), midi.notes.end(), [this](const Aestra::Audio::MidiNote& note) {
                return note.unitId == m_unitId || note.unitId == 0;
            });
        }
    }
    const float centerY = bounds.y + bounds.height * 0.5f;
    const float nameX = bounds.x + 12.0f;

    renderer.fillCircle(NUIPoint(nameX, bounds.y + 18.0f),
                        3.0f,
                        m_isEnabled ? NUIColor(0.38f, 0.90f, 0.48f, 1.0f) : NUIColor(0.42f, 0.42f, 0.48f, 1.0f));

    std::string displayName = m_name.empty() ? ("Unit " + std::to_string(static_cast<unsigned long long>(m_unitId))) : m_name;
    renderer.drawText(displayName,
                      NUIPoint(nameX + 10.0f, bounds.y + 11.0f),
                      12.0f,
                      theme.getColor("textPrimary").withAlpha(0.92f));

    renderer.drawText(m_groupLabel,
                      NUIPoint(nameX + 10.0f, bounds.y + 27.0f),
                      9.0f,
                      theme.getColor("textSecondary").withAlpha(0.5f));

    const float pillY = centerY - 10.0f;
    const NUIRect muteRect(bounds.x + bounds.width - 92.0f, pillY, 24.0f, 20.0f);
    const NUIRect soloRect(bounds.x + bounds.width - 62.0f, pillY, 24.0f, 20.0f);
    drawMuteIcon(renderer, muteRect, m_isMuted);
    drawSoloIcon(renderer, soloRect, m_isSolo);

    const float indicatorX = bounds.right() - 18.0f;
    for (int i = 0; i < 3; ++i) {
        const float h = 4.0f + i * 3.0f;
        const NUIRect bar(indicatorX + i * 4.0f, bounds.y + 28.0f - h * 0.5f, 2.0f, h);
        const NUIColor color = hasContent
            ? theme.getColor("accentPrimary").withAlpha(0.75f - i * 0.12f)
            : theme.getColor("textDisabled").withAlpha(0.28f);
        renderer.fillRoundedRect(bar, 1.0f, color);
    }
}

void UnitRow::drawContextBlock(NUIRenderer& renderer, const NUIRect& bounds) {
    auto& theme = NUIThemeManager::getInstance();
    struct NoteSpan {
        int startStep{0};
        int endStep{0};
        int pitch{48};
        float velocity{1.0f};
    };

    const NUIRect contentCard(bounds.x, bounds.y, std::max(0.0f, bounds.width), std::max(0.0f, bounds.height));
    renderer.fillRoundedRect(contentCard, 6.0f, NUIColor(0.074f, 0.074f, 0.102f, 1.0f));

    const float lanePadding = 6.0f;
    const NUIRect timelineStrip(contentCard.x + lanePadding,
                                contentCard.y + lanePadding,
                                std::max(0.0f, contentCard.width - lanePadding * 2.0f),
                                std::max(0.0f, contentCard.height - lanePadding * 2.0f));
    
    // === Step Grid Layout ===
    float availWidth = timelineStrip.width;
    float stepWidth = std::max(availWidth / static_cast<float>(m_stepCount), 26.0f); // Min 26px
    float totalWidth = stepWidth * m_stepCount;
    
    // Clamp scroll
    float maxScroll = std::max(0.0f, totalWidth - availWidth);
    m_scrollX = std::max(0.0f, std::min(m_scrollX, maxScroll));
    
    // === Fetch Active Steps ===
    std::vector<int> activeSteps;
    std::vector<NoteSpan> noteSpans;
    std::vector<int> stepPitch(m_stepCount, -1);
    std::vector<float> stepLength(m_stepCount, 0.0f);
    bool unitHasContent = !m_pluginId.empty() || !m_audioClip.empty();
    const bool stepMode = usesStepSequencerForType(m_type);
    if (m_patternId.isValid()) {
        auto* pattern = m_trackManager->getPatternManager().getPattern(m_patternId);
        if (pattern && pattern->isMidi()) {
            auto& midi = std::get<Aestra::Audio::MidiPayload>(pattern->payload);
            for (const auto& note : midi.notes) {
                if (note.unitId != m_unitId && note.unitId != 0) continue;
                unitHasContent = true;
                const int startStep = std::clamp(static_cast<int>(std::floor(note.startBeat / 0.25 + 0.0001)), 0, m_stepCount - 1);
                const double endBeat = std::max(note.startBeat + 0.25, note.startBeat + note.durationBeats);
                const int endExclusive =
                    std::clamp(static_cast<int>(std::ceil(endBeat / 0.25 - 0.0001)), startStep + 1, m_stepCount);

                for (int step = startStep; step < endExclusive; ++step) {
                    activeSteps.push_back(step);
                }
                noteSpans.push_back(NoteSpan{startStep, endExclusive, note.pitch, note.velocity});
                stepPitch[startStep] = note.pitch;
                stepLength[startStep] = static_cast<float>(note.durationBeats);
            }
        }
    }
    std::sort(activeSteps.begin(), activeSteps.end());
    activeSteps.erase(std::unique(activeSteps.begin(), activeSteps.end()), activeSteps.end());

    if (m_type == Aestra::Audio::UnitType::Sampler) {
        const NUIColor inactiveFill(0.133f, 0.133f, 0.184f, 1.0f);
        const NUIColor inactiveStroke(1.0f, 1.0f, 1.0f, 0.10f);
        const NUIColor activeFill(0.486f, 0.361f, 0.749f, 1.0f);
        const float cellGap = 3.0f;
        const float cellRadius = 3.0f;
        const float cellHeight = std::max(10.0f, timelineStrip.height - 6.0f);
        const float cellY = timelineStrip.y + (timelineStrip.height - cellHeight) * 0.5f;

        for (int step = 0; step < m_stepCount; ++step) {
            const float cellX = timelineStrip.x + (step * stepWidth) - m_scrollX + (cellGap * 0.5f);
            const float cellWidth = std::max(8.0f, stepWidth - cellGap);
            if (cellX + cellWidth < timelineStrip.x || cellX > timelineStrip.right()) {
                continue;
            }

            const bool isActive = std::binary_search(activeSteps.begin(), activeSteps.end(), step);
            NUIRect cellRect(cellX, cellY, cellWidth, cellHeight);
            renderer.fillRoundedRect(cellRect, cellRadius, isActive ? activeFill : inactiveFill);
            renderer.strokeRoundedRect(cellRect, cellRadius, 1.0f, inactiveStroke);

            if ((step + 1) % 4 == 0 && step + 1 < m_stepCount) {
                const float markerX = cellRect.right() + (cellGap * 0.5f);
                renderer.drawLine(NUIPoint(markerX, timelineStrip.y + 2.0f),
                                  NUIPoint(markerX, timelineStrip.bottom() - 2.0f),
                                  1.0f,
                                  NUIColor(1.0f, 1.0f, 1.0f, 0.12f));
            }
        }
    } else if (m_type == Aestra::Audio::UnitType::PitchedSampler) {
        const NUIColor inactiveFill(0.133f, 0.133f, 0.184f, 1.0f);
        const NUIColor inactiveStroke(1.0f, 1.0f, 1.0f, 0.10f);
        const NUIColor activeFill(0.486f, 0.361f, 0.749f, 1.0f);
        const NUIColor pitchText = theme.getColor("textSecondary").withAlpha(0.72f);
        const float cellGap = 3.0f;
        const float topHeight = std::max(8.0f, timelineStrip.height * 0.58f);
        const float pitchLaneHeight = std::max(8.0f, timelineStrip.height * 0.24f);
        const float lengthLaneHeight = std::max(3.0f, timelineStrip.height * 0.10f);
        const float topY = timelineStrip.y;
        const float pitchY = topY + topHeight + 2.0f;
        const float lengthY = timelineStrip.bottom() - lengthLaneHeight - 1.0f;

        std::vector<NUIPoint> slidePoints;
        for (int step = 0; step < m_stepCount; ++step) {
            const float cellX = timelineStrip.x + (step * stepWidth) - m_scrollX + (cellGap * 0.5f);
            const float cellWidth = std::max(8.0f, stepWidth - cellGap);
            if (cellX + cellWidth < timelineStrip.x || cellX > timelineStrip.right()) {
                continue;
            }
            const bool isActive = std::binary_search(activeSteps.begin(), activeSteps.end(), step);
            NUIRect cellRect(cellX, topY, cellWidth, topHeight);
            renderer.fillRoundedRect(cellRect, 3.0f, isActive ? activeFill : inactiveFill);
            renderer.strokeRoundedRect(cellRect, 3.0f, 1.0f, inactiveStroke);

            const NUIRect pitchRect(cellX, pitchY, cellWidth, pitchLaneHeight);
            if (stepPitch[step] >= 0) {
                const int display = stepPitch[step] - 36;
                renderer.drawTextCentered((display >= 0 ? "+" : "") + std::to_string(display), pitchRect, 8.0f, pitchText);
                slidePoints.push_back(NUIPoint(pitchRect.x + pitchRect.width * 0.5f, pitchRect.y + pitchRect.height * 0.5f));
            } else {
                renderer.drawTextCentered("·", pitchRect, 8.0f, pitchText.withAlpha(0.35f));
            }

            const NUIRect lengthBg(cellX, lengthY, cellWidth, lengthLaneHeight);
            renderer.fillRoundedRect(lengthBg, 1.5f, inactiveStroke.withAlpha(0.35f));
            if (stepLength[step] > 0.0f) {
                const float ratio = std::clamp(stepLength[step] / 1.0f, 0.15f, 1.0f);
                NUIRect lengthFill(cellX, lengthY, std::max(2.0f, cellWidth * ratio), lengthLaneHeight);
                renderer.fillRoundedRect(lengthFill, 1.5f, activeFill.lightened(0.08f));
            }
        }

        for (size_t i = 1; i < slidePoints.size(); ++i) {
            renderer.drawLine(slidePoints[i - 1], slidePoints[i], 1.5f, activeFill.withAlpha(0.6f));
        }
    } else if (usesPianoRollForType(m_type) && !noteSpans.empty()) {
        for (int lane = 0; lane < 5; ++lane) {
            const float y = timelineStrip.y + ((lane + 1) * timelineStrip.height / 5.0f);
            renderer.drawLine(NUIPoint(timelineStrip.x, y),
                              NUIPoint(timelineStrip.right(), y),
                              1.0f,
                              NUIColor(1.0f, 1.0f, 1.0f, 0.07f));
        }

        int minPitch = noteSpans[0].pitch;
        int maxPitch = noteSpans[0].pitch;
        for (const auto& span : noteSpans) {
            minPitch = std::min(minPitch, span.pitch);
            maxPitch = std::max(maxPitch, span.pitch);
        }
        minPitch = std::max(0, minPitch - 2);
        maxPitch = std::min(127, maxPitch + 2);
        const int pitchRange = std::max(12, maxPitch - minPitch);
        const NUIColor noteFill(0.486f, 0.361f, 0.749f, 0.88f);
        const NUIColor noteStroke(1.0f, 1.0f, 1.0f, 0.10f);
        const float noteHeight = 4.0f;

        for (const auto& span : noteSpans) {
            const float startX = timelineStrip.x + (span.startStep * stepWidth) + 1.5f - m_scrollX;
            const float endX = timelineStrip.x + (span.endStep * stepWidth) - 1.5f - m_scrollX;
            const float width = std::max(6.0f, endX - startX);
            if (startX + width < timelineStrip.x || startX > timelineStrip.right()) {
                continue;
            }

            float normalizedPitch = static_cast<float>(span.pitch - minPitch) / static_cast<float>(pitchRange);
            float noteY = timelineStrip.bottom() - 2.0f - (normalizedPitch * (timelineStrip.height - noteHeight - 4.0f)) - noteHeight;
            NUIRect noteRect(startX, noteY, width, noteHeight);
            renderer.fillRoundedRect(noteRect, 2.0f, noteFill);
            renderer.strokeRoundedRect(noteRect, 2.0f, 1.0f, noteStroke);
        }
    } else if (m_type == Aestra::Audio::UnitType::Audio) {
        if (!m_audioPreviewWaveform.empty()) {
            const float midY = timelineStrip.y + timelineStrip.height * 0.5f;
            const float ampScale = std::max(4.0f, timelineStrip.height * 0.42f);
            const float binWidth = timelineStrip.width / static_cast<float>(m_audioPreviewWaveform.size());
            const NUIColor waveColor(0.486f, 0.361f, 0.749f, 0.9f);

            for (size_t i = 0; i < m_audioPreviewWaveform.size(); ++i) {
                const float x = timelineStrip.x + static_cast<float>(i) * binWidth;
                const float amp = std::clamp(m_audioPreviewWaveform[i], 0.02f, 1.0f) * ampScale;
                renderer.drawLine(NUIPoint(x, midY - amp), NUIPoint(x, midY + amp), std::max(1.0f, binWidth * 0.6f), waveColor);
            }
        }
    }

    if (!unitHasContent && m_isHovered) {
        std::string emptyLabel = "Drop a sample or open Step Editor";
        if (m_type == Aestra::Audio::UnitType::PitchedSampler) {
            emptyLabel = "Drop a sample - pitch and slide per step";
        } else if (m_type == Aestra::Audio::UnitType::Instrument) {
            emptyLabel = "Open Piano Roll to add notes";
        } else if (m_type == Aestra::Audio::UnitType::Audio) {
            emptyLabel = "Drop an audio clip";
        }
        renderer.drawTextCentered("+", NUIRect(timelineStrip.x, timelineStrip.y + 2.0f, timelineStrip.width, 10.0f), 12.0f, theme.getColor("textSecondary").withAlpha(0.72f));
        renderer.drawTextCentered(emptyLabel,
                                  NUIRect(timelineStrip.x, timelineStrip.y + 10.0f, timelineStrip.width, timelineStrip.height - 10.0f),
                                  9.0f,
                                  theme.getColor("textSecondary").withAlpha(0.72f));
    }
}

bool UnitRow::onMouseEvent(const NUIMouseEvent& event) {
    auto bounds = getBounds();
    const NUIRect localDragRect(6.0f, 6.0f, 32.0f, 44.0f);
    const NUIRect localControlRect(42.0f, 0.0f, std::min(310.0f, bounds.width * 0.42f), 56.0f);
    const float separatorX = localControlRect.right() + 6.0f;
    const NUIRect localContextRect(separatorX + 8.0f, 6.0f, std::max(0.0f, bounds.width - (separatorX + 14.0f)), 44.0f);
    const NUIPoint localPoint(event.position.x - bounds.x, event.position.y - bounds.y);

    auto resolveGridStep = [this](const NUIPoint& position, const NUIRect& gridBounds) -> int {
        float relativeX = position.x - gridBounds.x;
        float availWidth = gridBounds.width;
        float stepWidth = std::max(availWidth / static_cast<float>(m_stepCount), 26.0f);
        float contentX = relativeX + m_scrollX;
        return static_cast<int>(contentX / stepWidth);
    };

    auto updateStepEditSpan = [this](int endExclusive) {
        if (!m_patternId.isValid() || m_stepEditStart < 0 || endExclusive <= m_stepEditStart) {
            return;
        }

        m_stepEditEndExclusive = std::max(m_stepEditStart + 1, std::min(m_stepCount, endExclusive));
        const double targetStart = static_cast<double>(m_stepEditStart) * 0.25;
        const double targetDuration = static_cast<double>(m_stepEditEndExclusive - m_stepEditStart) * 0.25;

        auto& pm = m_trackManager->getPatternManager();
        pm.applyPatch(m_patternId, [this, targetStart, targetDuration](Aestra::Audio::PatternSource& p) {
            if (!p.isMidi()) return;
            auto& midi = std::get<Aestra::Audio::MidiPayload>(p.payload);
            auto it = std::find_if(midi.notes.begin(), midi.notes.end(),
                [this, targetStart](const Aestra::Audio::MidiNote& n) {
                    return n.unitId == m_unitId &&
                           n.pitch == 60 &&
                           std::abs(n.startBeat - targetStart) < 0.01;
                });
            if (it != midi.notes.end()) {
                it->durationBeats = targetDuration;
            }
        });
    };

    auto finishStepEdit = [this]() {
        if (!m_isStepEditing) {
            return;
        }
        m_isStepEditing = false;
        if (m_onPatternEdited && m_patternId.isValid()) {
            m_onPatternEdited(m_patternId);
        }
        invalidateVisuals();
    };

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
        // Only scroll if hovering over context area (right of controls)
        if (bounds.contains(event.position) && (event.position.x - bounds.x > m_controlWidth)) {
            float delta = event.wheelDelta;
            if (delta > 10.0f) delta = 1.0f;
            if (delta < -10.0f) delta = -1.0f;
            
            // If notes exist, scroll pitch viewport instead of horizontal
            bool hasNotes = false;
            if (m_patternId.isValid()) {
                auto* pattern = m_trackManager->getPatternManager().getPattern(m_patternId);
                if (pattern && pattern->isMidi()) {
                    auto& midi = std::get<Aestra::Audio::MidiPayload>(pattern->payload);
                    for (const auto& note : midi.notes) {
                        if (note.unitId == m_unitId || note.unitId == 0) {
                            hasNotes = true;
                            break;
                        }
                    }
                }
            }
            
            if (hasNotes) {
                // Pitch viewport scroll (up/down = see higher/lower pitches)
                m_minimapPitchOffset -= delta * 4.0f; // 4 semitones per tick
                m_minimapPitchOffset = std::clamp(m_minimapPitchOffset, -60.0f, 60.0f);
            } else {
                // Horizontal scroll for step grid
                m_scrollX -= delta * 40.0f;
            }
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
    
    if (m_isHovered && localContextRect.contains(localPoint)) {
        const int stepIndex = resolveGridStep(localPoint, localContextRect);
        if (stepIndex >= 0 && stepIndex < m_stepCount) {
            m_hoveredStep = stepIndex;
        }
    }
    
    if (wasHovered != m_isHovered || oldHoveredStep != m_hoveredStep) {
        invalidateVisuals();
    }

    if (m_isStepEditing) {
        if (event.released || event.type == NUIMouseEventType::Up) {
            finishStepEdit();
            return true;
        }

        const bool isPointerMoveWhileEditing =
            !event.pressed && !event.released &&
            (event.type == NUIMouseEventType::Drag ||
             event.type == NUIMouseEventType::Move ||
             event.button == NUIMouseButton::None);

        if (isPointerMoveWhileEditing) {
            int stepIndex = resolveGridStep(localPoint, localContextRect);
            stepIndex = std::max(m_stepEditStart, std::min(m_stepCount - 1, stepIndex));
            const int endExclusive = stepIndex + 1;
            if (endExclusive != m_stepEditEndExclusive) {
                updateStepEditSpan(endExclusive);
                invalidateVisuals();
            }
            return true;
        }
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
                if (localDragRect.contains(localPoint)) {
                    m_isDragging = true;
                    m_dragStartPos = event.position;
                    if (m_onDragStart) m_onDragStart(m_unitId);
                    invalidateVisuals();
                    return true;
                }
                else if (localControlRect.contains(localPoint)) {
                    handleControlClick(event, localControlRect);
                    return true;
                }
                else if (localContextRect.contains(localPoint)) {
                    handleContextClick(event, localContextRect);
                    return true;
                }
            }
        }
        
        // === Right-click ===
        if (event.button == NUIMouseButton::Right) {
            if (bounds.contains(event.position)) {
                if (localDragRect.contains(localPoint)) {
                    if (m_onRequestColorPicker) m_onRequestColorPicker();
                    return true;
                }
                if (localContextRect.contains(localPoint)) {
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
    const auto rowBounds = getBounds();
    const float rowLocalX = event.position.x - rowBounds.x;
    const float rowLocalY = event.position.y - rowBounds.y;
    const float localX = rowLocalX - bounds.x;
    const float localY = rowLocalY - bounds.y;
    const float dotCenterX = 12.0f;
    const float dotCenterY = 18.0f;
    const float dx = localX - dotCenterX;
    const float dy = localY - dotCenterY;
    if ((dx * dx + dy * dy) <= 36.0f) {
        m_manager.setUnitEnabled(m_unitId, !m_isEnabled);
        updateState();
        invalidateVisuals();
        return;
    }

    const NUIRect muteRect(bounds.width - 92.0f, bounds.height * 0.5f - 10.0f, 24.0f, 20.0f);
    const NUIRect soloRect(bounds.width - 62.0f, bounds.height * 0.5f - 10.0f, 24.0f, 20.0f);
    const NUIPoint localPoint(localX, localY);
    if (muteRect.contains(localPoint)) {
        m_manager.setUnitMute(m_unitId, !m_isMuted);
        updateState();
        invalidateVisuals();
        return;
    }
    if (soloRect.contains(localPoint)) {
        m_manager.setUnitSolo(m_unitId, !m_isSolo);
        updateState();
        invalidateVisuals();
        return;
    }

    auto now = std::chrono::steady_clock::now();
    long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    bool manualDoubleClick = (nowMs - m_lastClickTimeMs < 500);
    m_lastClickTimeMs = nowMs;
    if (manualDoubleClick || event.doubleClick) {
        startEditing(NUIRect(22.0f, 8.0f, std::max(120.0f, bounds.width - 120.0f), 22.0f));
    }
}

void UnitRow::handleContextClick(const NUIMouseEvent& event, const NUIRect& bounds) {
    const auto rowBounds = getBounds();
    const NUIPoint localPoint(event.position.x - rowBounds.x - bounds.x,
                              event.position.y - rowBounds.y - bounds.y);

    if (usesPianoRollForType(m_type)) {
        if (m_onOpenPatternEditor && m_patternId.isValid()) {
            m_onOpenPatternEditor(m_patternId);
        }
        return;
    }

    if (m_type == Aestra::Audio::UnitType::Audio) {
        if (m_onLoadUnitSample) {
            m_onLoadUnitSample(m_unitId);
        }
        return;
    }

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
    float relativeX = localPoint.x;
    float availWidth = bounds.width;
    float stepWidth = std::max(availWidth / static_cast<float>(m_stepCount), 26.0f);
    
    float contentX = relativeX + m_scrollX;
    int stepIndex = static_cast<int>(contentX / stepWidth);
    
    if (stepIndex >= 0 && stepIndex < m_stepCount && m_patternId.isValid()) {
        auto& pm = m_trackManager->getPatternManager();

        bool removedExisting = false;
        pm.applyPatch(m_patternId, [this, stepIndex, &removedExisting](Aestra::Audio::PatternSource& p) {
            if (!p.isMidi()) return;
            auto& midi = std::get<Aestra::Audio::MidiPayload>(p.payload);

            const double targetBeat = stepIndex * 0.25;
            auto it = std::find_if(midi.notes.begin(), midi.notes.end(),
                [this, targetBeat](const Aestra::Audio::MidiNote& n) {
                    const double endBeat = std::max(n.startBeat + 0.25, n.startBeat + n.durationBeats);
                    return n.unitId == m_unitId &&
                           n.pitch == 60 &&
                           targetBeat >= n.startBeat - 0.01 &&
                           targetBeat < endBeat - 0.01;
                });

            if (it != midi.notes.end()) {
                midi.notes.erase(it);
                removedExisting = true;
                return;
            }

            const int defaultPitch = (m_type == Aestra::Audio::UnitType::PitchedSampler) ? 36 : 60;
            midi.notes.push_back({defaultPitch, targetBeat, 0.25, 100.0f / 127.0f, m_unitId});
        });

        if (removedExisting) {
            if (m_onPatternEdited) {
                m_onPatternEdited(m_patternId);
            }
        } else {
            m_isStepEditing = true;
            m_stepEditStart = stepIndex;
            m_stepEditEndExclusive = stepIndex + 1;

            Aestra::Audio::AudioQueueCommand audCmd;
            audCmd.type = Aestra::Audio::AudioQueueCommandType::AuditionUnit;
            audCmd.trackIndex = m_unitId;
            audCmd.value1 = 0.8f;
            if (m_trackManager) {
                m_trackManager->pushAudioCommand(audCmd);
            }
        }
        
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
    auto markAccepted = [this]() {
        m_isDropHighlighted = true;
        invalidateVisuals();
        return DropFeedback::Copy;
    };

    if (data.type == DragDataType::Plugin && !data.sourceClipIdString.empty()) {
        if (const auto* plugin = std::any_cast<PluginListItem>(&data.customData)) {
            if (plugin->typeName != "Instrument") {
                return DropFeedback::None;
            }
        }
        return markAccepted();
    }

    // Accept file drags (audio files)
    if (data.type == DragDataType::File) {
        // Check if it's an audio file
        std::string path = data.filePath;
        std::string ext = path.substr(path.find_last_of('.') + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        if (ext == "wav" || ext == "mp3" || ext == "flac" || ext == "ogg" || ext == "aiff") {
            return markAccepted();
        }
    }
    return DropFeedback::None;
}

DropFeedback UnitRow::onDragOver(const DragData& data, const NUIPoint& position) {
    return onDragEnter(data, position);
}

void UnitRow::onDragLeave() {
    m_isDropHighlighted = false;
    invalidateVisuals();
}

DropResult UnitRow::onDrop(const DragData& data, const NUIPoint& position) {
    (void)position;
    DropResult result;
    m_isDropHighlighted = false;
    
    if (data.type == DragDataType::Plugin && !data.sourceClipIdString.empty()) {
        if (const auto* plugin = std::any_cast<PluginListItem>(&data.customData)) {
            if (plugin->typeName != "Instrument") {
                result.accepted = false;
                result.message = "Only instrument plugins can be dropped into Arsenal";
                invalidateVisuals();
                return result;
            }
        }

        if (m_onPluginDropped) {
            m_onPluginDropped(m_unitId, data.sourceClipIdString);
            updateState();
            invalidateVisuals();
            result.accepted = true;
            result.message = "Instrument loaded into Arsenal";
            Aestra::Log::info("[UnitRow] Plugin dropped: " + data.sourceClipIdString);
        }
        return result;
    }

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
    
    invalidateVisuals();
    return result;
}

NUIRect UnitRow::getDropBounds() const {
    return getBounds();
}

} // namespace AestraUI
