// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "NUIPianoRollWidgets.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "PianoRollWidgetShared.h"
#include <algorithm>
#include <cmath>

namespace AestraUI {

// =============================================================================
// PianoRollView (split from NUIPianoRollWidgets.cpp)
// =============================================================================
PianoRollView::PianoRollView()
    : m_keyLaneWidth(76.0f), m_rulerHeight(28.0f), m_pixelsPerBeat(80.0f), m_keyHeight(24.0f),
      m_scrollX(0.0f), m_targetScrollX(0.0f)
{
    // [FIX] Canonical default octave: C3 (MIDI 48).
    // scrollY = (127 - targetPitch) * keyHeight => (127 - 48) * 24 = 1896
    m_scrollY = 1896.0f;
    m_targetScrollY = 1896.0f;
    m_keys = std::make_shared<PianoRollKeyLane>();
    m_ruler = std::make_shared<PianoRollRuler>();
    m_grid = std::make_shared<PianoRollGrid>();
    m_notes = std::make_shared<PianoRollNoteLayer>();
    m_controls = std::make_shared<PianoRollControlPanel>();

    m_notes->setOnHoveredPitchChanged([this](int pitch) {
        if (m_grid) m_grid->setHoveredPitch(pitch);
        if (m_keys) m_keys->setHoveredKey(pitch);
    });
    m_keys->setOnHoveredKeyChanged([this](int pitch) {
        if (m_grid) m_grid->setHoveredPitch(pitch);
    });
    
    // Toolbar
    m_toolbar = std::make_shared<PianoRollToolbar>();
    m_toolbar->setGrid(m_grid);
    m_toolbar->setNoteLayer(m_notes);
    m_toolbar->setPatternLengthBeats(m_patternLengthBeats);
    m_toolbar->setOnShowShortcutHelp([this]() {
        m_showShortcutHelp = !m_showShortcutHelp;
        repaint();
    });

    m_controls->setNoteLayer(m_notes);
    
    m_minimap = std::make_shared<PianoRollMinimap>(); // Local Minimap
    
    m_vScroll = std::make_shared<NUIScrollbar>(NUIScrollbar::Orientation::Vertical);
    m_vScroll->setOrientation(NUIScrollbar::Orientation::Vertical);
    {
        auto& theme = NUIThemeManager::getInstance();
        m_vScroll->setArrowSize(0.0f);
        m_vScroll->setBorderWidth(0.0f);
        m_vScroll->setBorderRadius(8.0f);
        m_vScroll->setTrackColor(theme.getColor("surfaceRaised").withAlpha(0.55f));
        m_vScroll->setThumbColor(theme.getColor("textPrimary").withAlpha(0.30f));
        m_vScroll->setThumbHoverColor(theme.getColor("textPrimary").withAlpha(0.48f));
        m_vScroll->setThumbPressedColor(theme.getColor("accentPrimary").withAlpha(0.68f));
        m_vScroll->setMinimumThumbSize(0.06);
    }

    // Initial default layout config
    m_minimap->setVisible(true);
    m_vScroll->setVisible(true);
    
    // Ruler Zoom Callback
    m_ruler->onZoomRequested = [this](float delta, float mouseX) {
        applyZoom((delta > 0) ? 1.15f : 0.85f, mouseX);
    };

    m_ruler->onPlayheadScrubbed = [this](double beat, bool active) {
        const double clampedBeat = std::clamp(beat, 0.0, m_totalDurationBeats);
        setPlayheadBeat(clampedBeat, false);
        if (m_onPlayheadScrubbed) {
            m_onPlayheadScrubbed(clampedBeat, active);
        }
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
    
    addChild(m_keys);
    addChild(m_ruler);
    addChild(m_grid);
    addChild(m_notes);
    addChild(m_controls);
    addChild(m_minimap);
    addChild(m_vScroll);
    addChild(m_toolbar); // Top (Render Last)
}

void PianoRollView::onRender(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const auto bounds = getBounds();
    renderer.fillRect(bounds, theme.getColor("backgroundPrimary"));

    const float toolbarH = 50.0f;
    const float minimapH = m_showLocalMinimap ? 28.0f : 0.0f;
    const float rulerH = 28.0f;
    const NUIRect pitchHeader(bounds.x,
                              bounds.y + toolbarH,
                              m_keyLaneWidth,
                              minimapH + rulerH);
    renderer.fillRect(pitchHeader, theme.getColor("backgroundSecondary").darkened(0.025f));
    renderer.drawLine(NUIPoint(pitchHeader.right() - 0.5f, pitchHeader.y),
                      NUIPoint(pitchHeader.right() - 0.5f, pitchHeader.bottom()),
                      1.0f,
                      theme.getColor("border").withAlpha(0.54f));
    renderer.drawLine(NUIPoint(pitchHeader.x, pitchHeader.bottom() - 0.5f),
                      NUIPoint(pitchHeader.right(), pitchHeader.bottom() - 0.5f),
                      1.0f,
                      theme.getColor("border").withAlpha(0.54f));
    const auto pitchLabelSize = renderer.measureText("PITCH", 9.0f);
    renderer.drawText("PITCH",
                      NUIPoint(pitchHeader.x + (pitchHeader.width - pitchLabelSize.width) * 0.5f,
                               pitchHeader.bottom() - rulerH + (rulerH - pitchLabelSize.height) * 0.5f),
                      9.0f,
                      theme.getColor("textSecondary").withAlpha(0.62f));

    NUIComponent::onRender(renderer);

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
            const float playheadEndY = m_controls ? m_controls->getBounds().bottom() : gridBounds.bottom();
            if (m_isPlayingCallback && m_isPlayingCallback()) {
                const float glowWidth = 4.0f;
                const float lineHeight = playheadEndY - playheadStartY;
                renderer.fillRectGradient(NUIRect(playheadX - glowWidth, playheadStartY, glowWidth, lineHeight),
                                          accent.withAlpha(0.0f),
                                          accent.withAlpha(0.14f),
                                          false);
                renderer.fillRectGradient(NUIRect(playheadX, playheadStartY, glowWidth, lineHeight),
                                          accent.withAlpha(0.14f),
                                          accent.withAlpha(0.0f),
                                          false);
            }
            renderer.drawLine(NUIPoint(playheadX, playheadStartY),
                              NUIPoint(playheadX, playheadEndY),
                              1.0f,
                              accent.withAlpha(0.55f));
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
                                     m_isResizingPanel ? 0.92f : (m_splitterHovered ? 0.52f : 0.22f)));
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
        {"Lane sidebar", "switch velocity / pan lane"},
    };
    static constexpr Entry kKeys[] = {
        {"Q", "quantize note starts"},
        {"Ctrl+G / Ctrl+Shift+G", "glue runs / subdivide by snap"},
        {"Ctrl+L", "connect notes (legato)"},
        {"Ctrl+Z / Y", "undo / redo"},
        {"Ctrl+C / V / D", "copy / paste / duplicate"},
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
    m_targetScrollX = std::max(0.0f, m_targetScrollX);

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

void PianoRollView::layoutChildren() {
    auto b = getBounds();
    float sbSize = 14.0f; 
    
    // 0. Toolbar (Standardized Aestra UI Height)
    float toolbarH = 50.0f;
    if (m_toolbar) m_toolbar->setBounds(NUIRect(b.x, b.y, b.width, toolbarH));
    
    // 1. Scrollbar/Minimap Section (Below Toolbar)
    float miniMapH = m_showLocalMinimap ? 28.0f : 0.0f;

    // 2. Ruler Section (Below Minimap if present)
    float rulerH = 28.0f;

    // Gap between minimap (overview) and ruler to visually separate them
    float minimapGap = m_showLocalMinimap ? 4.0f : 0.0f;

    float topTotalH = toolbarH + miniMapH + minimapGap + rulerH;

    float keyW = std::max(40.0f, m_keyLaneWidth);
    float contentW = std::max(0.0f, b.width - keyW - sbSize);
    float contentH = std::max(0.0f, b.height - topTotalH - m_controlPanelHeight); // Subtract control panel

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
    
    // 6. Control Panel (Bottom) - Spans Full Width (Keys + Content)
    // Ensures "Control" sidebar aligns with Keys
    m_controls->setBounds(NUIRect(b.x, b.y + topTotalH + contentH, b.width, m_controlPanelHeight));
    
    updateScrollbars();
    syncChildren();
}

void PianoRollView::updateScrollbars() {
    float totalBeats = static_cast<float>(std::max(4.0, m_totalDurationBeats));
    float visibleW = m_grid->getWidth();
    double viewDur = visibleW / m_pixelsPerBeat;
    double start = m_scrollX / m_pixelsPerBeat;
    
    if (m_minimap && m_showLocalMinimap) {
        m_minimap->setTotalDuration(totalBeats);
        m_minimap->setView(start, viewDur);
    }

    // Vertical
    float totalH = 128 * m_keyHeight;
    float visibleH = m_grid->getHeight();
    
    m_vScroll->setRangeLimit(0.0, totalH);
    m_vScroll->setCurrentRange(m_scrollY, visibleH);
}

void PianoRollView::syncChildren() {
    if (!m_keys) return;
    
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
    if (!getBounds().contains(event.position) && !m_isResizingPanel) return false;

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
                const auto menuBounds = menu->getGlobalBounds();
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
    float splitterY = b.y + b.height - m_controlPanelHeight;
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

    // 1. Give children priority (Ruler, Minimap, Grid, Notes)
    if (NUIComponent::onMouseEvent(event)) return true;

    // 2. View-level fallback for Grid Scrolling (if Grid didn't handle it)
    if (event.wheelDelta != 0.0f) {
        bool shift = (event.modifiers & NUIModifiers::Shift) || (event.modifiers & NUIModifiers::CapsLock);
        bool ctrl = (event.modifiers & NUIModifiers::Ctrl);
        
        if (ctrl) {
            // Zoom (Fallback) — same anchored, multiplicative semantics as the ruler.
            // The ruler passes grid-local X (its bounds start after the key lane);
            // mirror that basis or the beat under the cursor drifts with the lane width.
            applyZoom((event.wheelDelta > 0) ? 1.15f : 0.85f,
                      event.position.x - getBounds().x - m_grid->getBounds().x);
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
    if (m_notes->onKeyEvent(event)) return true;
    return NUIComponent::onKeyEvent(event);
}

void PianoRollView::setNotes(const std::vector<MidiNote>& notes) {
    m_notes->setNotes(notes);
    if (m_minimap) {
        m_minimap->setNotes(notes);
    }
}

void PianoRollView::setGhostPatterns(const std::vector<PianoRollNoteLayer::GhostPattern>& ghosts) {
    m_notes->setGhostPatterns(ghosts);
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

void PianoRollView::setBeatsPerBar(int bpb) {
    if (m_grid) m_grid->setBeatsPerBar(bpb);
    if (m_ruler) m_ruler->setBeatsPerBar(bpb);
    if (m_notes) m_notes->setBeatsPerBar(bpb);
    if (m_controls) m_controls->setBeatsPerBar(bpb);
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

void PianoRollView::setPlayheadBeat(double beat, bool follow) {
    m_playheadBeat = std::max(0.0, beat);

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

void PianoRollView::setTotalDurationBeats(double beats) {
    m_totalDurationBeats = std::max(8.0, beats);
    m_patternLengthBeats = m_totalDurationBeats;
    if (m_toolbar) {
        m_toolbar->setPatternLengthBeats(m_patternLengthBeats);
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

void PianoRollView::applyEdgeAutoScroll(float scrollX, float scrollY) {
    const float totalH = 128.0f * m_keyHeight;
    const float visibleH = m_grid ? m_grid->getHeight() : 0.0f;
    const float maxScrollY = std::max(0.0f, totalH - visibleH);
    const float visibleW = m_grid ? m_grid->getWidth() : 0.0f;
    const double dynamicTotalBeats = std::max(std::max(4.0, m_totalDurationBeats), getViewDurationBeats() + 8.0);
    const float totalW = static_cast<float>(dynamicTotalBeats) * m_pixelsPerBeat;
    const float maxScrollX = std::max(0.0f, totalW - visibleW);

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
