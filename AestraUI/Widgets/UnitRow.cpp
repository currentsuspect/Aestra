// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UnitRow.h"

#include "../../AestraCore/include/AestraLog.h"
#include "AudioEngine.h"
#include "Commands/AssignUnitToFirstFreeInsertCommand.h"
#include "Commands/SetUnitMixerChannelCommand.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "PluginBrowserPanel.h"
#include "TrackManager.h"

#include <algorithm>
#include <chrono>
#include <cmath>
namespace AestraUI {

namespace {
bool usesStepSequencerForType(Aestra::Audio::UnitType type) {
    return type == Aestra::Audio::UnitType::Sampler || type == Aestra::Audio::UnitType::PitchedSampler;
}

bool usesPianoRollForType(Aestra::Audio::UnitType type) {
    return type == Aestra::Audio::UnitType::Instrument;
}

NUIColor colorFromUnitValue(uint32_t value) {
    constexpr float scale = 1.0f / 255.0f;
    return NUIColor(static_cast<float>((value >> 16) & 0xff) * scale, static_cast<float>((value >> 8) & 0xff) * scale,
                    static_cast<float>(value & 0xff) * scale, 1.0f);
}

AestraUI::NUIComponent* getRootComponent(AestraUI::NUIComponent* component) {
    AestraUI::NUIComponent* root = component;
    while (root && root->getParent()) {
        root = root->getParent();
    }
    return root;
}

void detachContextMenu(const std::shared_ptr<AestraUI::NUIContextMenu>& menu) {
    if (!menu)
        return;
    if (auto* parent = menu->getParent()) {
        parent->removeChild(menu);
    }
}

void attachAndShowContextMenu(AestraUI::NUIComponent* owner, const std::shared_ptr<AestraUI::NUIContextMenu>& menu,
                              const AestraUI::NUIPoint& position) {
    if (!owner || !menu)
        return;
    AestraUI::NUIComponent* root = getRootComponent(owner);
    if (!root)
        root = owner;
    root->addChild(menu);
    menu->showAt(position);
    root->repaint();
}
} // namespace

UnitRow::UnitRow(std::shared_ptr<Aestra::Audio::TrackManager> trackManager, Aestra::Audio::UnitManager& manager,
                 Aestra::Audio::UnitID unitId, Aestra::Audio::PatternID patternId)
    : m_trackManager(trackManager), m_manager(manager), m_unitId(unitId), m_patternId(patternId) {
    this->updateState();
}

void UnitRow::updateState() {
    m_rootMidiNote = m_manager.getUnitRootMidiNote(m_unitId);
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
        m_audioClip =
            unit->audioClipPath.empty() ? "" : unit->audioClipPath.substr(unit->audioClipPath.find_last_of("/\\") + 1);
        m_pluginId = unit->pluginId;
        m_mixerChannelId = unit->targetMixerChannelId;
        m_mixerRouteShortLabel = "M";
        if (m_mixerChannelId != Aestra::Audio::MASTER_MIXER_CHANNEL_ID && m_trackManager) {
            bool resolved = false;
            for (size_t i = 0; i < m_trackManager->getChannelCount(); ++i) {
                const auto* channel = m_trackManager->getChannel(i);
                if (channel && channel->getChannelId() == m_mixerChannelId) {
                    m_mixerRouteShortLabel = std::to_string(i + 1);
                    resolved = true;
                    break;
                }
            }
            if (!resolved) {
                m_mixerRouteShortLabel = "!";
            }
        }
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
            m_groupLabel = "MIDI";
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

    const auto displayType = shouldUseNoteRoll() ? Aestra::Audio::UnitType::Instrument : m_type;

    // Ensure name label exists and stays in sync
    if (!m_nameLabel) {
        m_nameLabel = std::make_shared<UnitNameLabel>(m_name, displayType);
        addChild(m_nameLabel);
        layoutNameLabel();

        m_nameLabel->m_onOpenEditor = [this]() {
            if (m_onEditUnit)
                m_onEditUnit(m_unitId);
        };
        m_nameLabel->m_onRename = [this](const std::string& name) {
            if (m_onRenameUnit)
                m_onRenameUnit(m_unitId, name);
        };
    } else {
        m_nameLabel->setUnitName(m_name);
        m_nameLabel->setUnitType(displayType);
    }

    invalidateVisuals();
}

void UnitRow::setStepCount(int count) {
    m_stepCount = count;
    invalidateVisuals();
}

void UnitRow::setFitToWidth(bool fit) {
    if (fit == m_fitToWidth)
        return;
    m_fitToWidth = fit;
    if (fit)
        m_scrollX = 0.0f; // Nothing to page when the whole loop is shown
    invalidateVisuals();
}

void UnitRow::onRender(NUIRenderer& renderer) {
    if (m_nameLabel) {
        m_nameLabel->setUnitType(shouldUseNoteRoll() ? Aestra::Audio::UnitType::Instrument : m_type);
    }

    // Invalidate cache if flagged (e.g. state change or hover)
    if (m_needsCacheUpdate) {
        renderer.invalidateCache(reinterpret_cast<uint64_t>(this));
        m_needsCacheUpdate = false;
    }

    auto bounds = getBounds();

    // Stay inside the panel's list viewport: fully scrolled-out rows draw
    // nothing, partially visible ones are clipped in drawContent.
    const bool hasViewport = m_viewport.width > 0.0f && m_viewport.height > 0.0f;
    if (hasViewport && !bounds.intersects(m_viewport)) {
        return;
    }

    // Render live for now. The widget cache path is currently producing
    // duplicate/local-space ghosts for Arsenal rows on Linux.
    drawContent(renderer);

    // Render dynamic children (Input Widget, Context Menu) on top
    // Push UnitRow position so children (relative coords) draw correctly
    if (hasViewport)
        renderer.setClipRect(m_viewport);
    renderer.pushTransform(bounds.x, bounds.y);
    renderChildren(renderer);
    renderer.popTransform();
    if (hasViewport)
        renderer.clearClipRect();
}

void UnitRow::drawContent(NUIRenderer& renderer) {
    auto bounds = getBounds();
    auto& theme = NUIThemeManager::getInstance();

    NUIRect cardBounds = bounds;
    cardBounds.height = 56.0f;

    // Clip all painting to the card, further trimmed to the panel's list
    // viewport so rows scrolled past the panel edge don't spill over it.
    NUIRect paintClip = cardBounds;
    if (m_viewport.width > 0.0f && m_viewport.height > 0.0f) {
        const float clipTop = std::max(paintClip.y, m_viewport.y);
        const float clipBottom = std::min(paintClip.bottom(), m_viewport.bottom());
        paintClip.y = clipTop;
        paintClip.height = clipBottom - clipTop;
        if (paintClip.height <= 0.0f)
            return;
    }
    renderer.setClipRect(paintClip);

    const auto& themeProps = theme.getCurrentTheme();
    const float radius = std::max(0.0f, themeProps.radiusM - 1.0f);
    const NUIColor unitAccent = colorFromUnitValue(m_color);
    NUIColor cardBg = theme.getColor("backgroundSecondary");
    NUIColor border = theme.getColor("border").withAlpha(0.08f);
    if (m_isDropHighlighted) {
        cardBg = theme.getColor("accentPrimary").withAlpha(0.18f);
        border = theme.getColor("accentPrimary").withAlpha(0.95f);
    } else if (m_isSelected) {
        cardBg = unitAccent.withAlpha(0.06f);
        border = unitAccent.withAlpha(0.28f);
    } else if (m_isHovered) {
        cardBg = theme.getColor("surfaceRaised");
        border = theme.getColor("accentPrimary").withAlpha(0.22f);
    }

    renderer.fillRoundedRect(cardBounds, radius, cardBg);
    renderer.strokeRoundedRect(cardBounds, radius, m_isDropHighlighted ? 2.0f : 1.0f, border);
    renderer.fillRoundedRect({cardBounds.x + 1.0f, cardBounds.y + 8.0f, 4.0f, cardBounds.height - 16.0f}, 2.0f,
                             unitAccent.withAlpha(m_isEnabled ? 0.95f : 0.35f));

    // Density-derived control floor (responsive contract): narrow rows give
    // the grid the width back instead of clipping it against a fixed 220.
    m_controlWidth = std::clamp(cardBounds.width * 0.38f, controlFloorForDensity(cardBounds.width), 312.0f);

    NUIRect dragRect(cardBounds.x + 6.0f, cardBounds.y + 6.0f, 32.0f, cardBounds.height - 12.0f);
    drawDragHandle(renderer, dragRect);

    NUIRect controlRect(cardBounds.x + 42.0f, cardBounds.y, std::max(0.0f, m_controlWidth - 48.0f), cardBounds.height);
    drawControlBlock(renderer, controlRect);

    float separatorX = cardBounds.x + m_controlWidth;
    renderer.drawLine(NUIPoint(separatorX, cardBounds.y + 8.0f),
                      NUIPoint(separatorX, cardBounds.y + cardBounds.height - 8.0f), 1.0f,
                      theme.getColor("borderSubtle").withAlpha(0.55f));

    NUIRect contextRect(separatorX + 8.0f, cardBounds.y + 6.0f, cardBounds.right() - separatorX - 2.0f,
                        cardBounds.height - 12.0f);
    drawContextBlock(renderer, contextRect);
    renderer.clearClipRect();
}

void UnitRow::drawDragHandle(NUIRenderer& renderer, const NUIRect& bounds) {
    auto& theme = NUIThemeManager::getInstance();
    const NUIColor gripColor = theme.getColor("textSecondary").withAlpha(m_isHovered || m_isSelected ? 0.70f : 0.28f);
    const float clusterX = bounds.x + bounds.width * 0.5f - 3.0f;
    const float clusterY = bounds.y + bounds.height * 0.5f - 6.0f;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 2; ++col) {
            renderer.fillCircle({clusterX + static_cast<float>(col) * 6.0f, clusterY + static_cast<float>(row) * 6.0f},
                                1.35f, gripColor);
        }
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

void UnitRow::drawMuteIcon(NUIRenderer& renderer, const NUIRect& bounds, bool active, bool engaged) {
    auto& theme = NUIThemeManager::getInstance();

    NUIColor bgColor = active ? theme.getColor("warning") : theme.getColor("backgroundPrimary");
    NUIColor textColor = active
                             ? theme.getContrastColor(bgColor)
                             : (engaged ? theme.getColor("textSecondary")
                                        : theme.getColor("textDisabled").withAlpha(0.55f));
    NUIColor borderColor = active ? bgColor
                                  : (engaged ? theme.getColor("textDisabled")
                                             : theme.getColor("textDisabled").withAlpha(0.40f));

    // Circular Button
    float radius = theme.getRadius("xs") * 2.0f;
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

void UnitRow::drawSoloIcon(NUIRenderer& renderer, const NUIRect& bounds, bool active, bool engaged) {
    auto& theme = NUIThemeManager::getInstance();

    NUIColor bgColor = active ? theme.getColor("success") : theme.getColor("backgroundPrimary");
    NUIColor textColor = active
                             ? theme.getContrastColor(bgColor)
                             : (engaged ? theme.getColor("textSecondary")
                                        : theme.getColor("textDisabled").withAlpha(0.55f));
    NUIColor borderColor = active ? bgColor
                                  : (engaged ? theme.getColor("textDisabled")
                                             : theme.getColor("textDisabled").withAlpha(0.40f));

    // Circular Button
    float radius = theme.getRadius("xs") * 2.0f;
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
    if (!m_isHovered && !active)
        color = theme.getColor("textDisabled");

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

std::array<NUIRect, 3> UnitRow::controlPillRects(const NUIRect& controlBounds) const {
    const bool showBars = (m_density == Density::Full);
    const float routeW = (m_density == Density::Minimal) ? 24.0f : (m_density == Density::Compact) ? 34.0f : 42.0f;
    const float muteW = (m_density == Density::Minimal) ? 16.0f : (m_density == Density::Compact) ? 20.0f : 24.0f;
    const float soloW = (m_density == Density::Minimal) ? 16.0f : (m_density == Density::Compact) ? 20.0f : 24.0f;
    const float pillY = controlBounds.y + controlBounds.height * 0.5f - 10.0f;
    const float bandRight = controlBounds.right() - (showBars ? 18.0f : 4.0f);
    return {
        NUIRect(bandRight - soloW - 6.0f - muteW - 6.0f - routeW, pillY, routeW, 20.0f), // route
        NUIRect(bandRight - soloW - 6.0f - muteW, pillY, muteW, 20.0f),                   // mute
        NUIRect(bandRight - soloW, pillY, soloW, 20.0f),                                  // solo
    };
}

void UnitRow::drawControlBlock(NUIRenderer& renderer, const NUIRect& bounds) {
    auto& theme = NUIThemeManager::getInstance();
    const float nameX = bounds.x + 12.0f;
    const NUIColor unitAccent = colorFromUnitValue(m_color);
    const bool engaged = m_isHovered || m_isSelected;

    // Center the status dot on the primary name line ("Sampler 2"), using the
    // same line-height metric the renderer uses for vertical text centering,
    // so it reads as part of the label, not a top-attached status indicator.
    const float nameTopY = m_nameLabel ? m_nameLabel->getBounds().y + 3.0f : bounds.y + 11.0f;
    const float nameLineCenterY = nameTopY + renderer.getFontMetrics(12.0f).lineHeight * 0.5f;
    renderer.fillCircle(NUIPoint(nameX, nameLineCenterY), 3.0f,
                        m_isEnabled ? unitAccent : theme.getColor("textDisabled").withAlpha(0.45f));

    // Name + type label are rendered by the UnitNameLabel child component.

    const auto pills = controlPillRects(bounds);
    const NUIRect& routeRect = pills[0];
    const NUIRect& muteRect = pills[1];
    const NUIRect& soloRect = pills[2];
    const NUIColor routeFill = m_mixerChannelId == Aestra::Audio::MASTER_MIXER_CHANNEL_ID
                                   ? theme.getColor("surfaceTertiary")
                                   : unitAccent.withAlpha(engaged ? 0.16f : 0.07f);
    const NUIColor routeStroke =
        m_mixerRouteShortLabel == "!" ? theme.getColor("error").withAlpha(0.75f)
                                      : unitAccent.withAlpha(engaged ? 0.42f : 0.20f);
    renderer.fillRoundedRect(routeRect, 4.0f, routeFill);
    renderer.strokeRoundedRect(routeRect, 4.0f, 1.0f, routeStroke);
    renderer.drawTextCentered(m_mixerRouteShortLabel, routeRect, m_density == Density::Minimal ? 7.5f : 9.0f,
                              theme.getColor("textPrimary").withAlpha(engaged ? 0.9f : 0.45f));
    drawMuteIcon(renderer, muteRect, m_isMuted, engaged);
    drawSoloIcon(renderer, soloRect, m_isSolo, engaged);

}

void UnitRow::drawContextBlock(NUIRenderer& renderer, const NUIRect& bounds) {
    auto& theme = NUIThemeManager::getInstance();
    struct NoteSpan {
        int startStep{0};
        int endStep{0};
        double startBeat{0.0};
        double endBeat{0.0};
        int pitch{48};
        float velocity{1.0f};
    };

    const auto& themeProps = theme.getCurrentTheme();
    const NUIRect contentCard(bounds.x, bounds.y, std::max(0.0f, bounds.width), std::max(0.0f, bounds.height));
    renderer.fillRoundedRect(contentCard, themeProps.radiusS + 2.0f, theme.getColor("backgroundPrimary"));

    const float lanePadding = 6.0f;
    const NUIRect timelineStrip(contentCard.x + lanePadding, contentCard.y + lanePadding,
                                std::max(0.0f, contentCard.width - lanePadding * 2.0f),
                                std::max(0.0f, contentCard.height - lanePadding * 2.0f));

    // === Step Grid Layout ===
    float availWidth = timelineStrip.width;
    float stepWidth = gridStepWidth(availWidth);
    float totalWidth = stepWidth * m_stepCount + static_cast<float>((m_stepCount + 3) / 4) * kStepGroupGap;

    // Clamp scroll
    float maxScroll = std::max(0.0f, totalWidth - availWidth);
    m_scrollX = std::max(0.0f, std::min(m_scrollX, maxScroll));

    // === Fetch Active Steps ===
    std::vector<int> activeSteps;
    std::vector<NoteSpan> noteSpans;
    std::vector<int> stepPitch(m_stepCount, -1);
    std::vector<float> stepLength(m_stepCount, 0.0f);
    std::vector<float> stepVelocity(m_stepCount, kDefaultStepVelocity);
    bool unitHasContent = !m_pluginId.empty() || !m_audioClip.empty();
    bool noteRollMode = usesPianoRollForType(m_type);
    double visibleLengthBeats = static_cast<double>(m_stepCount) * 0.25;
    if (m_patternId.isValid()) {
        auto* pattern = m_trackManager->getPatternManager().getPattern(m_patternId);
        if (pattern && pattern->isMidi()) {
            visibleLengthBeats = std::max(0.25, pattern->lengthBeats);
            auto& midi = std::get<Aestra::Audio::MidiPayload>(pattern->payload);
            for (const auto& note : midi.notes) {
                if (note.unitId != m_unitId && note.unitId != 0)
                    continue;
                unitHasContent = true;
                if (m_type == Aestra::Audio::UnitType::Sampler &&
                    (note.pitch != m_rootMidiNote || std::abs(note.durationBeats - 0.25) > 0.01)) {
                    noteRollMode = true;
                }
                const int startStep =
                    std::clamp(static_cast<int>(std::floor(note.startBeat / 0.25 + 0.0001)), 0, m_stepCount - 1);
                const double endBeat = std::max(note.startBeat + 0.25, note.startBeat + note.durationBeats);
                const int endExclusive =
                    std::clamp(static_cast<int>(std::ceil(endBeat / 0.25 - 0.0001)), startStep + 1, m_stepCount);

                for (int step = startStep; step < endExclusive; ++step) {
                    activeSteps.push_back(step);
                }
                noteSpans.push_back(
                    NoteSpan{startStep, endExclusive, note.startBeat, endBeat, note.pitch, note.velocity});
                stepPitch[startStep] = note.pitch;
                stepLength[startStep] = static_cast<float>(note.durationBeats);
                stepVelocity[startStep] = note.velocity;
            }
        }
    }
    std::sort(activeSteps.begin(), activeSteps.end());
    activeSteps.erase(std::unique(activeSteps.begin(), activeSteps.end()), activeSteps.end());

    // Live playhead within the pattern — used to light up the step / note the
    // transport is currently over so the user can track it and time their edits.
    const double playBeat = playheadBeatInPattern();
    const int playStep = (playBeat >= 0.0) ? static_cast<int>(std::floor(playBeat / 0.25)) : -1;
    const NUIColor playheadAccent = theme.getColor("accentPrimary");

    if (m_type == Aestra::Audio::UnitType::Sampler && !noteRollMode) {
        const NUIColor inactiveFill = theme.getColor("surfaceTertiary").lightened(0.05f);
        const NUIColor inactiveStroke = theme.getColor("border").withAlpha(0.14f);
        const NUIColor activeFill = theme.getColor("primary");
        const float cellGap = 3.0f;
        const float cellRadius = 3.0f;
        const float cellHeight = std::max(10.0f, timelineStrip.height - 6.0f);
        const float cellY = timelineStrip.y + (timelineStrip.height - cellHeight) * 0.5f;

        for (int step = 0; step < m_stepCount; ++step) {
            const float cellX = timelineStrip.x + (step * stepWidth) + static_cast<float>(step / 4) * kStepGroupGap - m_scrollX +
                        (cellGap * 0.5f);
            // Do not exceed the step advance in narrow layouts (triage 2026-08-14).
            const float cellWidth = std::min(stepWidth, std::max(2.0f, stepWidth - cellGap));
            if (cellX + cellWidth < timelineStrip.x || cellX > timelineStrip.right()) {
                continue;
            }

            const bool isActive = std::binary_search(activeSteps.begin(), activeSteps.end(), step);
            const bool isPlayhead = (step == playStep);
            const bool isSelected = isStepSelected(step);
            NUIRect cellRect(cellX, cellY, cellWidth, cellHeight);
            NUIColor cellFill = isActive ? activeFill : inactiveFill;
            if (((step / 4) & 1) != 0) {
                cellFill = NUIColor::lerp(cellFill, theme.getColor("accentPrimary"), 0.10f);
            }
            if (isPlayhead && isActive) {
                cellFill = activeFill.lightened(0.35f); // note lights up under the playhead
            }
            if (isSelected) {
                cellFill = cellFill.lightened(0.28f); // selection brightens the note
            }
            if (isActive) {
                // Velocity meter: dim base + a brighter bar rising from the
                // bottom to the note's velocity, so accents read at a glance
                // and vertical drag has an obvious target.
                renderer.fillRoundedRect(cellRect, cellRadius, cellFill.withAlpha(0.26f));
                const float v = std::clamp(stepVelocity[step], kMinStepVelocity, 1.0f);
                const float fillH = std::max(2.0f, cellHeight * v);
                const NUIRect velRect(cellX, cellY + cellHeight - fillH, cellWidth, fillH);
                renderer.fillRoundedRect(velRect, cellRadius, cellFill);
            } else {
                renderer.fillRoundedRect(cellRect, cellRadius, cellFill);
            }
            renderer.strokeRoundedRect(cellRect, cellRadius, 1.0f, inactiveStroke);
            if (isActive && !isSelected) {
                // Subtle inner rim keeps adjacent active pads legible as
                // individual steps instead of one merged bar.
                renderer.strokeRoundedRect(cellRect, cellRadius, 1.0f,
                                           activeFill.lightened(0.25f).withAlpha(0.30f));
            }
            if (isSelected) {
                // Selection halo: expanded light ring, distinct from the
                // playhead (on-edge accent ring) and the active rim.
                const NUIRect haloRect(cellRect.x - 1.5f, cellRect.y - 1.5f,
                                       cellRect.width + 3.0f, cellRect.height + 3.0f);
                renderer.strokeRoundedRect(haloRect, cellRadius + 1.5f, 1.5f,
                                           theme.getColor("textPrimary").withAlpha(0.9f));
            }
            if ((step + 1) % 4 == 0 && step + 1 < m_stepCount) {
                const float seamX = cellRect.right() + (cellGap + kStepGroupGap) * 0.5f;
                renderer.drawLine(NUIPoint(seamX, timelineStrip.y + 2.0f),
                                  NUIPoint(seamX, timelineStrip.bottom() - 2.0f), 1.0f,
                                  theme.getColor("border").withAlpha(0.18f));
            }
            if (isPlayhead) {
                renderer.strokeRoundedRect(cellRect, cellRadius, 1.5f,
                                           playheadAccent.withAlpha(isActive ? 0.95f : 0.45f));
            }

        }
    } else if (m_type == Aestra::Audio::UnitType::PitchedSampler) {
        const NUIColor inactiveFill = theme.getColor("surfaceTertiary").lightened(0.05f);
        const NUIColor inactiveStroke = theme.getColor("border").withAlpha(0.14f);
        const NUIColor activeFill = theme.getColor("primary");
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
            const float cellX = timelineStrip.x + (step * stepWidth) + static_cast<float>(step / 4) * kStepGroupGap - m_scrollX +
                        (cellGap * 0.5f);
            // Do not exceed the step advance in narrow layouts (triage 2026-08-14).
            const float cellWidth = std::min(stepWidth, std::max(2.0f, stepWidth - cellGap));
            if (cellX + cellWidth < timelineStrip.x || cellX > timelineStrip.right()) {
                continue;
            }
            const bool isActive = std::binary_search(activeSteps.begin(), activeSteps.end(), step);
            const bool isPlayhead = (step == playStep);
            const bool isSelected = isStepSelected(step);
            NUIRect cellRect(cellX, topY, cellWidth, topHeight);
            NUIColor cellFill = isActive ? activeFill : inactiveFill;
            if (((step / 4) & 1) != 0) {
                cellFill = NUIColor::lerp(cellFill, theme.getColor("accentPrimary"), 0.10f);
            }
            if (isPlayhead && isActive) {
                cellFill = activeFill.lightened(0.35f);
            }
            if (isSelected) {
                cellFill = cellFill.lightened(0.28f); // selection brightens the note
            }
            renderer.fillRoundedRect(cellRect, 3.0f, cellFill);
            renderer.strokeRoundedRect(cellRect, 3.0f, 1.0f, inactiveStroke);
            if (isSelected) {
                const NUIRect haloRect(cellRect.x - 1.5f, cellRect.y - 1.5f,
                                       cellRect.width + 3.0f, cellRect.height + 3.0f);
                renderer.strokeRoundedRect(haloRect, 4.5f, 1.5f,
                                           theme.getColor("textPrimary").withAlpha(0.9f));
            }
            if ((step + 1) % 4 == 0 && step + 1 < m_stepCount) {
                const float seamX = cellRect.right() + (cellGap + kStepGroupGap) * 0.5f;
                renderer.drawLine(NUIPoint(seamX, timelineStrip.y + 2.0f),
                                  NUIPoint(seamX, timelineStrip.bottom() - 2.0f), 1.0f,
                                  theme.getColor("border").withAlpha(0.18f));
            }
            if (isPlayhead) {
                renderer.strokeRoundedRect(cellRect, 3.0f, 1.5f, playheadAccent.withAlpha(isActive ? 0.95f : 0.45f));
            }

            const NUIRect pitchRect(cellX, pitchY, cellWidth, pitchLaneHeight);
            if (stepPitch[step] >= 0) {
                const int display = stepPitch[step] - m_rootMidiNote; // semitones from root
                renderer.drawTextCentered((display >= 0 ? "+" : "") + std::to_string(display), pitchRect, 8.0f,
                                          pitchText);
                slidePoints.push_back(
                    NUIPoint(pitchRect.x + pitchRect.width * 0.5f, pitchRect.y + pitchRect.height * 0.5f));
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
    } else if (noteRollMode) {
        for (int lane = 0; lane < 5; ++lane) {
            const float y = timelineStrip.y + ((lane + 1) * timelineStrip.height / 5.0f);
            renderer.drawLine(NUIPoint(timelineStrip.x, y), NUIPoint(timelineStrip.right(), y), 1.0f,
                              theme.getColor("border").withAlpha(0.07f));
        }

        const double barLineBeats =
            m_trackManager ? static_cast<double>(m_trackManager->getTimelineClock().getBeatsPerBar()) : 4.0;
        for (double beat = barLineBeats; beat < visibleLengthBeats; beat += barLineBeats) {
            const float x = timelineStrip.x + static_cast<float>(beat / visibleLengthBeats) * totalWidth - m_scrollX;
            renderer.drawLine(NUIPoint(x, timelineStrip.y), NUIPoint(x, timelineStrip.bottom()), 1.0f,
                              theme.getColor("border").withAlpha(0.10f));
        }

        int minPitch = noteSpans.empty() ? 60 : noteSpans[0].pitch;
        int maxPitch = noteSpans.empty() ? 72 : noteSpans[0].pitch;
        for (const auto& span : noteSpans) {
            minPitch = std::min(minPitch, span.pitch);
            maxPitch = std::max(maxPitch, span.pitch);
        }
        minPitch = std::max(0, minPitch - 2);
        maxPitch = std::min(127, maxPitch + 2);
        const int pitchRange = std::max(12, maxPitch - minPitch);
        const NUIColor noteFill = theme.getColor("primary").withAlpha(0.88f);
        const NUIColor noteStroke = theme.getColor("border").withAlpha(0.10f);
        const float noteHeight = 4.0f;

        for (const auto& span : noteSpans) {
            const float startX = timelineStrip.x +
                                 static_cast<float>(span.startBeat / visibleLengthBeats) * totalWidth + 1.5f -
                                 m_scrollX;
            const float endX =
                timelineStrip.x + static_cast<float>(span.endBeat / visibleLengthBeats) * totalWidth - 1.5f - m_scrollX;
            const float width = std::max(6.0f, endX - startX);
            if (startX + width < timelineStrip.x || startX > timelineStrip.right()) {
                continue;
            }

            float normalizedPitch = static_cast<float>(span.pitch - minPitch) / static_cast<float>(pitchRange);
            float noteY = timelineStrip.bottom() - 2.0f -
                          (normalizedPitch * (timelineStrip.height - noteHeight - 4.0f)) - noteHeight;
            NUIRect noteRect(startX, noteY, width, noteHeight);
            const bool isPlaying = (playBeat >= 0.0 && playBeat >= span.startBeat && playBeat < span.endBeat);
            renderer.fillRoundedRect(noteRect, 2.0f, isPlaying ? playheadAccent : noteFill);
            renderer.strokeRoundedRect(noteRect, 2.0f, 1.0f, isPlaying ? playheadAccent : noteStroke);
        }
    } else if (m_type == Aestra::Audio::UnitType::Audio) {
        if (!m_audioPreviewWaveform.empty()) {
            const float midY = timelineStrip.y + timelineStrip.height * 0.5f;
            const float ampScale = std::max(4.0f, timelineStrip.height * 0.42f);
            const float binWidth = timelineStrip.width / static_cast<float>(m_audioPreviewWaveform.size());
            const NUIColor waveColor = theme.getColor("waveformLine").withAlpha(0.9f);

            for (size_t i = 0; i < m_audioPreviewWaveform.size(); ++i) {
                const float x = timelineStrip.x + static_cast<float>(i) * binWidth;
                const float amp = std::clamp(m_audioPreviewWaveform[i], 0.02f, 1.0f) * ampScale;
                renderer.drawLine(NUIPoint(x, midY - amp), NUIPoint(x, midY + amp), std::max(1.0f, binWidth * 0.6f),
                                  waveColor);
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
        renderer.drawTextCentered("+", NUIRect(timelineStrip.x, timelineStrip.y + 2.0f, timelineStrip.width, 10.0f),
                                  12.0f, theme.getColor("textSecondary").withAlpha(0.72f));
        renderer.drawTextCentered(
            emptyLabel,
            NUIRect(timelineStrip.x, timelineStrip.y + 10.0f, timelineStrip.width, timelineStrip.height - 10.0f), 9.0f,
            theme.getColor("textSecondary").withAlpha(0.72f));
    }
}

double UnitRow::playheadBeatInPattern() const {
    if (!m_trackManager || !m_trackManager->isPatternMode()) {
        return -1.0;
    }
    double lengthBeats = static_cast<double>(m_stepCount) * 0.25;
    if (m_patternId.isValid()) {
        if (const auto* pattern = m_trackManager->getPatternManager().getPattern(m_patternId)) {
            lengthBeats = std::max(lengthBeats, pattern->lengthBeats);
        }
    }
    if (lengthBeats <= 0.0) {
        return -1.0;
    }
    const double bpm = m_trackManager->getTimelineClock().getCurrentTempo();
    double beat = m_trackManager->getPosition() * (bpm / 60.0);
    beat = std::fmod(beat, lengthBeats);
    if (beat < 0.0)
        beat += lengthBeats;
    return beat;
}

bool UnitRow::shouldUseNoteRoll() const {
    if (usesPianoRollForType(m_type)) {
        return true;
    }
    if (m_type != Aestra::Audio::UnitType::Sampler || !m_trackManager || !m_patternId.isValid()) {
        return false;
    }

    const auto* pattern = m_trackManager->getPatternManager().getPattern(m_patternId);
    if (!pattern || !pattern->isMidi()) {
        return false;
    }

    const auto& midi = std::get<Aestra::Audio::MidiPayload>(pattern->payload);
    return std::any_of(midi.notes.begin(), midi.notes.end(), [this](const Aestra::Audio::MidiNote& note) {
        const bool belongsToUnit = note.unitId == m_unitId || note.unitId == 0;
        return belongsToUnit && (note.pitch != m_rootMidiNote || std::abs(note.durationBeats - 0.25) > 0.01);
    });
}

bool UnitRow::onMouseEvent(const NUIMouseEvent& event) {
    // Pointer events outside the panel's list viewport belong to whatever is
    // rendered there (rows scrolled out of view are not clickable). Ongoing
    // drags/step edits keep receiving events so gestures can finish.
    if (m_viewport.width > 0.0f && m_viewport.height > 0.0f && !m_isDragging && m_velEditStep < 0 &&
        !m_viewport.contains(event.position)) {
        if (m_isHovered || m_hoveredStep != -1) {
            m_isHovered = false;
            m_hoveredStep = -1;
            invalidateVisuals();
        }
        return false;
    }

    // Forward to UnitNameLabel first (same local-coordinate transform as old m_nameInput)
    if (m_nameLabel) {
        auto bounds = getBounds();
        NUIMouseEvent localEvent = event;
        localEvent.position.x -= bounds.x;
        localEvent.position.y -= bounds.y;
        if (m_nameLabel->onMouseEvent(localEvent)) {
            return true;
        }
    }

    auto bounds = getBounds();
    const NUIRect localDragRect(6.0f, 6.0f, 32.0f, 44.0f);
    m_controlWidth = std::clamp(bounds.width * 0.38f, controlFloorForDensity(bounds.width), 312.0f);
    const NUIRect localControlRect(42.0f, 0.0f, std::max(0.0f, m_controlWidth - 48.0f), 56.0f);
    const float separatorX = m_controlWidth;
    const NUIRect localContextRect(separatorX + 8.0f, 6.0f, std::max(0.0f, bounds.width - (separatorX + 2.0f)), 44.0f);
    // Step math must use the same geometry the pads are DRAWN with:
    // drawContextBlock insets the context rect by 6px lane padding before
    // laying out steps. Resolving against the un-inset rect skewed the step
    // width and offset, so clicks landed on neighboring pads (worse toward
    // the right edge of long loops).
    const NUIRect localGridRect(localContextRect.x + 6.0f, localContextRect.y + 6.0f,
                                std::max(0.0f, localContextRect.width - 12.0f),
                                std::max(0.0f, localContextRect.height - 12.0f));
    const NUIPoint localPoint(event.position.x - bounds.x, event.position.y - bounds.y);

    auto resolveGridStep = [this](const NUIPoint& position, const NUIRect& gridBounds) -> int {
        float relativeX = position.x - gridBounds.x;
        float availWidth = gridBounds.width;
        float stepWidth = gridStepWidth(availWidth);
        if (stepWidth <= 0.0f || relativeX < 0.0f || relativeX >= availWidth) {
            return -1;
        }
        // Exact pad containment: 4-step groups carry a structural gap, and the
        // 3px lane gap between pads is not clickable — only the drawn cell
        // itself registers (bounded, 32 steps max -> trivial loop).
        constexpr float kCellGap = 3.0f;
        const float contentX = relativeX + m_scrollX;
        for (int s = 0; s < m_stepCount; ++s) {
            const float slotLeft = s * stepWidth + static_cast<float>(s / 4) * kStepGroupGap;
            const float cellLeft = slotLeft + kCellGap * 0.5f;
            const float cellWidth = std::min(stepWidth, std::max(2.0f, stepWidth - kCellGap));
            if (contentX >= cellLeft && contentX < cellLeft + cellWidth) {
                return s;
            }
        }
        return -1;
    };

    // === Scroll Handling ===
    if (std::abs(event.wheelDelta) > 0.0f) {
        // Only scroll if hovering over context area (right of controls)
        if (bounds.contains(event.position) && (event.position.x - bounds.x > m_controlWidth)) {
            float delta = event.wheelDelta;
            if (delta > 10.0f)
                delta = 1.0f;
            if (delta < -10.0f)
                delta = -1.0f;

            // Step-grid units (Sampler / 808) scroll the grid horizontally so the
            // whole loop is reachable when it's longer than the row is wide. Only
            // the piano-roll / note-roll display scrolls the pitch viewport.
            if (shouldUseNoteRoll()) {
                // Pitch viewport scroll (up/down = see higher/lower pitches)
                m_minimapPitchOffset -= delta * 4.0f; // 4 semitones per tick
                m_minimapPitchOffset = std::clamp(m_minimapPitchOffset, -60.0f, 60.0f);
                invalidateVisuals();
                return true;
            }
            if (m_fitToWidth) {
                // Whole loop is on screen — nothing to scroll horizontally, so
                // let the wheel bubble up to the panel's vertical list scroll.
                return false;
            }
            if (m_onGridScroll) {
                // Panel owns the shared offset: it clamps against the loop
                // width and pushes the result to every row + the header.
                m_onGridScroll(-delta * 40.0f);
            } else {
                // Horizontal scroll for step grid (clamped against content in draw)
                m_scrollX = std::max(0.0f, m_scrollX - delta * 40.0f);
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
        const int stepIndex = resolveGridStep(localPoint, localGridRect);
        if (stepIndex >= 0 && stepIndex < m_stepCount) {
            m_hoveredStep = stepIndex;
        }
    }

    if (wasHovered != m_isHovered || oldHoveredStep != m_hoveredStep) {
        invalidateVisuals();
    }

    // === Step-grid gesture session ===
    if (m_velEditStep >= 0) {
        if (event.released || event.type == NUIMouseEventType::Up) {
            // A stationary click selects only when it lands on an already-
            // placed note — placing a new note (press on an empty pad) must
            // never hijack the selection. Paint is placement-only; velocity
            // drags on pre-existing notes select them like a click would.
            if (m_stepGestureMode == StepGestureMode::Pending && m_stepGestureWasActive) {
                applyClickSelection(m_velEditStep, m_stepGestureShiftHeld);
            } else if (m_stepGestureMode == StepGestureMode::Velocity && m_stepGestureWasActive) {
                applyClickSelection(m_velEditStep, false);
            } else if (m_stepGestureMode == StepGestureMode::Erase) {
                // Right-drag erase: drop erased steps from the selection.
                m_selectedSteps.erase(
                    std::remove_if(m_selectedSteps.begin(), m_selectedSteps.end(), [this](int s) {
                        float v = kDefaultStepVelocity;
                        return !stepHasNote(s, v);
                    }),
                    m_selectedSteps.end());
                notifyStepSelectionChanged();
            }
            if (m_stepGestureChanged && m_onPatternEdited && m_patternId.isValid()) {
                m_onPatternEdited(m_patternId);
            }
            m_velEditStep = -1;
            m_stepGestureLastStep = -1;
            m_stepGestureMode = StepGestureMode::None;
            m_stepGestureChanged = false;
            invalidateVisuals();
            return true;
        }

        const bool isPointerMove = !event.pressed && !event.released &&
                                   (event.type == NUIMouseEventType::Drag || event.type == NUIMouseEventType::Move ||
                                    event.button == NUIMouseButton::None);
        if (isPointerMove) {
            const float dx = event.position.x - m_stepGestureStartX;
            const float dy = m_velEditStartY - event.position.y; // up = louder
            if (m_stepGestureMode == StepGestureMode::Pending && std::max(std::abs(dx), std::abs(dy)) > 3.0f) {
                if (std::abs(dx) >= std::abs(dy)) {
                    // Horizontal drag paints from any starting pad. Erasing is
                    // now Delete/Backspace (selection-based editing), so a
                    // drag from an active note extends the phrase instead of
                    // destroying it.
                    m_stepGestureMode = StepGestureMode::Paint;
                } else {
                    m_stepGestureMode = StepGestureMode::Velocity;
                }
            }

            if (m_stepGestureMode == StepGestureMode::Paint || m_stepGestureMode == StepGestureMode::Erase) {
                const int currentStep = resolveGridStep(localPoint, localGridRect);
                if (currentStep >= 0 && currentStep < m_stepCount && currentStep != m_stepGestureLastStep) {
                    const int first = std::min(m_stepGestureLastStep, currentStep);
                    const int last = std::max(m_stepGestureLastStep, currentStep);
                    for (int step = first; step <= last; ++step) {
                        m_stepGestureChanged |=
                            m_stepGestureMode == StepGestureMode::Paint ? placeStepNote(step) : removeStepNote(step);
                    }
                    m_stepGestureLastStep = currentStep;
                    invalidateVisuals();
                }
            } else if (m_stepGestureMode == StepGestureMode::Velocity) {
                // Full row height ≈ full velocity range.
                const float vel = m_velEditBaseVelocity + dy / 90.0f;
                setStepNoteVelocity(m_velEditStep, vel);
                m_stepGestureChanged = true;
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
                    if (m_onDragStart)
                        m_onDragStart(m_unitId);
                    invalidateVisuals();
                    return true;
                } else if (localControlRect.contains(localPoint)) {
                    handleControlClick(event, localControlRect);
                    return true;
                } else if (localContextRect.contains(localPoint)) {
                    // Step grids support one continuous gesture on every pad:
                    // vertical movement edits velocity, horizontal movement
                    // paints, and a stationary click creates/selects. The
                    // pattern (not a loaded sample) is what makes pads real —
                    // notes-only units must edit identically to loaded ones.
                    if (usesStepSequencerForType(m_type) && !shouldUseNoteRoll() && m_patternId.isValid() &&
                        m_trackManager) {
                        const int step = resolveGridStep(localPoint, localGridRect);
                        if (step >= 0 && step < m_stepCount) {
                            // Double-click on a unit without a sample opens the
                            // file picker (fast onboarding). Units with a
                            // sample treat rapid taps as step programming.
                            if (m_audioClip.empty() && m_pluginId.empty()) {
                                auto now = std::chrono::steady_clock::now();
                                long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                      now.time_since_epoch())
                                                      .count();
                                const bool isDoubleClick =
                                    (nowMs - m_lastClipClickTimeMs < 400) || event.doubleClick;
                                m_lastClipClickTimeMs = nowMs;
                                if (isDoubleClick) {
                                    if (m_onLoadUnitSample) {
                                        m_onLoadUnitSample(m_unitId);
                                    }
                                    return true;
                                }
                            }
                            float vel = kDefaultStepVelocity;
                            const bool active = stepHasNote(step, vel);
                            m_velEditStep = step;
                            m_stepGestureLastStep = step;
                            m_stepGestureStartX = event.position.x;
                            m_velEditStartY = event.position.y;
                            m_stepGestureMode = StepGestureMode::Pending;
                            m_stepGestureChanged = false;
                            m_stepGestureShiftHeld = (event.modifiers & NUIModifiers::Shift) != 0;
                            m_stepGestureWasActive = active;
                            if (!active) {
                                m_stepGestureChanged = placeStepNote(step);
                                vel = kDefaultStepVelocity;
                            }
                            m_velEditBaseVelocity = vel;
                            invalidateVisuals();
                            return true;
                        }
                    }
                    handleContextClick(event, localGridRect);
                    return true;
                }
            }
        }

        // === Right-click (row body, outside name label which is handled by UnitNameLabel child) ===
        if (event.button == NUIMouseButton::Right) {
            if (bounds.contains(event.position)) {
                if (localControlRect.contains(localPoint)) {
                    const auto pills = controlPillRects(localControlRect);
                    if (pills[0].contains(localPoint)) {
                        showMixerRoutingMenu(event.position);
                        return true;
                    }
                }

                // Right-drag across a step grid erases every crossed pad.
                // Right-click raises the row context menu only from the name
                // label (far left) — the grid never opens menus.
                if (localContextRect.contains(localPoint) && usesStepSequencerForType(m_type) &&
                    !shouldUseNoteRoll() && m_patternId.isValid() && m_trackManager) {
                    const int stepIndex = resolveGridStep(localPoint, localGridRect);
                    if (stepIndex >= 0 && stepIndex < m_stepCount) {
                        float velocity = kDefaultStepVelocity;
                        m_velEditStep = stepIndex;
                        m_stepGestureLastStep = stepIndex;
                        m_stepGestureStartX = event.position.x;
                        m_velEditStartY = event.position.y;
                        m_velEditBaseVelocity = velocity;
                        m_stepGestureMode = StepGestureMode::Erase;
                        m_stepGestureChanged = removeStepNote(stepIndex);
                        invalidateVisuals();
                        return true;
                    }
                }
                // Row context menu lives on the far-left text (name) area only.
                if (m_nameLabel && m_nameLabel->getBounds().contains(localPoint)) {
                    showRowContextMenu(event.position);
                }
                return true;
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

    const auto pills = controlPillRects(bounds);
    const NUIRect& muteRect = pills[1];
    const NUIRect& soloRect = pills[2];
    const NUIRect& routeRect = pills[0];
    const NUIPoint rowLocalPoint(rowLocalX, rowLocalY);
    if (routeRect.contains(rowLocalPoint)) {
        showMixerRoutingMenu(event.position);
        return;
    }
    if (muteRect.contains(rowLocalPoint)) {
        m_manager.setUnitMute(m_unitId, !m_isMuted);
        updateState();
        invalidateVisuals();
        return;
    }
    if (soloRect.contains(rowLocalPoint)) {
        m_manager.setUnitSolo(m_unitId, !m_isSolo);
        updateState();
        invalidateVisuals();
        return;
    }
}

void UnitRow::handleContextClick(const NUIMouseEvent& event, const NUIRect& bounds) {
    const auto rowBounds = getBounds();
    const NUIPoint localPoint(event.position.x - rowBounds.x - bounds.x, event.position.y - rowBounds.y - bounds.y);

    if (shouldUseNoteRoll()) {
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

    // === Step-grid rows: clicks that miss a pad do nothing ===
    // Selection-based editing only creates/selects on a real pad hit; lane
    // padding and cell gaps are dead space now (they used to toggle the
    // nearest pad, which made left-clicks erase active notes). Double-click
    // on a unit without a sample still opens the file picker.
    if (usesStepSequencerForType(m_type)) {
        auto now = std::chrono::steady_clock::now();
        long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        const bool isDoubleClick = (nowMs - m_lastClipClickTimeMs < 400) || event.doubleClick;
        m_lastClipClickTimeMs = nowMs;
        if (isDoubleClick && m_audioClip.empty() && m_pluginId.empty()) {
            if (m_onLoadUnitSample) {
                m_onLoadUnitSample(m_unitId);
            }
        }
        return;
    }
}

bool UnitRow::isStepSelected(int step) const {
    return std::binary_search(m_selectedSteps.begin(), m_selectedSteps.end(), step);
}

void UnitRow::applyClickSelection(int step, bool additive) {
    if (step < 0 || step >= m_stepCount) {
        return;
    }
    if (additive) {
        if (!isStepSelected(step)) {
            m_selectedSteps.push_back(step);
            std::sort(m_selectedSteps.begin(), m_selectedSteps.end());
            notifyStepSelectionChanged();
            invalidateVisuals();
        }
    } else if (m_selectedSteps.size() != 1 || m_selectedSteps[0] != step) {
        m_selectedSteps = {step};
        notifyStepSelectionChanged();
        invalidateVisuals();
    }
}

void UnitRow::applyRangeSelection(int firstStep, int lastStep) {
    firstStep = std::max(0, firstStep);
    lastStep = std::min(m_stepCount - 1, lastStep);
    if (lastStep < firstStep) {
        return;
    }
    m_selectedSteps.clear();
    for (int s = firstStep; s <= lastStep; ++s) {
        m_selectedSteps.push_back(s);
    }
    notifyStepSelectionChanged();
    invalidateVisuals();
}

void UnitRow::notifyStepSelectionChanged() {
    if (m_onStepSelectionChanged) {
        m_onStepSelectionChanged(m_unitId, m_selectedSteps);
    }
}

void UnitRow::duplicateSelection() {
    if (m_selectedSteps.empty() || !m_patternId.isValid() || !m_trackManager) {
        return;
    }
    const int minStep = *std::min_element(m_selectedSteps.begin(), m_selectedSteps.end());
    const int maxStep = *std::max_element(m_selectedSteps.begin(), m_selectedSteps.end());
    const int span = maxStep - minStep + 1; // occupied span → duplicate starts right after it

    std::vector<int> newSelection;
    bool changed = false;
    m_trackManager->getPatternManager().applyPatch(m_patternId,
        [this, span, &newSelection, &changed](Aestra::Audio::PatternSource& p) {
            if (!p.isMidi()) {
                return;
            }
            auto& midi = std::get<Aestra::Audio::MidiPayload>(p.payload);
            const auto covers = [&midi](Aestra::Audio::UnitID unitId, double beat) {
                for (const auto& n : midi.notes) {
                    const double endBeat = std::max(n.startBeat + 0.25, n.startBeat + n.durationBeats);
                    if (n.unitId == unitId && beat >= n.startBeat - 0.01 && beat < endBeat - 0.01) {
                        return true;
                    }
                }
                return false;
            };
            for (const int step : m_selectedSteps) {
                const int dstStep = step + span;
                if (dstStep >= m_stepCount) {
                    continue; // clipped at the pattern end — never grows the loop silently
                }
                const double srcBeat = step * 0.25;
                const double dstBeat = dstStep * 0.25;
                const Aestra::Audio::MidiNote* src = nullptr;
                for (const auto& n : midi.notes) {
                    const double endBeat = std::max(n.startBeat + 0.25, n.startBeat + n.durationBeats);
                    if (n.unitId == m_unitId && srcBeat >= n.startBeat - 0.01 && srcBeat < endBeat - 0.01) {
                        src = &n;
                        break;
                    }
                }
                if (!src || covers(m_unitId, dstBeat)) {
                    continue; // no source note, or destination already occupied
                }
                Aestra::Audio::MidiNote copy = *src; // pitch/duration/velocity preserved
                copy.startBeat = dstBeat;
                midi.notes.push_back(copy);
                newSelection.push_back(dstStep);
                changed = true;
            }
        });
    if (!changed) {
        return;
    }
    m_selectedSteps = std::move(newSelection); // sorted: constant offset over a sorted selection
    notifyStepSelectionChanged();
    invalidateVisuals();
    if (m_onPatternEdited) {
        m_onPatternEdited(m_patternId);
    }
}

void UnitRow::deleteSelection() {
    if (m_selectedSteps.empty() || !m_patternId.isValid() || !m_trackManager) {
        return;
    }
    bool changed = false;
    m_trackManager->getPatternManager().applyPatch(m_patternId,
        [this, &changed](Aestra::Audio::PatternSource& p) {
            if (!p.isMidi()) {
                return;
            }
            auto& midi = std::get<Aestra::Audio::MidiPayload>(p.payload);
            const auto stepCovered = [this](const Aestra::Audio::MidiNote& n) {
                if (n.unitId != m_unitId) {
                    return false;
                }
                const double endBeat = std::max(n.startBeat + 0.25, n.startBeat + n.durationBeats);
                for (const int s : m_selectedSteps) {
                    const double beat = s * 0.25;
                    if (beat >= n.startBeat - 0.01 && beat < endBeat - 0.01) {
                        return true;
                    }
                }
                return false;
            };
            const size_t before = midi.notes.size();
            midi.notes.erase(std::remove_if(midi.notes.begin(), midi.notes.end(), stepCovered), midi.notes.end());
            changed = midi.notes.size() != before;
        });
    m_selectedSteps.clear();
    notifyStepSelectionChanged();
    invalidateVisuals();
    if (changed && m_onPatternEdited) {
        m_onPatternEdited(m_patternId);
    }
}

void UnitRow::moveSelection(int stepDelta) {
    if (m_selectedSteps.empty() || !m_patternId.isValid() || !m_trackManager) {
        return;
    }
    const int dir = stepDelta > 0 ? 1 : -1;
    bool changed = false;
    std::vector<int> newSelection;
    m_trackManager->getPatternManager().applyPatch(m_patternId,
        [this, dir, &newSelection, &changed](Aestra::Audio::PatternSource& p) {
            if (!p.isMidi()) {
                return;
            }
            auto& midi = std::get<Aestra::Audio::MidiPayload>(p.payload);
            // Process in movement direction so vacated slots are reusable and
            // blocked cascades resolve (right: high→low; left: low→high).
            std::vector<int> ordered = m_selectedSteps;
            if (dir > 0) {
                std::sort(ordered.rbegin(), ordered.rend());
            } else {
                std::sort(ordered.begin(), ordered.end());
            }

            const auto occupiedBy = [&midi](Aestra::Audio::UnitID uid, double beat) {
                for (const auto& n : midi.notes) {
                    const double endBeat = std::max(n.startBeat + 0.25, n.startBeat + n.durationBeats);
                    if (n.unitId == uid && beat >= n.startBeat - 0.01 && beat < endBeat - 0.01) {
                        return true;
                    }
                }
                return false;
            };
            const auto findAt = [&midi](Aestra::Audio::UnitID uid, double beat) {
                for (auto it = midi.notes.begin(); it != midi.notes.end(); ++it) {
                    const double endBeat = std::max(it->startBeat + 0.25, it->startBeat + it->durationBeats);
                    if (it->unitId == uid && beat >= it->startBeat - 0.01 && beat < endBeat - 0.01) {
                        return it;
                    }
                }
                return midi.notes.end();
            };

            for (const int step : ordered) {
                const int dst = step + dir;
                const double srcBeat = step * 0.25;
                const double dstBeat = dst * 0.25;
                if (dst < 0 || dst >= m_stepCount || occupiedBy(m_unitId, dstBeat)) {
                    newSelection.push_back(step); // stuck: note (or empty slot) stays put
                    continue;
                }
                auto it = findAt(m_unitId, srcBeat);
                if (it == midi.notes.end()) {
                    newSelection.push_back(dst); // empty selected slot moves freely
                    continue;
                }
                Aestra::Audio::MidiNote copy = *it; // pitch/duration/velocity preserved
                copy.startBeat = dstBeat;
                midi.notes.erase(it);
                midi.notes.push_back(copy);
                newSelection.push_back(dst);
                changed = true;
            }
        });
    std::sort(newSelection.begin(), newSelection.end());
    const bool selectionChanged = newSelection != m_selectedSteps;
    m_selectedSteps = std::move(newSelection);
    if (selectionChanged) {
        notifyStepSelectionChanged();
    }
    invalidateVisuals();
    if (changed && m_onPatternEdited) {
        m_onPatternEdited(m_patternId);
    }
}

void UnitRow::nudgeSelectionVelocity(float delta) {
    if (m_selectedSteps.empty() || !m_patternId.isValid() || !m_trackManager) {
        return;
    }
    bool changed = false;
    m_trackManager->getPatternManager().applyPatch(m_patternId,
        [this, delta, &changed](Aestra::Audio::PatternSource& p) {
            if (!p.isMidi()) {
                return;
            }
            auto& midi = std::get<Aestra::Audio::MidiPayload>(p.payload);
            for (const int step : m_selectedSteps) {
                const double beat = step * 0.25;
                for (auto& n : midi.notes) {
                    const double endBeat = std::max(n.startBeat + 0.25, n.startBeat + n.durationBeats);
                    if (n.unitId == m_unitId && beat >= n.startBeat - 0.01 && beat < endBeat - 0.01) {
                        const float v = std::clamp(n.velocity + delta, kMinStepVelocity, 1.0f);
                        if (v != n.velocity) {
                            n.velocity = v;
                            changed = true;
                        }
                    }
                }
            }
        });
    invalidateVisuals();
    if (changed && m_onPatternEdited) {
        m_onPatternEdited(m_patternId);
    }
}

void UnitRow::selectAllNotes() {
    if (!m_patternId.isValid() || !m_trackManager) {
        return;
    }
    std::vector<int> steps;
    for (int s = 0; s < m_stepCount; ++s) {
        float v = kDefaultStepVelocity;
        if (stepHasNote(s, v)) {
            steps.push_back(s);
        }
    }
    if (steps == m_selectedSteps) {
        return;
    }
    m_selectedSteps = std::move(steps);
    notifyStepSelectionChanged();
    invalidateVisuals();
}

bool UnitRow::stepHasNote(int step, float& velocityOut) const {
    velocityOut = kDefaultStepVelocity;
    if (!m_patternId.isValid() || !m_trackManager)
        return false;
    const auto* pattern = m_trackManager->getPatternManager().getPattern(m_patternId);
    if (!pattern || !pattern->isMidi())
        return false;
    const auto& midi = std::get<Aestra::Audio::MidiPayload>(pattern->payload);
    const double targetBeat = step * 0.25;
    for (const auto& n : midi.notes) {
        const double endBeat = std::max(n.startBeat + 0.25, n.startBeat + n.durationBeats);
        if (n.unitId == m_unitId && targetBeat >= n.startBeat - 0.01 && targetBeat < endBeat - 0.01) {
            velocityOut = n.velocity;
            return true;
        }
    }
    return false;
}

bool UnitRow::placeStepNote(int step) {
    if (!m_patternId.isValid() || !m_trackManager)
        return false;
    bool placed = false;
    m_trackManager->getPatternManager().applyPatch(m_patternId, [this, step, &placed](Aestra::Audio::PatternSource& p) {
        if (!p.isMidi())
            return;
        auto& midi = std::get<Aestra::Audio::MidiPayload>(p.payload);
        const double targetBeat = step * 0.25;
        for (const auto& n : midi.notes) {
            const double endBeat = std::max(n.startBeat + 0.25, n.startBeat + n.durationBeats);
            if (n.unitId == m_unitId && targetBeat >= n.startBeat - 0.01 && targetBeat < endBeat - 0.01) {
                return; // already present
            }
        }
        // NB: explicit-field init — positional init would drop m_unitId into
        // MidiNote::pan and leave unitId=0 (dropped by playback). Cf. #447.
        Aestra::Audio::MidiNote note;
        note.pitch = m_rootMidiNote; // root → sample plays untransposed
        note.startBeat = targetBeat;
        note.durationBeats = 0.25;
        note.velocity = kDefaultStepVelocity;
        note.unitId = m_unitId;
        midi.notes.push_back(note);
        placed = true;
    });
    return placed;
}

bool UnitRow::removeStepNote(int step) {
    if (!m_patternId.isValid() || !m_trackManager)
        return false;
    bool removed = false;
    m_trackManager->getPatternManager().applyPatch(m_patternId, [this, step,
                                                                 &removed](Aestra::Audio::PatternSource& p) {
        if (!p.isMidi())
            return;
        auto& midi = std::get<Aestra::Audio::MidiPayload>(p.payload);
        const double targetBeat = step * 0.25;
        auto it =
            std::find_if(midi.notes.begin(), midi.notes.end(), [this, targetBeat](const Aestra::Audio::MidiNote& n) {
                const double endBeat = std::max(n.startBeat + 0.25, n.startBeat + n.durationBeats);
                return n.unitId == m_unitId && targetBeat >= n.startBeat - 0.01 && targetBeat < endBeat - 0.01;
            });
        if (it != midi.notes.end()) {
            midi.notes.erase(it);
            removed = true;
        }
    });
    return removed;
}

void UnitRow::setStepNoteVelocity(int step, float velocity) {
    if (!m_patternId.isValid() || !m_trackManager)
        return;
    const float v = std::clamp(velocity, kMinStepVelocity, 1.0f);
    m_trackManager->getPatternManager().applyPatch(m_patternId, [this, step, v](Aestra::Audio::PatternSource& p) {
        if (!p.isMidi())
            return;
        auto& midi = std::get<Aestra::Audio::MidiPayload>(p.payload);
        const double targetBeat = step * 0.25;
        for (auto& n : midi.notes) {
            const double endBeat = std::max(n.startBeat + 0.25, n.startBeat + n.durationBeats);
            if (n.unitId == m_unitId && targetBeat >= n.startBeat - 0.01 && targetBeat < endBeat - 0.01) {
                n.velocity = v;
                return;
            }
        }
    });
}

bool UnitRow::onKeyEvent(const NUIKeyEvent& event) {
    // Key events are forwarded to children automatically by NUIComponent.
    // The UnitNameLabel (when renaming) receives them via its NUITextInput child.
    if (!event.pressed) {
        return false;
    }
    if (m_nameLabel && m_nameLabel->isRenaming()) {
        return false; // typing a name — Delete/Backspace belong to the text field
    }

    const bool isStepGrid = usesStepSequencerForType(m_type) && !shouldUseNoteRoll() &&
                            m_patternId.isValid() && m_trackManager;
    if (!isStepGrid) {
        return false;
    }

    const bool ctrl = (event.modifiers & NUIModifiers::Ctrl) != 0;
    if (ctrl && event.keyCode == NUIKeyCode::B) {
        if (!event.repeat) {
            duplicateSelection();
        }
        return true;
    }
    if (!ctrl && (event.keyCode == NUIKeyCode::Delete || event.keyCode == NUIKeyCode::Backspace)) {
        deleteSelection();
        return true;
    }
    if (!ctrl && (event.keyCode == NUIKeyCode::Left || event.keyCode == NUIKeyCode::Right)) {
        // Move mirrors the horizontal drag axis: selected notes shift one step.
        moveSelection(event.keyCode == NUIKeyCode::Right ? 1 : -1);
        return true;
    }
    if (!ctrl && (event.keyCode == NUIKeyCode::Up || event.keyCode == NUIKeyCode::Down)) {
        // Velocity mirrors the vertical drag axis: Up louder, Down quieter.
        nudgeSelectionVelocity(event.keyCode == NUIKeyCode::Up ? kVelocityNudgeStep : -kVelocityNudgeStep);
        return true;
    }
    if (ctrl && event.keyCode == NUIKeyCode::A) {
        if (!event.repeat) {
            selectAllNotes(); // Ctrl+A: select the active notes (empty slots stay unselected)
        }
        return true;
    }
    if (!ctrl && event.keyCode == NUIKeyCode::Escape) {
        // Esc dismisses the selection — intuitive way out of the editing grammar.
        if (!m_selectedSteps.empty()) {
            m_selectedSteps.clear();
            notifyStepSelectionChanged();
            invalidateVisuals();
        }
        return true;
    }
    return false;
}

//==============================================================================
// IDropTarget Implementation
//==============================================================================

UnitRow::~UnitRow() {
    detachContextMenu(m_rowContextMenu);
    detachContextMenu(m_mixerRoutingMenu);
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
    // A row scrolled out of the list viewport is invisible — it must not
    // catch drops either, so trim the drop area to what the user can see.
    NUIRect bounds = getBounds();
    if (m_viewport.width > 0.0f && m_viewport.height > 0.0f) {
        const float top = std::max(bounds.y, m_viewport.y);
        const float bottom = std::min(bounds.bottom(), m_viewport.bottom());
        bounds.y = top;
        bounds.height = std::max(0.0f, bottom - top);
    }
    return bounds;
}

void UnitRow::onResize(int width, int height) {
    (void)width;
    (void)height;
    layoutNameLabel();
}

void UnitRow::layoutNameLabel() {
    if (!m_nameLabel)
        return;
    auto bounds = getBounds();
    // Density tiers from available width: Full >= 620, Compact >= 440,
    // Minimal below. The control block floor follows the tier so narrow rows
    // give the step grid the width back instead of clipping it.
    m_density = densityForWidth(bounds.width);
    float controlWidth = std::clamp(bounds.width * 0.38f, controlFloorForDensity(bounds.width), 312.0f);
    float labelX = 54.0f; // 42 (control block) + 12 (name indent)
    // The label ends before the pill band; the band width follows the tier.
    const float pillBand = (m_density == Density::Full)     ? 142.0f
                           : (m_density == Density::Compact) ? 110.0f
                                                             : 74.0f;
    float labelWidth = std::max(16.0f, (controlWidth - 48.0f) - pillBand - 6.0f);
    m_nameLabel->setBounds(NUIRect(labelX, 8.0f, labelWidth, 30.0f));
    m_nameLabel->setCompact(m_density != Density::Full);
}

UnitRow::Density UnitRow::densityForWidth(float width) {
    return (width >= 620.0f)   ? Density::Full
           : (width >= 440.0f) ? Density::Compact
                               : Density::Minimal;
}

float UnitRow::controlFloorForDensity(float width) {
    const Density d = densityForWidth(width);
    return (d == Density::Minimal)   ? 150.0f
           : (d == Density::Compact) ? 180.0f
                                     : 220.0f;
}

void UnitRow::routeToMixerChannel(uint32_t channelId) {
    if (!m_trackManager || !m_manager.getUnit(m_unitId)) {
        return;
    }
    m_trackManager->getCommandHistory().pushAndExecute(
        std::make_shared<Aestra::Audio::SetUnitMixerChannelCommand>(*m_trackManager, m_unitId, channelId));
    updateState();
}

bool UnitRow::routeToFirstFreeMixerChannel() {
    if (!m_trackManager || !m_manager.getUnit(m_unitId)) {
        return false;
    }

    const std::string destinationName = m_name.empty() ? "Mixer Insert" : m_name;
    const bool routed = Aestra::Audio::assignUnitToFirstFreeInsert(*m_trackManager, m_unitId, destinationName, m_color);
    updateState();
    return routed;
}

void UnitRow::showMixerRoutingMenu(const NUIPoint& pos) {
    detachContextMenu(m_mixerRoutingMenu);
    if (!m_trackManager) {
        return;
    }

    m_mixerRoutingMenu = std::make_shared<NUIContextMenu>();
    m_mixerRoutingMenu->setOnHide([this]() { detachContextMenu(m_mixerRoutingMenu); });

    const auto addRouteItem = [this](uint32_t channelId, const std::string& label) {
        const bool selected = channelId == m_mixerChannelId;
        m_mixerRoutingMenu->addItem((selected ? "✓ " : "  ") + label,
                                    [this, channelId]() { routeToMixerChannel(channelId); });
    };

    addRouteItem(Aestra::Audio::MASTER_MIXER_CHANNEL_ID, "Master");
    if (m_trackManager->getChannelCount() > 0) {
        m_mixerRoutingMenu->addSeparator();
    }
    for (size_t i = 0; i < m_trackManager->getChannelCount(); ++i) {
        const auto* channel = m_trackManager->getChannel(i);
        if (!channel)
            continue;
        addRouteItem(channel->getChannelId(), std::to_string(i + 1) + "  " + channel->getName());
    }

    m_mixerRoutingMenu->addSeparator();
    m_mixerRoutingMenu->addItem("Assign to first free insert", [this]() { routeToFirstFreeMixerChannel(); });
    attachAndShowContextMenu(this, m_mixerRoutingMenu, pos);
}

void UnitRow::showRowContextMenu(const NUIPoint& pos) {
    detachContextMenu(m_rowContextMenu);

    bool hasPattern = false;
    Aestra::Audio::UnitType type = Aestra::Audio::UnitType::Sampler;
    if (auto* unit = m_manager.getUnit(m_unitId)) {
        hasPattern = unit->defaultPatternId.isValid();
        type = unit->type;
    }

    m_rowContextMenu = std::make_shared<NUIContextMenu>();
    m_rowContextMenu->setOnHide([this]() { detachContextMenu(m_rowContextMenu); });

    if (usesPianoRollForType(type) && hasPattern) {
        m_rowContextMenu->addItem("Open in Piano Roll", [this]() {
            // Verify unit still exists (defensive check against use-after-free)
            if (!m_manager.getUnit(m_unitId))
                return;
            if (m_onOpenPatternEditor && m_patternId.isValid()) {
                m_onOpenPatternEditor(m_patternId);
            }
        });
    }
    if (usesStepSequencerForType(type)) {
        bool hasSample = false;
        if (auto* unit = m_manager.getUnit(m_unitId))
            hasSample = !unit->audioClipPath.empty();

        if (hasSample) {
            m_rowContextMenu->addItem("Replace Sample", [this]() {
                if (!m_manager.getUnit(m_unitId))
                    return;
                if (m_onLoadUnitSample)
                    m_onLoadUnitSample(m_unitId);
            });
        } else {
            m_rowContextMenu->addItem("Load Sample", [this]() {
                if (!m_manager.getUnit(m_unitId))
                    return;
                if (m_onLoadUnitSample)
                    m_onLoadUnitSample(m_unitId);
            });
        }
    }
    if (type == Aestra::Audio::UnitType::Audio) {
        m_rowContextMenu->addItem("Load Audio Clip", [this]() {
            if (!m_manager.getUnit(m_unitId))
                return;
            if (m_onLoadUnitSample)
                m_onLoadUnitSample(m_unitId);
        });
    }

    m_rowContextMenu->addSeparator();
    m_rowContextMenu->addItem("Rename", [this]() {
        if (!m_manager.getUnit(m_unitId))
            return;
        if (m_nameLabel)
            m_nameLabel->beginRename();
    });

    m_rowContextMenu->addItem("Duplicate Unit", [this]() {
        if (!m_manager.getUnit(m_unitId))
            return;
        if (m_onDuplicateUnit)
            m_onDuplicateUnit(m_unitId);
    });

    m_rowContextMenu->addSeparator();
    m_rowContextMenu->addItem("Delete Unit", [this, pos]() {
        if (!m_manager.getUnit(m_unitId))
            return;
        showDeleteConfirmation(pos);
    });

    attachAndShowContextMenu(this, m_rowContextMenu, pos);
}

void UnitRow::showDeleteConfirmation(const NUIPoint& pos) {
    auto confirm = std::make_shared<NUIContextMenu>();
    confirm->addItem("Confirm Delete", [this]() {
        // Verify unit still exists (defensive check against use-after-free)
        if (!m_manager.getUnit(m_unitId))
            return;
        if (m_onDeleteUnit)
            m_onDeleteUnit(m_unitId);
    });
    confirm->addItem("Cancel", []() {});
    attachAndShowContextMenu(this, confirm, pos);
}

} // namespace AestraUI
