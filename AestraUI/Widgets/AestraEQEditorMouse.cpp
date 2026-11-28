// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraEQEditor.h"
#include "AestraEQEditorInternal.h"

#include "NUIIcon.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "Plugin/AestraEQ.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>
#include <vector>

namespace AestraUI {

static float normalizedFromLaneX(const NUIRect& lane, float x) {
    return std::clamp((x - lane.x) / std::max(1.0f, lane.width), 0.0f, 1.0f);
}

bool AestraEQEditor::handleFloatingBandPanelClick(const NUIMouseEvent& event) {
    if (!event.pressed || event.button != NUIMouseButton::Left)
        return false;
    if (!m_bandInspectorCollapsed)
        return false;
    const int idx = currentFloatingBandIndex();
    if (idx < 0)
        return false;
    const auto layout = floatingBandPanelLayout(idx, m_graphBounds);
    if (!layout.valid || !layout.panel.contains(event.position))
        return false;

    auto& bd = m_bands[static_cast<size_t>(idx)];
    m_selectedBand = idx;
    if (layout.enableRect.contains(event.position)) {
        const bool nowEnabled = !bd.enabled;
        setBandValue(idx, Knob::Enable, nowEnabled ? 1.0f : 0.0f);
        return true;
    }
    if ((bd.typeId != 0 || !bd.legacySlot) && layout.typeRect.contains(event.position)) {
        m_typeMenuBand = (m_typeMenuBand == idx) ? -1 : idx;
        m_typeMenuFromNodeQuickAction = false;
        m_hoveredTypeOption = -1;
        m_stereoMenuBand = -1;
        m_stereoMenuFromNodeQuickAction = false;
        m_hoveredStereoOption = -1;
        setDirty(true);
        return true;
    }
    if (layout.stereoRect.contains(event.position)) {
        m_stereoMenuBand = (m_stereoMenuBand == idx) ? -1 : idx;
        m_stereoMenuFromNodeQuickAction = false;
        m_hoveredStereoOption = -1;
        m_typeMenuBand = -1;
        m_typeMenuFromNodeQuickAction = false;
        m_hoveredTypeOption = -1;
        setDirty(true);
        return true;
    }
    auto beginPanelLaneEdit = [&](Knob target, const NUIRect& lane) {
        if (event.doubleClick) {
            beginNumericEdit(idx, target);
            return;
        }
        m_draggingCardBand = idx;
        m_draggingKnob = target;
        m_draggingLaneRect = lane;
        m_dragStartY = event.position.y;
        m_dragStartValue = (target == Knob::Freq)        ? bd.freq
                           : (target == Knob::Gain)      ? (bd.usesSlope ? bd.q : bd.gain)
                           : (target == Knob::Q)         ? bd.q
                           : (target == Knob::Threshold) ? bd.dynamicThreshold
                           : (target == Knob::Attack)    ? bd.dynamicAttack
                           : (target == Knob::Release)   ? bd.dynamicRelease
                                                         : bd.q;
        setBandValue(idx, target, normalizedFromLaneX(lane, event.position.x));
    };
    if (layout.freqRect.contains(event.position)) {
        beginPanelLaneEdit(Knob::Freq, layout.freqRect);
        return true;
    }
    if (layout.gainRect.contains(event.position)) {
        beginPanelLaneEdit(Knob::Gain, layout.gainRect);
        return true;
    }
    if (layout.hasQRow && layout.qRect.contains(event.position)) {
        beginPanelLaneEdit(Knob::Q, layout.qRect);
        return true;
    }
    if (layout.hasDynamic) {
        if (layout.dynamicEnableRect.contains(event.position)) {
            bd.dynamicEnabled = !bd.dynamicEnabled;
            if (bd.sidechainLinked) {
                bd.sidechainFreq = bd.freq;
                bd.sidechainQ = bd.q;
            }
            writeDynamicBandSnapshot(idx);
            setDirty(true);
            return true;
        }
        if (layout.thresholdRect.contains(event.position)) {
            beginPanelLaneEdit(Knob::Threshold, layout.thresholdRect);
            return true;
        }
        if (layout.attackRect.contains(event.position)) {
            beginPanelLaneEdit(Knob::Attack, layout.attackRect);
            return true;
        }
        if (layout.releaseRect.contains(event.position)) {
            beginPanelLaneEdit(Knob::Release, layout.releaseRect);
            return true;
        }
    }
    return true;
}

bool AestraEQEditor::handleSelectedNodeQuickActionClick(const NUIMouseEvent& event) {
    if (!event.pressed || event.button != NUIMouseButton::Left)
        return false;
    if (m_selectedBand < 0 || m_selectedBand >= static_cast<int>(m_bands.size()))
        return false;
    if (m_nodeQuickActionRect.width <= 0.0f || !m_nodeQuickActionRect.contains(event.position))
        return false;

    auto& bd = m_bands[static_cast<size_t>(m_selectedBand)];
    for (size_t i = 0; i < m_nodeQuickActionRects.size(); ++i) {
        if (!m_nodeQuickActionRects[i].contains(event.position))
            continue;
        const auto action = static_cast<NodeQuickAction>(i);
        switch (action) {
        case NodeQuickAction::TypePrev:
        case NodeQuickAction::TypeNext:
            if (bd.typeId != 0 || !bd.legacySlot) {
                const int optionCount = bd.legacySlot ? 4 : 8;
                const int current =
                    bd.legacySlot
                        ? std::clamp(
                              static_cast<int>(std::round(
                                  quantizeTypeNorm(m_instance ? m_instance->getParameter(bd.typeId) : bd.typeNorm) *
                                  3.0f)),
                              0, 3)
                        : std::clamp(static_cast<int>(std::round(std::clamp(bd.typeNorm, 0.0f, 1.0f) * 7.0f)), 0, 7);
                const int delta = action == NodeQuickAction::TypePrev ? -1 : 1;
                const int next = (current + delta + optionCount) % optionCount;
                m_stereoMenuBand = -1;
                m_stereoMenuFromNodeQuickAction = false;
                m_hoveredStereoOption = -1;
                closeBandContextMenu();
                setBandType(m_selectedBand, static_cast<uint32_t>(next));
            }
            return true;
        case NodeQuickAction::Stereo:
            m_stereoMenuBand = (m_stereoMenuBand == m_selectedBand) ? -1 : m_selectedBand;
            m_stereoMenuFromNodeQuickAction = m_stereoMenuBand >= 0;
            m_hoveredStereoOption = -1;
            m_typeMenuBand = -1;
            m_typeMenuFromNodeQuickAction = false;
            m_hoveredTypeOption = -1;
            closeBandContextMenu();
            setDirty(true);
            return true;
        case NodeQuickAction::Solo: {
            auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
            if (eq) {
                const int32_t solo = eq->getSoloBandSlot();
                eq->setSoloBandSlot(solo == static_cast<int32_t>(bd.slotIndex) ? -1
                                                                               : static_cast<int32_t>(bd.slotIndex));
                setDirty(true);
            }
            return true;
        }
        case NodeQuickAction::Duplicate:
            copyBandToClipboard(m_selectedBand);
            duplicateBand(m_selectedBand);
            return true;
        case NodeQuickAction::Delete:
            deleteBand(m_selectedBand);
            return true;
        case NodeQuickAction::Count:
            return true;
        }
    }
    return true;
}

bool AestraEQEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible())
        return false;
    const auto b = getBounds();
    if (event.pressed && b.contains(event.position)) {
        setFocused(true);
    }
    if (AestraPanelWindow::onMouseEvent(event))
        return true;

    const bool contains = b.contains(event.position);
    const bool dragging =
        m_draggingGraphBand >= 0 || m_draggingDetectorBand >= 0 || m_draggingCardBand >= 0 || m_draggingOutputGain;
    if (!contains && !isDraggingWindow() && !dragging)
        return false;

    if (event.pressed && event.button == NUIMouseButton::Left && m_numericEditActive &&
        !numericEditBounds().contains(event.position)) {
        commitNumericEdit();
    }

    if (handleFloatingBandPanelClick(event))
        return true;

    if (handleSelectedNodeQuickActionClick(event))
        return true;

    if (handleRightClickActions(event)) {
        return true;
    }
    if (handleOpenMenusClick(event)) {
        return true;
    }
    if (handleHeaderButtonsClick(event)) {
        return true;
    }
    if (handleAnalyzerPanelClick(event)) {
        return true;
    }
    if (handleWheel(event)) {
        return true;
    }
    if (handlePress(event)) {
        return true;
    }
    if (handleDragUpdate(event)) {
        return true;
    }
    handleHoverUpdate(event);

    return contains;
}

