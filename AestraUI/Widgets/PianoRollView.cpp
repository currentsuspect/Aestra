// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "NUIPianoRollWidgets.h"
#include "PianoRollInspector.h"
#include "PlayheadMarker.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "PianoRollDuplicate.h"
#include "PianoRollWidgetShared.h"
#include "../Helpers/PianoRollInteraction.h"
#include <algorithm>
#include <cmath>

namespace AestraUI {

// Gap between the minimap (overview) and the ruler — both the layout and the
// pitch-header geometry must agree on it so the key lane stays aligned.
// Chrome band heights, in one place. onRender() and layoutChildren() each used
// to carry their own copy of all four, so the header's painted geometry and the
// grid's actual geometry were free to drift apart.
//
// Spec 2 §6: the editing canvas is the main object, so every band around it is
// as small as it can be while its own contents still fit. The toolbar's buttons
// are 30px, which 42 clears; the ruler and minimap are text and a strip.
constexpr float kToolbarHeight = 42.0f;
constexpr float kMinimapHeight = 22.0f;
constexpr float kRulerHeight = 44.0f; // FD-63: the chord ruler, a bar number over its chord
constexpr float kStatusHeight = 24.0f; // FD-63: the status line under the lanes
constexpr float kMinimapGap = 0.0f; // FD-63: the overview sits on the ruler, one band

// =============================================================================
// PianoRollView (split from NUIPianoRollWidgets.cpp)
// =============================================================================
PianoRollView::PianoRollView()
    // Spec 2 §6: the pitch lane keeps its key shapes and octave labels at 58px;
    // the 18px it gives back goes straight to the grid.
    : m_keyLaneWidth(68.0f), m_rulerHeight(kRulerHeight), m_pixelsPerBeat(80.0f), m_keyHeight(16.0f), // FD-63: 16 px rows
      m_scrollX(0.0f), m_targetScrollX(0.0f)
{
    // FD-63 board: C5 (MIDI 72) at the top until a pattern's notes say otherwise (frameNotesVertically).
    m_scrollY = (127.0f - 72.0f) * m_keyHeight;
    m_targetScrollY = m_scrollY;
    m_keys = std::make_shared<PianoRollKeyLane>();
    m_ruler = std::make_shared<PianoRollRuler>();
    m_grid = std::make_shared<PianoRollGrid>();
    m_notes = std::make_shared<PianoRollNoteLayer>();
    m_controls = std::make_shared<PianoRollControlPanel>();
    m_inspector = std::make_shared<PianoRollInspector>();

    m_notes->setOnHoveredPitchChanged([this](int pitch) {
        if (m_grid) m_grid->setHoveredPitch(pitch);
        if (m_keys) m_keys->setHoveredKey(pitch);
        if (m_statusPitch != pitch) { m_statusPitch = pitch; repaint(); }
    });
    m_keys->setOnHoveredKeyChanged([this](int pitch) {
        if (m_grid) m_grid->setHoveredPitch(pitch);
        if (m_statusPitch != pitch) { m_statusPitch = pitch; repaint(); }
    });
    // A note pressed in the grid presses its key (SPEC 3 §5.1).
    m_notes->setOnKeyPressChanged([this](int pitch, bool down) {
        if (!m_keys) return;
        if (down) {
            m_keys->pressKey(pitch);
        } else {
            m_keys->releaseKey(pitch);
        }
    });
    
    // Toolbar
    m_toolbar = std::make_shared<PianoRollToolbar>();
    m_toolbar->setGrid(m_grid);
    m_toolbar->setNoteLayer(m_notes);
    m_toolbar->setKeyLane(m_keys);
    m_toolbar->setPatternLengthBeats(m_patternLengthBeats);
    m_toolbar->setOnShowShortcutHelp([this]() {
        m_showShortcutHelp = !m_showShortcutHelp;
        repaint();
    });

    m_controls->setNoteLayer(m_notes);
    m_inspector->setNoteLayer(m_notes);
    
    m_minimap = std::make_shared<PianoRollMinimap>(); // Local Minimap
    
    m_vScroll = std::make_shared<NUIScrollbar>(NUIScrollbar::Orientation::Vertical);
    m_vScroll->setOrientation(NUIScrollbar::Orientation::Vertical);
    // No styling here: drawOverlayScrollbar() owns how every scrollbar looks
    // (spec 2 §2), and the pixel thumb floor is enforced by the component.
    m_vScroll->setMinimumThumbSize(0.06);

    // Initial default layout config
    m_minimap->setVisible(true);
    m_vScroll->setVisible(true);

    m_hScroll = std::make_shared<NUIScrollbar>(NUIScrollbar::Orientation::Horizontal);
    m_hScroll->setOrientation(NUIScrollbar::Orientation::Horizontal);
    m_hScroll->setMinimumThumbSize(0.06);
    m_hScroll->setVisible(true);

    // Ruler Zoom Callback
    m_ruler->onZoomRequested = [this](float delta, float mouseX) {
        const float zoomDelta = std::clamp(delta, -4.0f, 4.0f);
        applyZoom(std::pow(1.15f, zoomDelta), mouseX);
    };

    m_ruler->onPlayheadScrubbed = [this](double beat, bool active) {
        const double clampedBeat = std::clamp(beat, 0.0, m_totalDurationBeats);
        setPlayheadBeat(clampedBeat, false);
        if (m_onPlayheadScrubbed) {
            m_onPlayheadScrubbed(clampedBeat, active);
        }
    };

    // Ruler loop zone (SPEC 3 §5.2): snapped to the editor's grid, nearest line.
    m_ruler->snapBeat = [this](double beat) {
        const double step = m_notes ? MusicTheory::getSnapDuration(m_notes->getSnap()) : 0.0;
        return step > 0.0 ? std::round(beat / step) * step : beat;
    };
    m_ruler->onLoopZoneDrawn = [this](double start, double end) {
        // Clamped to the pattern, like the scrub: past its end there is only silence to loop. A
        // zone entirely past the end becomes empty, and applyLoopZone drops it.
        const double limit = std::max(0.0, m_totalDurationBeats);
        applyLoopZone(std::make_pair(std::clamp(start, 0.0, limit), std::clamp(end, 0.0, limit)), true);
        // The highlight selects the notes inside it (V82-PR1, FD-52), so Duplicate has something to act on.
        if (m_loopZone) m_notes->selectStartingIn(m_loopZone->first, m_loopZone->second);
    };
    // Duplicate past the pattern's end grows the pattern by whole bars first (as the toolbar's + does).
    m_notes->setOnNeedPatternEnd([this](double endBeat) {
        const int bars = PianoRollDuplicate::barsToGrow(endBeat, m_patternLengthBeats, static_cast<double>(m_beatsPerBar));
        if (bars > 0 && m_toolbar) m_toolbar->requestPatternBars(bars);
    });
    m_ruler->onLoopZoneCleared = [this]() {
        if (m_loopZone) applyLoopZone(std::nullopt, true);
    };

    m_minimap->onViewChanged = [this](double start, double duration) {
        m_scrollX = static_cast<float>(start * m_pixelsPerBeat);
        m_targetScrollX = m_scrollX;
        
        // ZOOM LOGIC: 
        // duration * ppb = visibleWidth
        // ppb = visibleWidth / duration
        float visibleW = m_grid->getWidth();
        if (duration > 0.001) {
             m_pixelsPerBeat = visibleW / static_cast<float>(duration);
        }
        
        syncChildren();
    };
    
    m_vScroll->setOnScroll([this](double val) {
        float totalH = 128 * m_keyHeight;
        float visibleH = m_grid->getHeight();
        float maxScroll = std::max(0.0f, totalH - visibleH);
        m_targetScrollY = safeClampRange(static_cast<float>(val), 0.0f, maxScroll);
    });

    // Horizontal scrollbar drives the target scroll across the scrollable
    // domain (16-bar empty floor, growing with content — Track Manager parity).
    m_hScroll->setOnScroll([this](double val) {
        const float maxScrollX = horizontalScrollMax();
        m_targetScrollX = safeClampRange(static_cast<float>(val), 0.0f, maxScrollX);
    });
    
    addChild(m_keys);
    addChild(m_ruler);
    addChild(m_grid);
    addChild(m_notes);
    addChild(m_controls);
    addChild(m_inspector);
    addChild(m_minimap);
    addChild(m_vScroll);
    addChild(m_hScroll);
    addChild(m_toolbar); // Top (Render Last)
}

void PianoRollView::onRender(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const auto bounds = getBounds();
    renderer.fillRect(bounds, theme.getColor("backgroundPrimary"));

    const float toolbarH = m_toolbarHosted ? 0.0f : kToolbarHeight;
    const float minimapH = m_showLocalMinimap ? kMinimapHeight : 0.0f;
    const float minimapGap = m_showLocalMinimap ? kMinimapGap : 0.0f;
    const float rulerH = kRulerHeight;
    const NUIRect pitchHeader(bounds.x,
                              bounds.y + toolbarH,
                              m_keyLaneWidth,
                              minimapH + minimapGap + rulerH);
    // FD-63: the corner over the keys is plain bed, ruled off from the ruler and the keys; the overview's rule runs
    // across it too, so the overview reads as one band of the frame.
    if (m_showLocalMinimap)
        renderer.drawLine(NUIPoint(pitchHeader.x, pitchHeader.y + minimapH - 0.5f),
                          NUIPoint(pitchHeader.right(), pitchHeader.y + minimapH - 0.5f), 1.0f, theme.getColor("border"));
    renderer.drawLine(NUIPoint(pitchHeader.right() - 0.5f, pitchHeader.y), NUIPoint(pitchHeader.right() - 0.5f, pitchHeader.bottom()),
                      1.0f, theme.getColor("border"));
    renderer.drawLine(NUIPoint(pitchHeader.x, pitchHeader.bottom() - 0.5f), NUIPoint(pitchHeader.right(), pitchHeader.bottom() - 0.5f),
                      1.0f, theme.getColor("border"));

    renderer.setClipRect(bounds);
    NUIComponent::onRender(renderer);
    renderer.clearClipRect();
    renderStatusLine(renderer);

    // Draw playhead
    if (m_grid && m_ruler) {
        const auto gridBounds = m_grid->getBounds();
        const auto rulerBounds = m_ruler->getBounds();
        const auto accent = theme.getColor("accentPrimary");
        const double visibleStartBeat = getViewStartBeat();
        const double visibleEndBeat = visibleStartBeat + getViewDurationBeats();
        const float playheadX = snapVerticalLineX(
            beatToScreenX(m_playheadBeat, m_pixelsPerBeat, m_scrollX, gridBounds.x));

        if (m_playheadBeat >= visibleStartBeat && m_playheadBeat <= visibleEndBeat &&
            playheadX >= gridBounds.x && playheadX <= gridBounds.right()) {
            const float playheadStartY = rulerBounds.bottom();
            const float playheadEndY = gridBounds.bottom();
            (void)accent;
            // FD-63 board: a 1 px line in the text colour, over the grid only.
            renderer.fillRect(NUIRect(playheadX, playheadStartY, 1.0f, playheadEndY - playheadStartY), theme.getColor("textPrimary"));
        }
    }

    if (m_controls) {
        const auto controlBounds = m_controls->getBounds();
        const float gripWidth = 34.0f;
        const NUIRect splitterGrip(controlBounds.x + (controlBounds.width - gripWidth) * 0.5f,
                                   controlBounds.y - 2.0f,
                                   gripWidth,
                                   4.0f);
        renderer.fillRoundedRect(splitterGrip,
                                 2.0f,
                                 theme.getColor("accentPrimary").withAlpha(
                                     m_isResizingPanel ? 0.92f : (m_splitterHovered ? 0.52f : 0.0f))); // shown on hover only
    }

    if (m_toolbar) {
        if (auto menu = m_toolbar->getActiveContextMenu(); menu && menu->isVisible()) {
            menu->onRender(renderer);
        }
    }

    if (m_showShortcutHelp) {
        renderShortcutHelp(renderer);
    }
}

void PianoRollView::renderShortcutHelp(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const auto bounds = getBounds();
    const float toolbarH = 50.0f;
    const NUIRect area(bounds.x, bounds.y + toolbarH, bounds.width, bounds.height - toolbarH);

    // Dim the editors underneath so the sheet reads as modal.
    renderer.fillRect(area, theme.getColor("backgroundPrimary").withAlpha(0.62f));

    struct Entry { const char* keys; const char* action; };
    static constexpr Entry kMouse[] = {
        {"Drag", "draw a note, keep dragging for length"},
        {"Shift+Drag", "paint a run of notes"},
        {"Ctrl+Drag", "marquee select (any tool)"},
        {"Alt+Drag", "clone the selection"},
        {"Alt mid-drag", "bypass snap for fine moves"},
        {"Alt+Wheel", "adjust velocity under cursor"},
        {"Right-Click", "erase"},
        {"Double-Click", "add note / note properties"},
        {"Lane sidebar", "show velocity, chance, ratchet or pan"},
    };
    static constexpr Entry kKeys[] = {
        {"Q", "quantize note starts"},
        {"Ctrl+G / Ctrl+Shift+G", "glue runs / subdivide by snap"},
        {"Ctrl+L", "connect notes (legato)"},
        {"Ctrl+Z / Y", "undo / redo"},
        {"Ctrl+C / V", "copy / paste"},
        {"Ctrl+D", "duplicate after the selection (snap grid) / ruler range"},
        {"Ctrl+A", "select all"},
        {"Arrows", "nudge / transpose"},
        {"Shift+Left/Right", "resize by grid"},
        {"Shift+Up/Down", "octave jump"},
        {"Delete", "remove selection"},
    };
    constexpr size_t kMouseCount = sizeof(kMouse) / sizeof(kMouse[0]);
    constexpr size_t kKeysCount = sizeof(kKeys) / sizeof(kKeys[0]);
    constexpr size_t kRows = kMouseCount > kKeysCount ? kMouseCount : kKeysCount;

    const float rowH = 19.0f;
    const float padX = 24.0f;
    const float padY = 18.0f;
    const float titleH = 18.0f;
    const float sectionH = 16.0f;
    const float footerH = 14.0f;
    const float colGap = 28.0f;
    const float keyColW = 118.0f;
    const float panelW = std::min(660.0f, area.width - 32.0f);
    const float panelH = padY + titleH + 12.0f + sectionH + kRows * rowH + 14.0f + footerH + padY;
    const NUIRect panel(area.x + (area.width - panelW) * 0.5f,
                        area.y + std::max(8.0f, (area.height - panelH) * 0.5f),
                        panelW,
                        panelH);

    renderer.drawShadow(panel, 0.0f, 6.0f, 26.0f, NUIColor(0.0f, 0.0f, 0.0f, 0.45f));
    renderer.fillRoundedRect(panel, 10.0f, theme.getColor("backgroundSecondary").withAlpha(0.98f));
    renderer.strokeRoundedRect(panel, 10.0f, 1.0f, theme.getColor("border").withAlpha(0.7f));

    const auto accent = theme.getColor("accentPrimary");
    const auto keyColor = theme.getColor("textPrimary").withAlpha(0.95f);
    const auto actionColor = theme.getColor("textSecondary").withAlpha(0.92f);

    float y = panel.y + padY;
    const char* title = "PIANO ROLL SHORTCUTS";
    const auto titleSize = renderer.measureText(title, 11.0f);
    renderer.drawText(title,
                      NUIPoint(panel.x + (panel.width - titleSize.width) * 0.5f, y),
                      11.0f,
                      accent.withAlpha(0.95f));
    y += titleH + 12.0f;

    const float colW = (panelW - padX * 2.0f - colGap) * 0.5f;
    const float leftX = panel.x + padX;
    const float rightX = leftX + colW + colGap;

    renderer.drawText("MOUSE", NUIPoint(leftX, y), 9.0f, actionColor.withAlpha(0.6f));
    renderer.drawText("KEYS", NUIPoint(rightX, y), 9.0f, actionColor.withAlpha(0.6f));
    y += sectionH;

    for (size_t i = 0; i < kRows; ++i) {
        const float rowY = y + i * rowH;
        if (i < kMouseCount) {
            renderer.drawText(kMouse[i].keys, NUIPoint(leftX, rowY), 10.5f, keyColor);
            renderer.drawText(kMouse[i].action, NUIPoint(leftX + keyColW, rowY), 10.5f, actionColor);
        }
        if (i < kKeysCount) {
            renderer.drawText(kKeys[i].keys, NUIPoint(rightX, rowY), 10.5f, keyColor);
            renderer.drawText(kKeys[i].action, NUIPoint(rightX + keyColW, rowY), 10.5f, actionColor);
        }
    }
    y += kRows * rowH + 14.0f;

    const char* footer = "Chord & Strum live in the toolbar menu. F1 toggles this sheet; any key or click closes it.";
    const auto footerSize = renderer.measureText(footer, 9.5f);
    renderer.drawText(footer,
                      NUIPoint(panel.x + (panel.width - footerSize.width) * 0.5f, y),
                      9.5f,
                      actionColor.withAlpha(0.7f));
}

void PianoRollView::onResize(int width, int height) {
    NUIComponent::onResize(width, height);
    layoutChildren();
}

void PianoRollView::onUpdate(double deltaTime) {
    NUIComponent::onUpdate(deltaTime);

    const float totalH = 128 * m_keyHeight;
    const float visibleH = m_grid ? m_grid->getHeight() : 0.0f;
    const float maxScrollY = std::max(0.0f, totalH - visibleH);
    m_targetScrollY = safeClampRange(m_targetScrollY, 0.0f, maxScrollY);
    m_targetScrollX = safeClampRange(m_targetScrollX, 0.0f, horizontalScrollMax());

    bool changed = false;
    const float ease = 1.0f - std::exp(-static_cast<float>(deltaTime) * 18.0f);

    const float dx = m_targetScrollX - m_scrollX;
    if (std::abs(dx) > 0.1f) {
        m_scrollX += dx * ease;
        changed = true;
    } else if (std::abs(dx) > 0.0f) {
        m_scrollX = m_targetScrollX;
        changed = true;
    }

    const float dy = m_targetScrollY - m_scrollY;
    if (std::abs(dy) > 0.1f) {
        m_scrollY += dy * ease;
        changed = true;
    } else if (std::abs(dy) > 0.0f) {
        m_scrollY = m_targetScrollY;
        changed = true;
    }

    if (changed) {
        updateScrollbars();
        syncChildren();
    }
}

NUIRect PianoRollView::getRulerBounds() const {
    return m_ruler ? m_ruler->getBounds() : NUIRect{};
}

NUIRect PianoRollView::getMinimapBounds() const {
    return m_minimap ? m_minimap->getBounds() : NUIRect{};
}

NUIRect PianoRollView::getGridBounds() const {
    return m_grid ? m_grid->getBounds() : NUIRect();
}

std::shared_ptr<PianoRollToolbar> PianoRollView::detachToolbarForHost() {
    if (m_toolbar && !m_toolbarHosted) {
        removeChild(m_toolbar);
        m_toolbarHosted = true;
        layoutChildren();
    }
    return m_toolbar;
}

void PianoRollView::layoutChildren() {
    auto b = getBounds();
    // FD-63: the inspector takes a column at the right, from the top to the status line; the rest lays out beside it.
    // A narrow view (under 900 px) drops it rather than squeeze the grid.
    if (m_inspector) {
        const bool room = b.width >= 900.0f;
        m_inspector->setVisible(room);
        const float top = m_toolbarHosted ? 0.0f : kToolbarHeight;
        if (room) {
            m_inspector->setBounds(NUIRect(b.right() - PianoRollInspector::kWidth, b.y + top, PianoRollInspector::kWidth,
                                           std::max(0.0f, b.height - top - kStatusHeight)));
            b.width -= PianoRollInspector::kWidth;
        }
    }
    const float sbSize = kOverlayScrollbarThickness;
    
    // 0. Toolbar — zero once a host panel has taken it into its title bar.
    float toolbarH = m_toolbarHosted ? 0.0f : kToolbarHeight;
    if (m_toolbar && !m_toolbarHosted) m_toolbar->setBounds(NUIRect(b.x, b.y, b.width, toolbarH));
    
    // 1. Scrollbar/Minimap Section (Below Toolbar)
    float miniMapH = m_showLocalMinimap ? kMinimapHeight : 0.0f;

    // 2. Ruler Section (Below Minimap if present)
    float rulerH = kRulerHeight;

    // Gap between minimap (overview) and ruler to visually separate them
    float minimapGap = m_showLocalMinimap ? kMinimapGap : 0.0f;

    float topTotalH = toolbarH + miniMapH + minimapGap + rulerH;

    float keyW = std::max(40.0f, m_keyLaneWidth);
    const float hScrollH = kOverlayScrollbarThickness; // Horizontal scrollbar row below the grid
    float contentW = std::max(0.0f, b.width - keyW - sbSize);
    float contentH = std::max(0.0f, b.height - topTotalH - m_controlPanelHeight - hScrollH - kStatusHeight);

    // 1. Minimap (Top)
    m_minimap->setVisible(m_showLocalMinimap);
    if (m_showLocalMinimap) {
        m_minimap->setBounds(NUIRect(b.x + keyW, b.y + toolbarH, contentW, miniMapH));
    }

    // 2. Ruler (leave a small gap below minimap)
    m_ruler->setBounds(NUIRect(b.x + keyW, b.y + toolbarH + miniMapH + minimapGap, contentW, rulerH));
    
    // 3. Grid/Notes (Below Ruler)
    NUIRect contentRect(b.x + keyW, b.y + topTotalH, contentW, contentH);
    m_grid->setBounds(contentRect);
    m_notes->setBounds(contentRect);
    
    // 4. Keys (Left, spans Grid height)
    m_keys->setBounds(NUIRect(b.x, b.y + topTotalH, keyW, contentH));
    
    // 5. V-Scroll (Right, spans Grid height only)
    m_vScroll->setBounds(NUIRect(b.x + b.width - sbSize, b.y + topTotalH, sbSize, contentH));

    // 5b. H-Scroll (below the grid, spans the content width)
    if (m_hScroll) {
        m_hScroll->setBounds(NUIRect(b.x + keyW, b.y + topTotalH + contentH, contentW, hScrollH));
    }
    
    // 6. Control Panel (Bottom) - Spans Full Width (Keys + Content)
    // Ensures "Control" sidebar aligns with Keys
    m_controls->setBounds(NUIRect(b.x, b.y + topTotalH + contentH + hScrollH, b.width, m_controlPanelHeight));
    m_controls->setSidebarWidth(m_keyLaneWidth); // its beat 0 must sit under the grid's
    
    updateScrollbars();
    syncChildren();
}

float PianoRollView::horizontalScrollMax() const {
    const float visibleW = m_grid ? m_grid->getWidth() : 0.0f;
    const float totalW = static_cast<float>(std::max(0.0, m_scrollDomainEndBeats)) * m_pixelsPerBeat;
    return std::max(0.0f, totalW - visibleW);
}

void PianoRollView::updateScrollbarDomain() {
    // Track Manager parity: 16 bars when empty, dynamic padding with content.
    const double beatsPerBarD = static_cast<double>(std::max(1, m_beatsPerBar));
    const double contentEnd = std::max(0.0, m_totalDurationBeats);
    const bool hasContent = m_notes && !m_notes->getNotes().empty();

    double requiredEnd;
    if (!hasContent) {
        // Empty: fixed 16-bar floor (matches the Track Manager timeline).
        requiredEnd = beatsPerBarD * 16.0;
    } else {
        const double clipBars = contentEnd / beatsPerBarD;
        double padBars;
        if (clipBars < 16.0) {
            padBars = 8.0;
        } else if (clipBars < 64.0) {
            padBars = clipBars * 0.25;
        } else {
            padBars = clipBars * 0.125;
        }
        requiredEnd = contentEnd + beatsPerBarD * padBars;
    }

    if (m_scrollDomainEndBeats <= 0.0 || requiredEnd > m_scrollDomainEndBeats + 1e-3) {
        m_scrollDomainEndBeats = requiredEnd;
    } else if (requiredEnd < m_scrollDomainEndBeats - 1e-3) {
        // Shrink is allowed once content exists (Track Manager parity); the
        // 16-bar floor applies to the empty case only.
        m_scrollDomainEndBeats = std::max(requiredEnd, 0.0);
    }
}

void PianoRollView::updateScrollbars() {
    updateScrollbarDomain();

    float totalBeats = static_cast<float>(m_scrollDomainEndBeats);
    float visibleW = m_grid->getWidth();
    double viewDur = visibleW / m_pixelsPerBeat;
    double start = m_scrollX / m_pixelsPerBeat;
    
    if (m_minimap && m_showLocalMinimap) {
        m_minimap->setTotalDuration(totalBeats);
        m_minimap->setView(start, viewDur);
    }

    if (m_hScroll) {
        const double totalW = static_cast<double>(m_scrollDomainEndBeats) * m_pixelsPerBeat;
        m_hScroll->setRangeLimit(0.0, totalW);
        m_hScroll->setCurrentRange(m_scrollX, visibleW);
    }

    // Vertical
    float totalH = 128 * m_keyHeight;
    float visibleH = m_grid->getHeight();
    
    m_vScroll->setRangeLimit(0.0, totalH);
    m_vScroll->setCurrentRange(m_scrollY, visibleH);
}

void PianoRollView::syncChildren() {
    if (!m_keys) return;
    if (m_toolbar) m_toolbar->setZoomForSmartSnap(m_pixelsPerBeat, m_beatsPerBar); // FD-66: the zoom picks the snap
    
    float x = m_scrollX;
    float y = m_scrollY; 
    
    m_keys->setScrollOffsetY(y);
    m_keys->setKeyHeight(m_keyHeight);
    
    m_ruler->setScrollX(x);
    m_ruler->setPixelsPerBeat(m_pixelsPerBeat);
    m_ruler->setPlayheadBeat(m_playheadBeat);

    m_grid->setPixelsPerBeat(m_pixelsPerBeat);
    m_grid->setKeyHeight(m_keyHeight);
    m_grid->setScrollOffsetX(x);
    m_grid->setScrollOffsetY(y);
    m_grid->setPlayheadBeat(m_playheadBeat);
    m_grid->setTotalDurationBeats(m_totalDurationBeats);
    
    m_notes->setPixelsPerBeat(m_pixelsPerBeat);
    m_notes->setKeyHeight(m_keyHeight);
    m_notes->setScrollOffsetX(m_scrollX);
    m_notes->setScrollOffsetY(m_scrollY);
    m_notes->setPlayheadBeat(m_playheadBeat);
    m_notes->setTotalDurationBeats(m_totalDurationBeats);
    m_notes->setPlaying(m_isPlayingCallback && m_isPlayingCallback());

    if (m_controls) {
        m_controls->setPixelsPerBeat(m_pixelsPerBeat);
        m_controls->setScrollX(m_scrollX);
    }

    if (m_minimap && m_showLocalMinimap) {
        m_minimap->setPlayheadBeat(m_playheadBeat);
    }
    
}

bool PianoRollView::onMouseEvent(const NUIMouseEvent& event) {
    // A release always reaches the children, wherever the button comes up. A drag that began
    // inside (the minimap bar, a note, the velocity lane) must end even when released outside
    // the editor: dropping it left the minimap "still clicked", scrolling the view on every
    // later move (SPEC 3 §2.4). Every child's release path checks its own drag state, so a
    // release that is not theirs does nothing.
    if (!getBounds().contains(event.position) && !m_isResizingPanel && !event.released) return false;

    // The shortcut sheet captures the pointer: any click or scroll dismisses it
    // instead of reaching the editors underneath.
    if (m_showShortcutHelp) {
        if (event.pressed || event.wheelDelta != 0.0f) {
            m_showShortcutHelp = false;
            repaint();
        }
        return true;
    }

    if (m_toolbar) {
        if (auto menu = m_toolbar->getActiveContextMenu(); menu && menu->isVisible()) {
            if (menu->onMouseEvent(event)) return true;

            if (event.pressed && event.button == NUIMouseButton::Left) {
                const auto menuBounds = menu->getBounds();
                const auto toolbarBounds = m_toolbar->getBounds();
                if (!menuBounds.contains(event.position) &&
                    !toolbarBounds.contains(event.position)) {
                    m_toolbar->dismissActiveContextMenu();
                    return true;
                }
            }
        }
    }

    auto b = getBounds();
    float splitterY = b.y + b.height - kStatusHeight - m_controlPanelHeight; // the lane sits on the status line
    float splitterZone = 7.0f;
    const bool splitterHovered = std::abs(event.position.y - splitterY) < splitterZone;
    if (m_splitterHovered != splitterHovered) {
        m_splitterHovered = splitterHovered;
        repaint();
    }
    
    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (std::abs(event.position.y - splitterY) < splitterZone) {
            m_isResizingPanel = true;
            m_dragStartPos = event.position;
            m_dragStartPanelHeight = m_controlPanelHeight;
            return true;
        }
    }
    else if (m_isResizingPanel && !event.released) {
        float dy = event.position.y - m_dragStartPos.y;
        // Dragging UP increases height
        float newH = m_dragStartPanelHeight - dy;
        const float maxPanelHeight = std::max(20.0f, b.height * 0.5f);
        m_controlPanelHeight = std::min(std::max(newH, 20.0f), maxPanelHeight);
        layoutChildren();
        return true;
    }
    else if (event.released) {
        if (m_isResizingPanel) {
            m_isResizingPanel = false;
            return true;
        }
    }

    // The ruler highlight clears two ways, so it is never a mystery: a left press on the grid OUTSIDE it, or a right press
    // on the grid (a right-click on the ruler without dragging does too: PianoRollRuler). A press inside it leaves it be,
    // and whatever the press does anyway (select, add or erase a note) still happens. Esc clears it as well.
    if (m_loopZone && event.pressed && m_grid && m_pixelsPerBeat > 0.0f) {
        const NUIRect g = m_grid->getBounds();
        if (g.contains(event.position)) {
            const double beat = static_cast<double>(event.position.x - g.x + m_scrollX) / static_cast<double>(m_pixelsPerBeat);
            const bool outside = beat < m_loopZone->first - 1e-9 || beat >= m_loopZone->second - 1e-9;
            if (event.button == NUIMouseButton::Right || (event.button == NUIMouseButton::Left && outside)) {
                applyLoopZone(std::nullopt, true);
            }
        }
    }

    // 1. Give children priority (Ruler, Minimap, Grid, Notes)
    if (NUIComponent::onMouseEvent(event)) return true;

    // 2. View-level fallback for Grid Scrolling (if Grid didn't handle it)
    if (event.wheelDelta != 0.0f) {
        bool shift = (event.modifiers & NUIModifiers::Shift);
        bool ctrl = (event.modifiers & NUIModifiers::Ctrl);
        
        if (ctrl) {
            // Zoom (Fallback) — same anchored, multiplicative semantics as the ruler.
            // The ruler passes grid-local X (its bounds start after the key lane);
            // mirror that basis or the beat under the cursor drifts with the lane
            // width. Bounds are window-absolute, so grid-local X is the event X
            // minus the grid's own origin — subtracting getBounds() as well would
            // double-count the view position and desync the anchor whenever the
            // piano-roll panel sits away from the window origin.
            const float zoomDelta = std::clamp(event.wheelDelta, -4.0f, 4.0f);
            applyZoom(std::pow(1.15f, zoomDelta), event.position.x - m_grid->getBounds().x);
        } else if (shift) {
            // H-Scroll
            m_targetScrollX = std::max(0.0f, m_targetScrollX - event.wheelDelta * 40.0f);
        } else {
            // V-Scroll
            float totalH = 128 * m_keyHeight;
            float visibleH = m_grid->getHeight();
            float maxScroll = std::max(0.0f, totalH - visibleH);
            
            float newY = m_targetScrollY - event.wheelDelta * 30.0f;
            m_targetScrollY = safeClampRange(newY, 0.0f, maxScroll);
        }
        
        if (ctrl) {
            updateScrollbars(); 
            syncChildren();
        }
        return true;
    }
    