bool AestraEQEditor::handleRightClickActions(const NUIMouseEvent& event) {
    if (event.pressed && event.button == NUIMouseButton::Right) {
        using EQ = Aestra::Audio::Plugins::AestraEQ;
        closeBandContextMenu();
        if (m_typeMenuBand >= 0 && !m_typeMenuRect.contains(event.position)) {
            m_typeMenuBand = -1;
            m_hoveredTypeOption = -1;
            setDirty(true);
        }
        if (m_stereoMenuBand >= 0 && !m_stereoMenuRect.contains(event.position)) {
            m_stereoMenuBand = -1;
            m_hoveredStereoOption = -1;
            setDirty(true);
        }
        if (m_bypassRect.contains(event.position)) {
            resetParameterToDefault(EQ::kParamBypass);
            return true;
        }
        if (m_outputGainRect.contains(event.position)) {
            resetParameterToDefault(EQ::kParamOutputGain);
            return true;
        }
        if (m_polarityRect.contains(event.position)) {
            resetParameterToDefault(EQ::kParamPolarityInvert);
            return true;
        }
        if (m_compareCopyRect.contains(event.position)) {
            copyCompareSlotToOther();
            return true;
        }

        const int floatingIdx = currentFloatingBandIndex();
        if (floatingIdx >= 0) {
            const auto floating = floatingBandPanelLayout(floatingIdx, m_graphBounds);
            if (floating.valid && floating.panel.contains(event.position)) {
                openBandContextMenu(floatingIdx, event.position);
                return true;
            }
        }

        Knob knob = Knob::None;
        const int cardIdx = hitTestBandCard(event.position.x, event.position.y, knob);
        if (cardIdx >= 0) {
            openBandContextMenu(cardIdx, event.position);
            return true;
        }

        const int graphIdx = hitTestGraphNode(event.position.x, event.position.y);
        if (graphIdx >= 0) {
            openBandContextMenu(graphIdx, event.position);
            return true;
        }
    }
    return false;
}