    return false;
}

bool PianoRollView::onKeyEvent(const NUIKeyEvent& event) {
    if (event.pressed && event.keyCode == NUIKeyCode::F1) {
        m_showShortcutHelp = !m_showShortcutHelp;
        repaint();
        return true;
    }
    if (m_showShortcutHelp && event.pressed) {
        // While the sheet is up, any key dismisses it rather than editing the
        // notes it covers.
        m_showShortcutHelp = false;
        repaint();
        return true;
    }
    // Esc is the exit: it clears the loop zone, and the note layer still gets it (deselect).
    if (event.pressed && event.keyCode == NUIKeyCode::Escape && m_loopZone) {
        applyLoopZone(std::nullopt, true);
        m_notes->onKeyEvent(event);
        return true;
    }
    // Duplicate with a ruler highlight copies the highlighted block and moves the highlight onto the copy (FL).
    if (event.pressed && (event.modifiers & NUIModifiers::Ctrl) && event.keyCode == NUIKeyCode::D && m_loopZone) {
        if (const auto next = m_notes->duplicateLikeFl(m_loopZone)) applyLoopZone(*next, true);
        return true;
    }
    if (m_notes->onKeyEvent(event)) return true;
    return NUIComponent::onKeyEvent(event);
}

void PianoRollView::setLoopZone(std::optional<std::pair<double, double>> zone) { applyLoopZone(zone, false); }

void PianoRollView::applyLoopZone(std::optional<std::pair<double, double>> zone, bool notify) {
    if (zone && zone->second <= zone->first + 1e-6) {
        zone.reset();
    }
    m_loopZone = zone;
    const bool active = m_loopZone.has_value();
    const double start = active ? m_loopZone->first : 0.0;
    const double end = active ? m_loopZone->second : 0.0;
    if (m_ruler) m_ruler->setLoopZone(active, start, end);
    if (m_grid) m_grid->setLoopZone(active, start, end);
    if (notify && m_onLoopZoneChanged) {
        m_onLoopZoneChanged(m_loopZone);
    }
    repaint();
}

void PianoRollView::setNotes(const std::vector<MidiNote>& notes) {
    m_notes->setNotes(notes);
    if (m_minimap) {
        m_minimap->setNotes(notes);
    }
    // Content changed → the scrollable domain floor/growth may change.
    updateScrollbars();
}

void PianoRollView::setGhostPatterns(const std::vector<PianoRollNoteLayer::GhostPattern>& ghosts) {
    m_notes->setGhostPatterns(ghosts);
}

void PianoRollView::setNoteColor(const NUIColor& color) {
    m_notes->setNoteColor(color);
    if (m_minimap) m_minimap->setNoteColor(color);
    m_toolbar->setUnitColor(color); // the chip beside the unit name
}