bool AestraEQEditor::handleOpenMenusClick(const NUIMouseEvent& event) {
    if (m_bandContextMenuBand >= 0 && event.pressed && event.button == NUIMouseButton::Left) {
        static constexpr BandMenuAction kActions[] = {BandMenuAction::Reset,         BandMenuAction::InvertGain,
                                                      BandMenuAction::ToggleDynamic, BandMenuAction::SplitLR,
                                                      BandMenuAction::SplitMS,       BandMenuAction::Copy,
                                                      BandMenuAction::Paste,         BandMenuAction::Duplicate,
                                                      BandMenuAction::Delete,        BandMenuAction::ClearAll};
        static_assert(std::size(kActions) == std::tuple_size_v<decltype(m_bandContextOptionRects)>,
                      "Band context click actions must match rendered option rows");
        for (size_t i = 0; i < m_bandContextOptionRects.size(); ++i) {
            if (m_bandContextOptionRects[i].contains(event.position)) {
                applyBandContextAction(kActions[i]);
                return true;
            }
        }
        if (!m_bandContextMenuRect.contains(event.position)) {
            closeBandContextMenu();
        }
    }

    if (m_typeMenuBand >= 0 && event.pressed && event.button == NUIMouseButton::Left) {
        for (size_t i = 0; i < m_typeOptionRects.size(); ++i) {
            if (m_typeOptionRects[i].contains(event.position)) {
                setBandType(m_typeMenuBand, static_cast<uint32_t>(i));
                return true;
            }
        }
        if (!m_typeMenuRect.contains(event.position)) {
            m_typeMenuBand = -1;
            m_hoveredTypeOption = -1;
            setDirty(true);
        }
    }

    if (m_stereoMenuBand >= 0 && event.pressed && event.button == NUIMouseButton::Left) {
        for (size_t i = 0; i < m_stereoOptionRects.size(); ++i) {
            if (m_stereoOptionRects[i].contains(event.position)) {
                setBandStereoMode(m_stereoMenuBand, static_cast<uint32_t>(i));
                return true;
            }
        }
        if (!m_stereoMenuRect.contains(event.position)) {
            m_stereoMenuBand = -1;
            m_hoveredStereoOption = -1;
            setDirty(true);
        }
    }
    return false;
}

bool AestraEQEditor::handleHeaderButtonsClick(const NUIMouseEvent& event) {
    // Bypass click
    if (event.pressed && event.button == NUIMouseButton::Left && m_bypassRect.contains(event.position)) {
        setBypassed(!isBypassed());
        return true;
    }

    if (event.pressed && event.button == NUIMouseButton::Left && m_polarityRect.contains(event.position)) {
        setPolarityInverted(!polarityInverted());
        return true;
    }

    if (event.pressed && event.button == NUIMouseButton::Left && m_compareARect.contains(event.position)) {
        switchCompareSlot(0);
        return true;
    }

    if (event.pressed && event.button == NUIMouseButton::Left && m_compareBRect.contains(event.position)) {
        switchCompareSlot(1);
        return true;
    }

    if (event.pressed && event.button == NUIMouseButton::Left && m_compareCopyRect.contains(event.position)) {
        copyCompareSlotToOther();
        return true;
    }

    if (event.pressed && event.button == NUIMouseButton::Left && m_curveScaleRect.contains(event.position)) {
        cycleCurveDbRange();
        return true;
    }

    if (event.pressed && event.button == NUIMouseButton::Left && m_analyzerMenuRect.contains(event.position)) {
        m_analyzerPanelOpen = !m_analyzerPanelOpen;
        setDirty(true);
        return true;
    }
    return false;
}