const std::vector<MidiNote>& PianoRollView::getNotes() const {
    return m_notes->getNotes();
}

void PianoRollView::setOnNotesChanged(std::function<void(const std::vector<MidiNote>&)> cb) {
    m_notes->setOnNotesChanged([this, cb = std::move(cb)](const std::vector<MidiNote>& notes) {
        if (m_minimap) {
            m_minimap->setNotes(notes);
        }
        if (cb) cb(notes);
    });
}

void PianoRollView::setIsPlayingCallback(std::function<bool()> cb) {
    m_isPlayingCallback = std::move(cb);
}

void PianoRollView::setOnPreviewNote(std::function<void(int pitch, int velocity)> cb) {
    if (m_keys) {
        m_keys->setOnPreviewNote(cb);
        m_keys->setIsPlayingFromParent(m_isPlayingCallback);
    }
    if (m_notes) {
        // The note layer auditions pitches while notes are placed and dragged,
        // through the same synth path the key lane uses.
        m_notes->setOnPreviewNote(std::move(cb));
        m_notes->setIsPlayingCallback(m_isPlayingCallback);
    }
}

void PianoRollView::setDefaultUnitId(uint64_t unitId) {
    m_notes->setDefaultUnitId(unitId);
}

void PianoRollView::setPixelsPerBeat(float ppb) {
    if (!std::isfinite(ppb) || ppb <= 0.0f) {
        return;
    }
    m_pixelsPerBeat = ppb;
    updateScrollbars();
    syncChildren();
}

std::string chordToneRole(int pitch, const PianoRollChord& chord) {
    if (chord.root < 0 || pitch < 0 || pitch > 127) return {};
    const int pc = pitch % 12;
    if (!(chord.toneMask & (1u << pc))) return {};
    switch ((pc - chord.root + 12) % 12) {
    case 0: return "root";
    case 2: return "9th";
    case 3: case 4: return "3rd";
    case 5: return "4th";
    case 6: return "\xE2\x99\xAD" "5th";
    case 7: return "5th";
    case 8: return "\xE2\x99\xAF" "5th";
    case 9: return "6th";
    case 10: case 11: return "7th";
    default: return {};
    }
}

void PianoRollView::setHarmony(std::vector<PianoRollChord> chords, const PianoRollKeyGuess& key) {
    m_chords = std::move(chords);
    if (m_ruler) m_ruler->setChords(&m_chords);
    if (m_grid) m_grid->setChords(&m_chords);
    if (m_toolbar) m_toolbar->setDetectedKey(key);
    repaint();
}

void PianoRollView::setSnap(SnapGrid snap) {
    if (m_toolbar) m_toolbar->chooseSnap(snap);
}
void PianoRollView::setSmartSnap(bool on) {
    if (m_toolbar) m_toolbar->setSmartSnap(on);
}
SnapGrid PianoRollView::getSnap() const { return m_toolbar ? m_toolbar->getSnap() : SnapGrid::Beat; }
bool PianoRollView::isSmartSnap() const { return m_toolbar && m_toolbar->isSmartSnap(); }