bool AestraEQEditor::handleAnalyzerPanelClick(const NUIMouseEvent& event) {
    if (m_analyzerPanelOpen && event.pressed && event.button == NUIMouseButton::Left &&
        m_analyzerSourceRect.contains(event.position)) {
        cycleAnalyzerSource();
        return true;
    }

    if (m_analyzerPanelOpen && event.pressed && event.button == NUIMouseButton::Left &&
        m_analyzerStereoRect.contains(event.position)) {
        cycleAnalyzerStereoMode();
        return true;
    }

    if (m_analyzerPanelOpen && event.pressed && event.button == NUIMouseButton::Left &&
        m_analyzerTiltRect.contains(event.position)) {
        cycleAnalyzerTilt();
        return true;
    }

    if (m_analyzerPanelOpen && event.pressed && event.button == NUIMouseButton::Left &&
        m_analyzerDecayRect.contains(event.position)) {
        cycleAnalyzerDecay();
        return true;
    }

    if (m_analyzerPanelOpen && event.pressed && event.button == NUIMouseButton::Left &&
        m_analyzerFreezeRect.contains(event.position)) {
        setAnalyzerFrozen(!m_analyzerFrozen);
        return true;
    }

    if (m_analyzerPanelOpen && event.pressed && event.button == NUIMouseButton::Left &&
        m_analyzerCollisionRect.contains(event.position)) {
        setAnalyzerCollisionEnabled(!m_analyzerCollisionEnabled);
        return true;
    }

    if (m_analyzerPanelOpen && event.pressed && event.button == NUIMouseButton::Left &&
        m_analyzerCollisionStrengthRect.contains(event.position)) {
        cycleAnalyzerCollisionStrength();
        return true;
    }

    if (m_analyzerPanelOpen && event.pressed && event.button == NUIMouseButton::Left &&
        !m_analyzerPanelRect.contains(event.position)) {
        m_analyzerPanelOpen = false;
        setDirty(true);
    }
    return false;
}

bool AestraEQEditor::handleWheel(const NUIMouseEvent& event) {
    if (event.wheelDelta != 0.0f && m_outputGainRect.contains(event.position)) {
        const float stepDb = (event.modifiers & NUIModifiers::Shift) ? 0.1f : 1.0f;
        setOutputGain(outputGain() + (event.wheelDelta > 0 ? stepDb : -stepDb) / 36.0f);
        return true;
    }

    // Wheel on graph nodes adjusts Q/slope
    if (event.wheelDelta != 0.0f) {
        int idx = hitTestGraphNode(event.position.x, event.position.y);
        if (idx >= 0) {
            auto& bd = m_bands[idx];
            m_selectedBand = idx;
            if (bd.usesSlope) {
                const float cur = std::round(quantizeSlopeNorm(bd.q) * 6.0f);
                const float nx = std::clamp(cur + (event.wheelDelta > 0 ? 1.0f : -1.0f), 0.0f, 6.0f);
                bd.q = nx / 6.0f;
            } else {
                const float step = (event.modifiers & NUIModifiers::Shift) ? 0.01f : 0.04f;
                bd.q = std::clamp(bd.q + (event.wheelDelta > 0 ? step : -step), 0.0f, 1.0f);
            }
            if (bd.legacySlot && m_instance)
                m_instance->setParameter(bd.qId, bd.q);
            else
                writeDynamicBandSnapshot(idx);
            setDirty(true);
            return true;
        }
    }
    return false;
}

bool AestraEQEditor::handlePress(const NUIMouseEvent& event) {
    // Press
    if (event.pressed && event.button == NUIMouseButton::Left) {
        const int selectedIdx =
            (m_selectedBand >= 0 && m_selectedBand < static_cast<int>(m_bands.size())) ? m_selectedBand : 0;
        const NUIRect slotRailHit{m_selectedSlotRailRect.x, m_selectedSlotRailRect.y - 7.0f,
                                  m_selectedSlotRailRect.width, m_selectedSlotRailRect.height + 14.0f};
        if (m_selectedSlotRailRect.width > 0.0f && slotRailHit.contains(event.position)) {
            const float t =
                std::clamp((event.position.x - m_selectedSlotRailRect.x) / std::max(1.0f, m_selectedSlotRailRect.width),
                           0.0f, 1.0f);
            const uint32_t slot = static_cast<uint32_t>(
                std::round(t * static_cast<float>(Aestra::Audio::Plugins::AestraEQ::kMaxDynamicBands - 1)));
            const auto found = std::find_if(m_bands.begin(), m_bands.end(),
                                            [slot](const Band& band) { return band.slotIndex == slot; });
            if (found != m_bands.end()) {
                m_selectedBand = static_cast<int>(std::distance(m_bands.begin(), found));
                m_typeMenuBand = -1;
                m_typeMenuFromNodeQuickAction = false;
                m_hoveredTypeOption = -1;
                m_stereoMenuBand = -1;
                m_stereoMenuFromNodeQuickAction = false;
                m_hoveredStereoOption = -1;
                closeBandContextMenu();
                setDirty(true);
                return true;
            }
        }
        if (m_selectedCollapseRect.contains(event.position)) {
            m_bandInspectorCollapsed = !m_bandInspectorCollapsed;
            m_typeMenuBand = -1;
            m_typeMenuFromNodeQuickAction = false;
            m_hoveredTypeOption = -1;
            m_stereoMenuBand = -1;
            m_stereoMenuFromNodeQuickAction = false;
            m_hoveredStereoOption = -1;
            setDirty(true);
            return true;
        }
        if (m_selectedPrevRect.contains(event.position)) {
            selectAdjacentBand(-1);
            return true;
        }
        if (m_selectedNextRect.contains(event.position)) {
            selectAdjacentBand(1);
            return true;
        }
        if (m_selectedDuplicateRect.contains(event.position)) {
            copyBandToClipboard(selectedIdx);
            setDirty(true);
            return true;
        }
        if (m_selectedDeleteRect.contains(event.position)) {
            if (selectedIdx >= 0 && selectedIdx < static_cast<int>(m_bands.size())) {
                deleteBand(selectedIdx);
            }
            return true;
        }
        int detectorIdx = hitTestDynamicDetectorHandle(event.position.x, event.position.y);
        if (detectorIdx >= 0) {
            m_selectedBand = detectorIdx;
            m_draggingDetectorBand = detectorIdx;
            auto& bd = m_bands[static_cast<size_t>(detectorIdx)];
            m_graphDragStartPosition = event.position;
            m_detectorDragStartFreq = bd.sidechainFreq;
            m_detectorDragStartQ = bd.sidechainQ;
            updateDynamicDetectorFromGraphPosition(detectorIdx, event.position, event.modifiers);
            return true;
        }
        int idx = hitTestGraphNode(event.position.x, event.position.y);
        if (idx >= 0) {
            m_selectedBand = idx;
            m_bandInspectorCollapsed = false;
            auto& bd = m_bands[static_cast<size_t>(idx)];
            if (event.doubleClick) {
                beginNumericEdit(idx, bd.usesGain ? Knob::Gain : Knob::Q);
                return true;
            }
            if (!bd.enabled) {
                setBandValue(idx, Knob::Enable, 1.0f);
            }
            m_draggingGraphBand = idx;
            m_graphDragStartPosition = event.position;
            m_graphDragStartFreq = bd.freq;
            m_graphDragStartGain = bd.gain;
            updateBandFromGraphPosition(idx, event.position, event.modifiers);
            return true;
        }
        if (graphInnerBounds(m_graphBounds).contains(event.position) && createBandAtGraphPoint(event.position)) {
            m_draggingGraphBand = m_selectedBand;
            m_graphDragStartPosition = event.position;
            m_graphDragStartFreq = m_selectedBand >= 0 ? m_bands[static_cast<size_t>(m_selectedBand)].freq : 0.5f;
            m_graphDragStartGain = m_selectedBand >= 0 ? m_bands[static_cast<size_t>(m_selectedBand)].gain : 0.5f;
            return true;
        }
        if (m_outputGainRect.contains(event.position)) {
            m_draggingOutputGain = true;
            // Relative vertical drag → cursor capture (the graph nodes and card
            // lanes are absolute-position and stay on the visible cursor).
            beginKnobCapture(m_outputGainRect.center(), event.position);
            return true;
        }
        Knob knob = Knob::None;
        int cardIdx = hitTestBandCard(event.position.x, event.position.y, knob);
        if (cardIdx >= 0) {
            m_selectedBand = cardIdx;
            if (event.doubleClick && knob != Knob::None && knob != Knob::Enable && knob != Knob::Type &&
                knob != Knob::Stereo) {
                beginNumericEdit(cardIdx, knob);
                return true;
            }
            if (knob == Knob::None) {
                setDirty(true);
                return true;
            }
            if (knob == Knob::Enable) {
                auto& bd = m_bands[cardIdx];
                const bool nowEnabled = !bd.enabled;
                setBandValue(cardIdx, Knob::Enable, nowEnabled ? 1.0f : 0.0f);
                return true;
            }
            if (knob == Knob::Type) {
                m_typeMenuBand = (m_typeMenuBand == cardIdx) ? -1 : cardIdx;
                m_typeMenuFromNodeQuickAction = false;
                m_hoveredTypeOption = -1;
                m_stereoMenuBand = -1;
                m_stereoMenuFromNodeQuickAction = false;
                m_hoveredStereoOption = -1;
                setDirty(true);
                return true;
            }
            if (knob == Knob::Stereo) {
                m_stereoMenuBand = (m_stereoMenuBand == cardIdx) ? -1 : cardIdx;
                m_stereoMenuFromNodeQuickAction = false;
                m_hoveredStereoOption = -1;
                m_typeMenuBand = -1;
                m_typeMenuFromNodeQuickAction = false;
                m_hoveredTypeOption = -1;
                setDirty(true);
                return true;
            }
            m_draggingCardBand = cardIdx;
            m_draggingKnob = knob;
            m_draggingLaneRect = (knob == Knob::Freq)   ? m_bands[cardIdx].freqKnob
                                 : (knob == Knob::Gain) ? m_bands[cardIdx].gainKnob
                                                        : m_bands[cardIdx].qKnob;
            m_dragStartY = event.position.y;
            const auto& bd = m_bands[cardIdx];
            m_dragStartValue = (knob == Knob::Freq)   ? bd.freq
                               : (knob == Knob::Gain) ? (bd.usesSlope ? bd.q : bd.gain)
                                                      : bd.q;
            setBandValue(cardIdx, knob, normalizedFromLaneX(m_draggingLaneRect, event.position.x));
            return true;
        }
    }
    return false;
}