void PianoRollView::setBeatsPerBar(int bpb) {
    m_beatsPerBar = std::max(1, bpb);
    if (m_grid) m_grid->setBeatsPerBar(bpb);
    if (m_ruler) m_ruler->setBeatsPerBar(bpb);
    if (m_inspector) m_inspector->setBeatsPerBar(bpb);
    if (m_notes) m_notes->setBeatsPerBar(bpb);
    if (m_controls) m_controls->setBeatsPerBar(bpb);
    // Full refresh so the h-scrollbar and minimap pick up the new domain
    // immediately (updateScrollbarDomain alone only mutates the value).
    updateScrollbars();
}

void PianoRollView::setTool(GlobalTool tool) {
    if (m_notes) m_notes->setTool(tool);
}

void PianoRollView::setScale(int root, ScaleType type) {
    const bool snapToScale = m_notes && m_notes->getSnapToScale();
    if (m_toolbar) m_toolbar->setHarmonyContext(root, type, snapToScale);
}

void PianoRollView::setSnapToScale(bool enabled) {
    if (!m_toolbar) return;
    m_toolbar->setHarmonyContext(m_toolbar->getRootKey(), m_toolbar->getScaleType(), enabled);
}

void PianoRollView::applyHarmonyContextEdit(int root, ScaleType type, bool snapToScale) {
    if (m_toolbar) m_toolbar->applyHarmonyContextEdit(root, type, snapToScale);
}

int PianoRollView::getRootKey() const {
    return m_toolbar ? m_toolbar->getRootKey() : 0;
}

ScaleType PianoRollView::getScaleType() const {
    return m_toolbar ? m_toolbar->getScaleType() : ScaleType::Chromatic;
}

bool PianoRollView::getSnapToScale() const {
    return m_toolbar && m_toolbar->getSnapToScale();
}

void PianoRollView::setOnHarmonyContextChanged(std::function<void(int, ScaleType, bool)> cb) {
    if (m_toolbar) m_toolbar->setOnHarmonyContextChanged(std::move(cb));
}

void PianoRollView::setPlatformBridge(NUIPlatformBridge* bridge) {
    if (m_notes) m_notes->setPlatformBridge(bridge);
    if (m_minimap) m_minimap->setPlatformBridge(bridge);
}

void PianoRollView::setPatternName(const std::string& name) {
    if (m_toolbar) m_toolbar->setPatternName(name);
}

void PianoRollView::setPatternChoices(const std::vector<PianoRollToolbar::PatternChoice>& choices, int selectedValue) {
    if (m_toolbar) {
        m_toolbar->setPatternChoices(choices, selectedValue);
    }
}

void PianoRollView::setUnitChoices(const std::vector<PianoRollToolbar::PatternChoice>& choices, int selectedValue) {
    if (m_toolbar) {
        m_toolbar->setUnitChoices(choices, selectedValue);
    }
}

void PianoRollView::setPatternLengthBeats(double beats) {
    m_patternLengthBeats = std::max(8.0, beats);
    if (m_toolbar) {
        m_toolbar->setPatternLengthBeats(m_patternLengthBeats);
    }
    if (std::abs(m_totalDurationBeats - m_patternLengthBeats) > 0.001) {
        setTotalDurationBeats(m_patternLengthBeats);
    } else {
        repaint();
    }
}

void PianoRollView::applyZoom(float factor, float anchorX) {
    const float oldPPB = m_pixelsPerBeat;
    const float newPPB = std::clamp(oldPPB * factor, 10.0f, 500.0f);
    if (std::abs(newPPB - oldPPB) < 0.001f) {
        return;
    }
    m_pixelsPerBeat = newPPB;
    m_scrollX = pianoRollZoomAnchorScroll(m_scrollX, oldPPB, newPPB, anchorX);
    m_targetScrollX = m_scrollX;
    updateScrollbars();
    syncChildren();
}

// FD-63, board "A + C": the status line. Left, the playhead as bar.beat.step and the hovered key; right, the shortcuts
// that matter most. The harmony service will add what the hovered key is in the chord ("A4 = 3rd of F").
void PianoRollView::renderStatusLine(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const auto b = getBounds();
    const NUIRect strip(b.x, b.bottom() - kStatusHeight, b.width, kStatusHeight);
    renderer.fillRect(strip, theme.getColor("backgroundPrimary"));
    renderer.drawLine(NUIPoint(strip.x, strip.y + 0.5f), NUIPoint(strip.right(), strip.y + 0.5f), 1.0f, theme.getColor("border"));
    const float fs = 11.0f;
    const auto ink = theme.getColor("textMuted");
    const int bpb = std::max(1, m_beatsPerBar);
    const double beat = std::max(0.0, m_playheadBeat);
    const int bar = static_cast<int>(beat / bpb) + 1;
    const int beatInBar = static_cast<int>(std::fmod(beat, static_cast<double>(bpb))) + 1;
    const int step = static_cast<int>(std::fmod(beat, 1.0) * 4.0) + 1;
    std::string left = std::to_string(bar) + "." + std::to_string(beatInBar) + "." + std::to_string(step);
    float x = strip.x + 14.0f;
    renderer.drawText(left, NUIPoint(x, renderer.calculateTextY(strip, fs)), fs, ink);
    x += renderer.measureText(left, fs).width + 18.0f;
    if (m_statusPitch >= 0 && m_statusPitch <= 127) {
        // "A4 = 3rd of F" when the key is a tone of the chord at the playhead (board A + C).
        std::string pitch = MusicTheory::getPitchName(m_statusPitch);
        for (const auto& c : m_chords) {
            if (beat < c.startBeat || beat >= c.endBeat) continue;
            if (const std::string role = chordToneRole(m_statusPitch, c); !role.empty()) pitch += " = " + role + " of " + c.name;
            break;
        }
        renderer.drawText(pitch, NUIPoint(x, renderer.calculateTextY(strip, fs)), fs, ink);
    }
    const std::string hints = "Ctrl+D duplicate \xC2\xB7 Q quantize \xC2\xB7 Alt+Wheel velocity";
    const float hw = renderer.measureText(hints, fs).width;
    renderer.drawText(hints, NUIPoint(strip.right() - 14.0f - hw, renderer.calculateTextY(strip, fs)), fs, ink);
}