bool AestraEQEditor::handleDragUpdate(const NUIMouseEvent& event) {
    // Drag
    if (m_draggingGraphBand >= 0) {
        updateBandFromGraphPosition(m_draggingGraphBand, event.position, event.modifiers);
        if (!event.pressed && event.button == NUIMouseButton::Left)
            m_draggingGraphBand = -1;
        return true;
    }
    if (m_draggingDetectorBand >= 0) {
        updateDynamicDetectorFromGraphPosition(m_draggingDetectorBand, event.position, event.modifiers);
        if (!event.pressed && event.button == NUIMouseButton::Left)
            m_draggingDetectorBand = -1;
        return true;
    }
    if (m_draggingOutputGain) {
        // Service-owned frame delta (up = increase), accumulated into value.
        setOutputGain(outputGain() + knobDragStep(event, 180.0f));
        if (!event.pressed && event.button == NUIMouseButton::Left) {
            endKnobCapture();
            m_draggingOutputGain = false;
        }
        return true;
    }
    if (m_draggingCardBand >= 0) {
        setBandValue(m_draggingCardBand, m_draggingKnob, normalizedFromLaneX(m_draggingLaneRect, event.position.x));
        if (!event.pressed && event.button == NUIMouseButton::Left) {
            m_draggingCardBand = -1;
            m_draggingKnob = Knob::None;
            m_draggingLaneRect = NUIRect();
        }
        return true;
    }
    return false;
}

void AestraEQEditor::handleHoverUpdate(const NUIMouseEvent& event) {
    const bool contains = getBounds().contains(event.position);
    // Hover
    if (!event.pressed && !event.released) {
        Knob k = Knob::None;
        const int detectorIdx = hitTestDynamicDetectorHandle(event.position.x, event.position.y);
        const int graphIdx = detectorIdx >= 0 ? -1 : hitTestGraphNode(event.position.x, event.position.y);
        int idx = graphIdx;
        int floatingPanelHover = -1;
        if (floatingPanelHover < 0) {
            const int floatingCandidate = m_hoveredFloatingBand;
            if (floatingCandidate >= 0 && floatingCandidate < static_cast<int>(m_bands.size())) {
                const auto floating = floatingBandPanelLayout(floatingCandidate, m_graphBounds);
                if (floating.valid && floating.panel.contains(event.position)) {
                    floatingPanelHover = floatingCandidate;
                }
            }
        }
        if (idx < 0 && contains)
            idx = hitTestBandCard(event.position.x, event.position.y, k);
        const bool overGraphNode = graphIdx >= 0 || detectorIdx >= 0;
        const bool overNodeQuickActions = m_nodeQuickActionRect.contains(event.position);
        const auto graphInner = graphInnerBounds(m_graphBounds);
        const NUIRect graphControlZone{graphInner.right() - 116.0f, graphInner.y - 2.0f, 118.0f, 28.0f};
        const bool graphCursorVisible = graphInner.contains(event.position) && !overGraphNode &&
                                        !overNodeQuickActions && !graphControlZone.contains(event.position);
        if (graphCursorVisible != m_graphCursorVisible ||
            (graphCursorVisible && (std::abs(event.position.x - m_graphCursorPoint.x) > 0.5f ||
                                    std::abs(event.position.y - m_graphCursorPoint.y) > 0.5f))) {
            m_graphCursorVisible = graphCursorVisible;
            m_graphCursorPoint = event.position;
            setDirty(true);
        }
        const bool hoveredFromGraph = graphIdx >= 0;
        if (idx != m_hoveredBand || hoveredFromGraph != m_hoveredBandFromGraph) {
            m_hoveredBand = idx;
            m_hoveredBandFromGraph = hoveredFromGraph;
            setDirty(true);
        }
        if (floatingPanelHover != m_hoveredFloatingBand) {
            m_hoveredFloatingBand = floatingPanelHover;
            setDirty(true);
        }
        const bool hover = m_bypassRect.contains(event.position);
        if (hover != m_bypassHovered) {
            m_bypassHovered = hover;
            setDirty(true);
        }
        int typeOptionHover = -1;
        if (m_typeMenuBand >= 0) {
            for (size_t i = 0; i < m_typeOptionRects.size(); ++i) {
                if (m_typeOptionRects[i].contains(event.position)) {
                    typeOptionHover = static_cast<int>(i);
                    break;
                }
            }
        }
        if (typeOptionHover != m_hoveredTypeOption) {
            m_hoveredTypeOption = typeOptionHover;
            setDirty(true);
        }
        int stereoOptionHover = -1;
        if (m_stereoMenuBand >= 0) {
            for (size_t i = 0; i < m_stereoOptionRects.size(); ++i) {
                if (m_stereoOptionRects[i].contains(event.position)) {
                    stereoOptionHover = static_cast<int>(i);
                    break;
                }
            }
        }
        if (stereoOptionHover != m_hoveredStereoOption) {
            m_hoveredStereoOption = stereoOptionHover;
            setDirty(true);
        }
        int bandContextOptionHover = -1;
        if (m_bandContextMenuBand >= 0) {
            for (size_t i = 0; i < m_bandContextOptionRects.size(); ++i) {
                if (m_bandContextOptionRects[i].contains(event.position)) {
                    bandContextOptionHover = static_cast<int>(i);
                    break;
                }
            }
        }
        if (bandContextOptionHover != m_hoveredBandContextOption) {
            m_hoveredBandContextOption = bandContextOptionHover;
            setDirty(true);
        }
        int nodeQuickActionHover = -1;
        if (m_nodeQuickActionRect.contains(event.position)) {
            for (size_t i = 0; i < m_nodeQuickActionRects.size(); ++i) {
                if (m_nodeQuickActionRects[i].contains(event.position)) {
                    nodeQuickActionHover = static_cast<int>(i);
                    break;
                }
            }
        }
        if (nodeQuickActionHover != m_hoveredNodeQuickAction) {
            m_hoveredNodeQuickAction = nodeQuickActionHover;
            m_nodeQuickActionHoverStarted = std::chrono::steady_clock::now();
            setDirty(true);
        }
        const bool outputHover = m_outputGainRect.contains(event.position);
        if (outputHover != m_outputGainHovered) {
            m_outputGainHovered = outputHover;
            setDirty(true);
        }
        const bool polarityHover = m_polarityRect.contains(event.position);
        if (polarityHover != m_polarityHovered) {
            m_polarityHovered = polarityHover;
            setDirty(true);
        }
        const bool compareAHover = m_compareARect.contains(event.position);
        if (compareAHover != m_compareAHovered) {
            m_compareAHovered = compareAHover;
            setDirty(true);
        }
        const bool compareBHover = m_compareBRect.contains(event.position);
        if (compareBHover != m_compareBHovered) {
            m_compareBHovered = compareBHover;
            setDirty(true);
        }
        const bool compareCopyHover = m_compareCopyRect.contains(event.position);
        if (compareCopyHover != m_compareCopyHovered) {
            m_compareCopyHovered = compareCopyHover;
            setDirty(true);
        }
        const bool prevHover = m_selectedPrevRect.contains(event.position);
        if (prevHover != m_selectedPrevHovered) {
            m_selectedPrevHovered = prevHover;
            setDirty(true);
        }
        const bool nextHover = m_selectedNextRect.contains(event.position);
        if (nextHover != m_selectedNextHovered) {
            m_selectedNextHovered = nextHover;
            setDirty(true);
        }
        const bool duplicateHover = m_selectedDuplicateRect.contains(event.position);
        if (duplicateHover != m_selectedDuplicateHovered) {
            m_selectedDuplicateHovered = duplicateHover;
            setDirty(true);
        }
        const bool deleteHover = m_selectedDeleteRect.contains(event.position);
        if (deleteHover != m_selectedDeleteHovered) {
            m_selectedDeleteHovered = deleteHover;
            setDirty(true);
        }
        const bool collapseHover = m_selectedCollapseRect.contains(event.position);
        if (collapseHover != m_selectedCollapseHovered) {
            m_selectedCollapseHovered = collapseHover;
            setDirty(true);
        }
        const int selectedForHover =
            (m_selectedBand >= 0 && m_selectedBand < static_cast<int>(m_bands.size())) ? m_selectedBand : -1;
        const bool stereoHover = selectedForHover >= 0 &&
                                 m_bands[static_cast<size_t>(selectedForHover)].stereoButton.contains(event.position);
        if (stereoHover != m_selectedStereoHovered) {
            m_selectedStereoHovered = stereoHover;
            setDirty(true);
        }
        const bool scaleHover = m_curveScaleRect.contains(event.position);
        if (scaleHover != m_curveScaleHovered) {
            m_curveScaleHovered = scaleHover;
            setDirty(true);
        }
        const bool analyzerMenuHover = m_analyzerMenuRect.contains(event.position);
        if (analyzerMenuHover != m_analyzerMenuHovered) {
            m_analyzerMenuHovered = analyzerMenuHover;
            setDirty(true);
        }
        const bool analyzerSourceHover = m_analyzerPanelOpen && m_analyzerSourceRect.contains(event.position);
        if (analyzerSourceHover != m_analyzerSourceHovered) {
            m_analyzerSourceHovered = analyzerSourceHover;
            setDirty(true);
        }
        const bool analyzerStereoHover = m_analyzerPanelOpen && m_analyzerStereoRect.contains(event.position);
        if (analyzerStereoHover != m_analyzerStereoHovered) {
            m_analyzerStereoHovered = analyzerStereoHover;
            setDirty(true);
        }
        const bool analyzerTiltHover = m_analyzerPanelOpen && m_analyzerTiltRect.contains(event.position);
        if (analyzerTiltHover != m_analyzerTiltHovered) {
            m_analyzerTiltHovered = analyzerTiltHover;
            setDirty(true);
        }
        const bool analyzerDecayHover = m_analyzerPanelOpen && m_analyzerDecayRect.contains(event.position);
        if (analyzerDecayHover != m_analyzerDecayHovered) {
            m_analyzerDecayHovered = analyzerDecayHover;
            setDirty(true);
        }
        const bool analyzerFreezeHover = m_analyzerPanelOpen && m_analyzerFreezeRect.contains(event.position);
        if (analyzerFreezeHover != m_analyzerFreezeHovered) {
            m_analyzerFreezeHovered = analyzerFreezeHover;
            setDirty(true);
        }
        const bool analyzerCollisionHover = m_analyzerPanelOpen && m_analyzerCollisionRect.contains(event.position);
        if (analyzerCollisionHover != m_analyzerCollisionHovered) {
            m_analyzerCollisionHovered = analyzerCollisionHover;
            setDirty(true);
        }
        const bool analyzerCollisionStrengthHover =
            m_analyzerPanelOpen && m_analyzerCollisionStrengthRect.contains(event.position);
        if (analyzerCollisionStrengthHover != m_analyzerCollisionStrengthHovered) {
            m_analyzerCollisionStrengthHovered = analyzerCollisionStrengthHover;
            setDirty(true);
        }
        int hoveredSlot = -1;
        const NUIRect slotRailHit{m_selectedSlotRailRect.x, m_selectedSlotRailRect.y - 7.0f,
                                  m_selectedSlotRailRect.width, m_selectedSlotRailRect.height + 14.0f};
        if (m_selectedSlotRailRect.width > 0.0f && slotRailHit.contains(event.position)) {
            const float t =
                std::clamp((event.position.x - m_selectedSlotRailRect.x) / std::max(1.0f, m_selectedSlotRailRect.width),
                           0.0f, 1.0f);
            const uint32_t slot = static_cast<uint32_t>(
                std::round(t * static_cast<float>(Aestra::Audio::Plugins::AestraEQ::kMaxDynamicBands - 1)));
            const auto found = std::find_if(m_bands.begin(), m_bands.end(),
                                            [slot](const Band& band) { return band.slotIndex == slot; });
            if (found != m_bands.end()) {
                hoveredSlot = static_cast<int>(slot);
            }
        }
        if (hoveredSlot != m_hoveredSelectedSlot) {
            m_hoveredSelectedSlot = hoveredSlot;
            setDirty(true);
        }
    }
}

} // namespace AestraUI