void PianoRollView::setPlayheadBeat(double beat, bool follow) {
    m_playheadBeat = std::max(0.0, beat);
    if (m_keys) {
        m_keys->setPlayingPitches(m_playbackKeysActive && m_notes ? pitchesUnderBeat(m_notes->getNotes(), m_playheadBeat)
                                                                  : std::array<bool, 128>{});
    }

    if (follow && m_grid) {
        const float visibleW = m_grid->getWidth();
        if (visibleW > 0.0f) {
            // Target-only update: the onUpdate ease animates the scroll, so
            // follow never teleports or fights user/scrub scrolling.
            m_targetScrollX = pianoRollFollowTargetScroll(m_scrollX, m_pixelsPerBeat, visibleW, m_playheadBeat);
            updateScrollbars();
        }
    }

    syncChildren();
}

bool PianoRollView::isKeyPlaying(int pitch) const { return m_keys && m_keys->isKeyPlaying(pitch); }

void PianoRollView::setTotalDurationBeats(double beats) {
    m_totalDurationBeats = std::max(8.0, beats);
    m_patternLengthBeats = m_totalDurationBeats;
    if (m_toolbar) {
        m_toolbar->setPatternLengthBeats(m_patternLengthBeats);
    }
    // A shorter pattern cuts the loop zone to its new end (or ends it, if the zone lay wholly past
    // it), exactly as drawing the zone clamps. The listener hears the change like any other (#970).
    if (m_loopZone && m_loopZone->second > m_totalDurationBeats) {
        const double end = m_totalDurationBeats;
        applyLoopZone(std::make_pair(std::min(m_loopZone->first, end), end), true);
    }
    updateScrollbars();
    syncChildren();
}

void PianoRollView::setOnAdjustPatternLength(std::function<void(int barsDelta)> cb) {
    if (m_toolbar) {
        m_toolbar->setOnAdjustPatternLength(std::move(cb));
    }
}

void PianoRollView::setOnPatternChoiceSelected(std::function<void(int patternValue)> cb) {
    if (m_toolbar) {
        m_toolbar->setOnPatternChoiceSelected(std::move(cb));
    }
}

void PianoRollView::setFollowPlayhead(bool on) {
    if (m_toolbar) {
        m_toolbar->setFollowPlayhead(on);
    }
}

bool PianoRollView::getFollowPlayhead() const {
    return m_toolbar ? m_toolbar->getFollowPlayhead() : false;
}

void PianoRollView::setOnFollowPlayheadChanged(std::function<void(bool)> cb) {
    if (m_toolbar) {
        m_toolbar->setOnFollowPlayheadChanged(std::move(cb));
    }
}

void PianoRollView::setOnCenterOnPlayhead(std::function<void()> cb) {
    if (m_toolbar) {
        m_toolbar->setOnCenterOnPlayhead(std::move(cb));
    }
}

void PianoRollView::centerOnPlayhead() {
    // Explicit center action: aim the eased scroll so the playhead sits at
    // the viewport's middle (clamped to the pattern start). The guarded
    // follow math is for CONTINUOUS tracking — it deliberately does nothing
    // inside its dead zone, which is wrong for a one-shot jump (CR review).
    if (m_grid) {
        const float visibleW = m_grid->getWidth();
        if (visibleW > 0.0f) {
            const float playheadX = static_cast<float>(m_playheadBeat * m_pixelsPerBeat);
            m_targetScrollX = std::max(0.0f, playheadX - visibleW * 0.5f);
            updateScrollbars();
        }
    }
}

void PianoRollView::setOnUnitChoiceSelected(std::function<void(int unitValue)> cb) {
    if (m_toolbar) {
        m_toolbar->setOnUnitChoiceSelected(std::move(cb));
    }
}

void PianoRollView::setOnPlayheadScrubbed(std::function<void(double beat, bool active)> cb) {
    m_onPlayheadScrubbed = std::move(cb);
}

void PianoRollView::setLocalMinimapVisible(bool visible) {
    if (m_showLocalMinimap == visible) return;
    m_showLocalMinimap = visible;
    layoutChildren();
}

void PianoRollView::frameNotesVertically() {
    // The highest note two rows under the top, so the line reads from its top; an empty pattern keeps the view.
    int highest = -1;
    if (m_notes)
        for (const auto& n : m_notes->getNotes())
            if (!n.isDeleted) highest = std::max(highest, n.pitch);
    if (highest < 0) return;
    const float totalH = 128.0f * m_keyHeight;
    const float visibleH = m_grid ? m_grid->getHeight() : 0.0f;
    const float maxScrollY = std::max(0.0f, totalH - visibleH);
    m_scrollY = safeClampScroll((127.0f - static_cast<float>(std::min(127, highest + 2))) * m_keyHeight, maxScrollY);
    m_targetScrollY = m_scrollY;
    updateScrollbars();
    syncChildren();
}

void PianoRollView::applyEdgeAutoScroll(float scrollX, float scrollY) {
    const float totalH = 128.0f * m_keyHeight;
    const float visibleH = m_grid ? m_grid->getHeight() : 0.0f;
    const float maxScrollY = std::max(0.0f, totalH - visibleH);
    const float maxScrollX = horizontalScrollMax();

    m_scrollX = safeClampScroll(scrollX, maxScrollX);
    m_scrollY = safeClampScroll(scrollY, maxScrollY);
    m_targetScrollX = m_scrollX;
    m_targetScrollY = m_scrollY;
    updateScrollbars();
    syncChildren();
}

double PianoRollView::getViewStartBeat() const {
    return static_cast<double>(m_scrollX) / m_pixelsPerBeat;
}

double PianoRollView::getViewDurationBeats() const {
    if (!m_grid) return 4.0;
    const float visibleW = m_grid->getWidth();
    if (visibleW <= 0.0f) return 4.0;
    return static_cast<double>(visibleW) / m_pixelsPerBeat;
}

void PianoRollView::setViewWindow(double startBeat, double durationBeats) {
    const double clampedDuration = std::max(0.25, durationBeats);
    if (m_grid) {
        const float visibleW = m_grid->getWidth();
        if (visibleW > 0.0f) {
            m_pixelsPerBeat = std::clamp(visibleW / static_cast<float>(clampedDuration), 10.0f, 500.0f);
        }
    }
    m_scrollX = std::max(0.0f, static_cast<float>(startBeat * m_pixelsPerBeat));
    m_targetScrollX = m_scrollX;
    updateScrollbars();
    syncChildren();
}

} // namespace AestraUI
