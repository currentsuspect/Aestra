// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "NUIPianoRollWidgets.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "PianoRollWidgetShared.h"
#include <algorithm>
#include <cmath>
#include "NUIButton.h"
#include "NUIContextMenu.h"
#include "NUIDropdown.h"
#include "NUIIcon.h"

namespace AestraUI {

// =============================================================================
// PianoRollToolbar (split from NUIPianoRollWidgets.cpp)
// =============================================================================
PianoRollToolbar::PianoRollToolbar() {
    setupUI();
}

void PianoRollToolbar::closeActiveContextMenu() {
    if (!m_activeContextMenu) {
        return;
    }

    if (auto* parent = m_activeContextMenu->getParent()) {
        parent->removeChild(m_activeContextMenu);
    } else {
        removeChild(m_activeContextMenu);
    }

    m_activeContextMenu = nullptr;
}

void PianoRollToolbar::setupUI() {
    // 0. Menu Button (Scale, Snap, etc.)
    m_menuBtn = std::make_shared<NUIButton>("");
    m_menuBtn->setTooltip("More");
    
    const char* menuSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><circle cx="5" cy="12" r="2"/><circle cx="12" cy="12" r="2"/><circle cx="19" cy="12" r="2"/></svg>)";
    m_menuIcon = std::make_shared<NUIIcon>(menuSvg);
    
    m_menuBtn->setOnClick([this]() {
        if (m_activeContextMenu) {
            if (!m_activeContextMenu->isVisible()) {
                closeActiveContextMenu();
            } else {
                closeActiveContextMenu();
                return;
            }
        }

        auto menu = std::make_shared<NUIContextMenu>();
        m_activeContextMenu = std::static_pointer_cast<NUIComponent>(menu);
        
        // --- SNAP SUBMENU ---
        auto snapMenu = std::make_shared<NUIContextMenu>();
        auto snaps = MusicTheory::getSnapOptions();
        for (auto snap : snaps) {
            snapMenu->addItem(MusicTheory::getSnapName(snap), [this, snap]() {
                applySnap(snap);
            });
        }
        menu->addSubmenu("Snap", snapMenu);
        
        // --- ROOT KEY SUBMENU ---
        auto rootMenu = std::make_shared<NUIContextMenu>();
        auto roots = MusicTheory::getRootNames();
        for (size_t i = 0; i < roots.size(); ++i) {
            rootMenu->addRadioItem(roots[i], "piano-roll-root", m_rootKey == static_cast<int>(i), [this, i]() {
                applyHarmonyContextEdit(static_cast<int>(i), m_scaleType, m_snapToScale);
            });
        }
        menu->addSubmenu("Root Key", rootMenu);
        
        // --- SCALE SUBMENU ---
        auto scaleMenu = std::make_shared<NUIContextMenu>();
        auto scales = MusicTheory::getScales();
        for (size_t i = 0; i < scales.size(); ++i) {
            scaleMenu->addRadioItem(scales[i].name, "piano-roll-scale",
                                    m_scaleType == static_cast<ScaleType>(i), [this, i]() {
                applyHarmonyContextEdit(m_rootKey, static_cast<ScaleType>(i), m_snapToScale);
            });
        }
        menu->addSubmenu("Scale Type", scaleMenu);

        // --- SNAP TO SCALE TOGGLE ---
        menu->addCheckbox("Snap to Scale", m_snapToScale, [this](bool enabled) {
            applyHarmonyContextEdit(m_rootKey, m_scaleType, enabled);
        });

        // --- CHORD MODE TOGGLE (pencil stamps a diatonic triad) ---
        bool chordModeActive = false;
        if (auto n = notes_.lock()) {
            chordModeActive = n->getChordMode();
        }
        menu->addCheckbox("Chord (Triad)", chordModeActive, [this](bool) {
            if (auto n = notes_.lock()) {
                n->setChordMode(!n->getChordMode());
            }
        });

        // --- STRUM SUBMENU (staggers the selected chord low → high) ---
        auto strumMenu = std::make_shared<NUIContextMenu>();
        const struct { const char* name; double beats; } kStrumSpreads[] = {
            {"Tight (1/64)", 0.0625}, {"1/32", 0.125}, {"1/16", 0.25}, {"Wide (1/8)", 0.5},
        };
        for (const auto& spread : kStrumSpreads) {
            const double beats = spread.beats;
            strumMenu->addItem(spread.name, [this, beats]() {
                if (auto n = notes_.lock()) n->strumSelectedNotes(beats);
            });
        }
        menu->addSubmenu("Strum Selection", strumMenu);

        // --- SUBDIVIDE ---
        menu->addItem("Subdivide Selection", [this]() {
            if (auto n = notes_.lock()) n->subdivideSelectedNotes();
        });

        // --- HUMANIZE ---
        menu->addItem("Humanize Velocity", [this]() {
            if (auto n = notes_.lock()) n->humanizeSelectedVelocities();
        });

        menu->addItem("Center on Playhead", [this]() {
            if (onCenterOnPlayhead_) onCenterOnPlayhead_();
        });

        // --- SHORTCUT CHEAT SHEET ---
        menu->addItem("Keyboard Shortcuts", [this]() {
            if (onShowShortcutHelp_) onShowShortcutHelp_();
        });

        auto b = m_menuBtn->getBounds();
        if (auto* parent = getParent()) {
            parent->addChild(m_activeContextMenu);
            menu->showAt(static_cast<int>(b.x),
                         static_cast<int>(b.y + b.height + 2.0f));
        } else {
            addChild(m_activeContextMenu);
            menu->showAt(static_cast<int>(b.x), static_cast<int>(b.y + b.height + 2.0f));
        }
    });

    // 1. Tool Buttons
    m_ptrBtn = std::make_shared<NUIButton>("");
    m_ptrBtn->setTooltip("Select");
    m_ptrBtn->setOnClick([this](){ setActiveTool(GlobalTool::Pointer); });

    m_pencilBtn = std::make_shared<NUIButton>("");
    m_pencilBtn->setTooltip("Draw");
    m_pencilBtn->setOnClick([this](){ setActiveTool(GlobalTool::Pencil); });

    m_eraserBtn = std::make_shared<NUIButton>("");
    m_eraserBtn->setTooltip("Erase");
    m_eraserBtn->setOnClick([this](){ setActiveTool(GlobalTool::Eraser); });

    m_patternDropdown = std::make_shared<NUIDropdown>();
    m_patternDropdown->setPlaceholderText("Pattern");
    m_patternDropdown->setMaxVisibleItems(8);
    m_patternDropdown->setItemHeight(24.0f);
    m_patternDropdown->setOnSelectionChanged([this](int, int value, const std::string&) {
        if (!m_updatingPatternDropdown && onPatternChoiceSelected_) {
            onPatternChoiceSelected_(value);
        }
    });

    m_unitDropdown = std::make_shared<NUIDropdown>();
    m_unitDropdown->setPlaceholderText("Unit");
    m_unitDropdown->setMaxVisibleItems(8);
    m_unitDropdown->setItemHeight(24.0f);
    m_unitDropdown->setOnSelectionChanged([this](int, int value, const std::string&) {
        if (!m_updatingUnitDropdown && onUnitChoiceSelected_) {
            onUnitChoiceSelected_(value);
        }
    });

    m_snapDropdown = std::make_shared<NUIDropdown>();
    m_snapDropdown->setPlaceholderText("Snap");
    m_snapDropdown->setMaxVisibleItems(8);
    m_snapDropdown->setItemHeight(24.0f);
    for (auto snap : MusicTheory::getSnapOptions()) {
        m_snapDropdown->addItem(MusicTheory::getSnapName(snap), static_cast<int>(snap));
    }
    m_snapDropdown->setSelectedByValue(static_cast<int>(m_currentSnap));
    m_snapDropdown->setOnSelectionChanged([this](int, int value, const std::string&) {
        if (!m_updatingSnapDropdown) {
            applySnap(static_cast<SnapGrid>(value));
        }
    });

    m_scaleBtn = std::make_shared<NUIButton>("");
    m_scaleBtn->setTooltip("Scale: root key, scale type and snap to scale");
    m_scaleBtn->setOnClick([this]() { openScaleMenu(); });

    m_lengthDownBtn = std::make_shared<NUIButton>("");
    m_lengthDownBtn->setTooltip("Shorten Pattern By 2 Bars");
    m_lengthDownBtn->setOnClick([this]() {
        if (onAdjustPatternLength_) onAdjustPatternLength_(-2);
    });

    m_lengthUpBtn = std::make_shared<NUIButton>("");
    m_lengthUpBtn->setTooltip("Extend Pattern By 2 Bars");
    m_lengthUpBtn->setOnClick([this]() {
        if (onAdjustPatternLength_) onAdjustPatternLength_(2);
    });
    
    // Icons
    const char* ptrSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path fill-rule="evenodd" d="M5.4 2.5v15.9l4.2-3.9 3.1 6.7 2.6-1.2-3.1-6.5 5.7-.6L5.4 2.5zm2.1 4.4 5.6 4.6-3.9.4 3 6.4-.7.3-3-6.5-1 1V6.9z"/></svg>)";
    m_ptrIcon = std::make_shared<NUIIcon>(ptrSvg);
    
    const char* penSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M3 17.25V21h3.75L17.81 9.94l-3.75-3.75L3 17.25zM20.71 7.04c.39-.39.39-1.02 0-1.41l-2.34-2.34c-.39-.39-1.02-.39-1.41 0l-1.83 1.83 3.75 3.75 1.83-1.83z"/></svg>)";
    m_pencilIcon = std::make_shared<NUIIcon>(penSvg);
    
    const char* eraserSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M15.14 3c-.51 0-1.02.2-1.41.59L2.59 14.73c-.78.78-.78 2.05 0 2.83L5.43 20.39c.39.39.9.59 1.41.59.51 0 1.02-.2 1.41-.59l10.96-10.96c.78-.78.78-2.05 0-2.83l-2.66-2.67c-.39-.39-.9-.59-1.41-.59zM9 16l-3.37-3.37L15.14 3.1 19 6.9 9 16z"/></svg>)";
    m_eraserIcon = std::make_shared<NUIIcon>(eraserSvg);

    const char* minusSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M5 11h14v2H5z"/></svg>)";
    const char* plusSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M11 5h2v14h-2zM5 11h14v2H5z"/></svg>)";
    m_lengthDownIcon = std::make_shared<NUIIcon>(minusSvg);
    m_lengthUpIcon = std::make_shared<NUIIcon>(plusSvg);

    // Follow-playhead toggle (default OFF: the playhead always moves, the
    // viewport only follows when explicitly enabled) + one-shot "center on
    // playhead" jump. 0.7.0 triage: editing must not yank the viewport.
    m_followBtn = std::make_shared<NUIButton>("");
    m_followBtn->setToggleable(true);
    m_followBtn->setTooltip("Follow playhead");
    m_followBtn->setOnClick([this]() {
        m_followPlayhead = !m_followPlayhead;
        if (onFollowPlayheadChanged_) onFollowPlayheadChanged_(m_followPlayhead);
        repaint();
    });
    const char* followSvg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="7"/><circle cx="12" cy="12" r="2.4" fill="currentColor" stroke="none"/></svg>)";
    m_followIcon = std::make_shared<NUIIcon>(followSvg);

    m_centerBtn = std::make_shared<NUIButton>("");
    m_centerBtn->setTooltip("Center on playhead");
    m_centerBtn->setOnClick([this]() {
        if (onCenterOnPlayhead_) onCenterOnPlayhead_();
    });
    const char* centerSvg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="3"/><path d="M12 2v4M12 18v4M2 12h4M18 12h4"/></svg>)";
    m_centerIcon = std::make_shared<NUIIcon>(centerSvg);

    addChild(m_menuBtn);
    addChild(m_ptrBtn);
    addChild(m_pencilBtn);
    addChild(m_eraserBtn);
    addChild(m_patternDropdown);
    addChild(m_unitDropdown);
    addChild(m_snapDropdown);
    addChild(m_scaleBtn);
    addChild(m_lengthDownBtn);
    addChild(m_lengthUpBtn);
    addChild(m_followBtn);
    addChild(m_centerBtn);
}

void PianoRollToolbar::openScaleMenu() {
    if (m_activeContextMenu) {
        const bool wasOpen = m_activeContextMenu->isVisible();
        closeActiveContextMenu();
        if (wasOpen) return;
    }
    auto menu = std::make_shared<NUIContextMenu>();
    m_activeContextMenu = std::static_pointer_cast<NUIComponent>(menu);
    auto rootMenu = std::make_shared<NUIContextMenu>();
    const auto roots = MusicTheory::getRootNames();
    for (size_t i = 0; i < roots.size(); ++i) {
        rootMenu->addRadioItem(roots[i], "piano-roll-root", m_rootKey == static_cast<int>(i), [this, i]() {
            applyHarmonyContextEdit(static_cast<int>(i), m_scaleType, m_snapToScale);
        });
    }
    menu->addSubmenu("Root Key", rootMenu);
    auto scaleMenu = std::make_shared<NUIContextMenu>();
    const auto scales = MusicTheory::getScales();
    for (size_t i = 0; i < scales.size(); ++i) {
        scaleMenu->addRadioItem(scales[i].name, "piano-roll-scale", m_scaleType == static_cast<ScaleType>(i), [this, i]() {
            applyHarmonyContextEdit(m_rootKey, static_cast<ScaleType>(i), m_snapToScale);
        });
    }
    menu->addSubmenu("Scale Type", scaleMenu);
    menu->addCheckbox("Snap to Scale", m_snapToScale, [this](bool enabled) { applyHarmonyContextEdit(m_rootKey, m_scaleType, enabled); });
    const auto at = m_scaleBtn->getBounds();
    if (auto* parent = getParent()) parent->addChild(m_activeContextMenu);
    else addChild(m_activeContextMenu);
    menu->showAt(static_cast<int>(at.x), static_cast<int>(at.y + at.height + 2.0f));
}

void PianoRollToolbar::applySnap(SnapGrid snap) {
    m_currentSnap = snap;
    if (auto g = grid_.lock()) g->setSnap(snap);
    if (auto n = notes_.lock()) n->setSnap(snap);
    if (m_snapDropdown) {
        m_updatingSnapDropdown = true;
        m_snapDropdown->setSelectedByValue(static_cast<int>(snap));
        m_updatingSnapDropdown = false;
    }
    repaint();
}

void PianoRollToolbar::setPatternName(const std::string& name) {
    m_patternName = name;
    repaint();
}

void PianoRollToolbar::setPatternLengthBeats(double beats) {
    m_patternLengthBeats = std::max(8.0, beats);
    repaint();
}

void PianoRollToolbar::setPatternChoices(const std::vector<PatternChoice>& choices, int selectedValue) {
    if (!m_patternDropdown) {
        return;
    }
    if (choices == m_appliedPatternChoices && selectedValue == m_appliedPatternSelection) {
        return;
    }
    m_appliedPatternChoices = choices;
    m_appliedPatternSelection = selectedValue;

    m_updatingPatternDropdown = true;
    m_patternDropdown->clearItems();
    for (const auto& choice : choices) {
        m_patternDropdown->addItem(choice.label, choice.value);
    }
    m_patternDropdown->setSelectedByValue(selectedValue);
    m_updatingPatternDropdown = false;
    repaint();
}

void PianoRollToolbar::setUnitChoices(const std::vector<PatternChoice>& choices, int selectedValue) {
    if (!m_unitDropdown) {
        return;
    }
    if (choices == m_appliedUnitChoices && selectedValue == m_appliedUnitSelection) {
        return;
    }
    m_appliedUnitChoices = choices;
    m_appliedUnitSelection = selectedValue;

    m_updatingUnitDropdown = true;
    m_unitDropdown->clearItems();
    for (const auto& choice : choices) {
        m_unitDropdown->addItem(choice.label, choice.value);
    }
    m_unitDropdown->setSelectedByValue(selectedValue);
    m_updatingUnitDropdown = false;
    repaint();
}

void PianoRollToolbar::setHarmonyContext(int rootKey, ScaleType scaleType, bool snapToScale) {
    const int scaleIndex = std::clamp(static_cast<int>(scaleType), 0, static_cast<int>(ScaleType::Count) - 1);
    m_rootKey = std::clamp(rootKey, 0, 11);
    m_scaleType = static_cast<ScaleType>(scaleIndex);
    m_snapToScale = snapToScale;

    if (auto grid = grid_.lock()) {
        grid->setRootKey(m_rootKey);
        grid->setScaleType(m_scaleType);
    }
    if (auto notes = notes_.lock()) {
        notes->setRootKey(m_rootKey);
        notes->setScaleType(m_scaleType);
        notes->setSnapToScale(m_snapToScale);
    }
    repaint();
}

void PianoRollToolbar::applyHarmonyContextEdit(int rootKey, ScaleType scaleType, bool snapToScale) {
    setHarmonyContext(rootKey, scaleType, snapToScale);
    if (onHarmonyContextChanged_) {
        onHarmonyContextChanged_(m_rootKey, m_scaleType, m_snapToScale);
    }
}


void PianoRollToolbar::onRender(NUIRenderer& renderer) {
    auto b = getBounds();
    auto& themeManager = NUIThemeManager::getInstance();

    const auto toolbarBg = themeManager.getColor("backgroundSecondary").darkened(0.015f);
    const auto border = themeManager.getColor("border");
    const auto text = themeManager.getColor("textPrimary");
    const auto accent = themeManager.getColor("accentPrimary");

    // Hosted in a title bar, the strip draws no surface of its own: the bar already is one.
    if (!m_hostedInTitleBar) {
        renderer.fillRect(b, toolbarBg);
        renderer.drawLine(NUIPoint(b.x, b.y + 0.5f), NUIPoint(b.right(), b.y + 0.5f), 1.0f, themeManager.getColor("divider"));
        renderer.drawLine(NUIPoint(b.x, b.bottom() - 0.5f), NUIPoint(b.right(), b.bottom() - 0.5f), 1.0f, border.withAlpha(0.52f));
    }

    // NOTE: deliberately NOT clipped to bounds: the dropdowns draw their open list below the strip.
    // Children keep input and hit-testing; the chrome and glyphs below are drawn over them.
    renderChildren(renderer);

    // FD-48 / board 6, one row: Select | Draw | Erase as a labelled segment, the pattern and the unit as chips (the unit
    // with its own colour), SNAP, LENGTH and SCALE as values, Follow as a toggle, the rest under More. A cluster appears
    // whole or not at all; the ones that matter least go first when the window is narrow (right edge = the window buttons).
    const float h = m_hostedInTitleBar ? 26.0f : 30.0f;
    const float pad = m_hostedInTitleBar ? 4.0f : 10.0f;
    const float gap = 8.0f;
    const float small = themeManager.getFontSize("xs");
    const float label = themeManager.getFontSize("s");
    float x = b.x + pad;
    const float y = b.y + (b.height - h) * 0.5f;
    const float right = b.right();
    const auto park = [](const std::shared_ptr<NUIComponent>& w) {
        if (w) w->setBounds(NUIRect(0.0f, 0.0f, 0.0f, 0.0f));
    };
    const auto outline = [&](const NUIRect& r, bool hovered) {
        if (hovered) renderer.fillRoundedRect(r, 5.0f, themeManager.getColor("buttonBgHover").withAlpha(0.9f));
        renderer.strokeRoundedRect(r, 5.0f, 1.0f, border.withAlpha(hovered ? 0.55f : 0.35f));
    };

    // The More button and Follow are anchored at the right edge: always reachable, whatever else is dropped.
    const float moreX = right - pad - h;
    const float followW = m_hostedInTitleBar ? 72.0f : 78.0f;
    const float followX = moreX - gap * 0.5f - followW;
    const float limit = followX - gap; // the middle clusters must end before this

    // ---- Tools: a labelled segment (icons alone when the row is tight) ----
    const bool wide = (limit - x) > 780.0f;
    struct Tool { std::shared_ptr<NUIButton> button; std::shared_ptr<NUIIcon> icon; GlobalTool tool; const char* name; };
    const Tool tools[] = {{m_ptrBtn, m_ptrIcon, GlobalTool::Pointer, "Select"},
                          {m_pencilBtn, m_pencilIcon, GlobalTool::Pencil, "Draw"},
                          {m_eraserBtn, m_eraserIcon, GlobalTool::Eraser, "Erase"}};
    float widths[3];
    float segmentW = 4.0f;
    for (int i = 0; i < 3; ++i) {
        widths[i] = wide ? 26.0f + renderer.measureText(tools[i].name, label).width + 10.0f : h;
        segmentW += widths[i];
    }
    if (x + segmentW <= limit) {
        renderer.strokeRoundedRect(NUIRect(x, y, segmentW, h), 6.0f, 1.0f, border.withAlpha(0.35f));
        float tx = x + 2.0f;
        for (int i = 0; i < 3; ++i) {
            const NUIRect r(tx, y + 2.0f, widths[i], h - 4.0f);
            tools[i].button->setBounds(r);
            tools[i].button->setText("");
            const bool active = activeTool_ == tools[i].tool;
            if (active) renderer.fillRoundedRect(r, 4.0f, accent.withAlpha(0.28f));
            else if (tools[i].button->isHovered()) renderer.fillRoundedRect(r, 4.0f, themeManager.getColor("buttonBgHover").withAlpha(0.9f));
            const float alpha = active ? 1.0f : (tools[i].button->isHovered() ? 0.9f : 0.62f);
            const float glyph = 14.0f;
            const float contentW = wide ? glyph + 6.0f + renderer.measureText(tools[i].name, label).width : glyph;
            const float gx = std::round(r.x + (r.width - contentW) * 0.5f);
            if (tools[i].icon) {
                tools[i].icon->setIconSize(glyph, glyph);
                tools[i].icon->setBounds(NUIRect(gx, std::round(r.y + (r.height - glyph) * 0.5f), glyph, glyph));
                tools[i].icon->setColor(text.withAlpha(alpha));
                tools[i].icon->onRender(renderer);
            }
            if (wide) renderer.drawText(tools[i].name, NUIPoint(gx + glyph + 6.0f, renderer.calculateTextY(r, label)), label, text.withAlpha(alpha));
            tx += widths[i];
        }
        x += segmentW + gap;
    } else {
        for (const auto& t : tools) park(t.button);
    }

    // ---- Pattern and unit chips ----
    if (m_patternDropdown) {
        const float w = m_hostedInTitleBar ? std::clamp(b.width * 0.15f, 110.0f, 160.0f) : std::clamp(b.width * 0.22f, 170.0f, 240.0f);
        if (x + w <= limit) {
            outline(NUIRect(x - 2.0f, y, w + 4.0f, h), false);
            m_patternDropdown->setBounds(NUIRect(x, y, w, h));
            m_patternDropdown->onRender(renderer);
            x += w + gap;
        } else {
            park(m_patternDropdown);
        }
    }
    if (m_unitDropdown) {
        const float dot = 16.0f; // room for the unit's colour
        const float w = m_hostedInTitleBar ? std::clamp(b.width * 0.12f, 100.0f, 140.0f) : std::clamp(b.width * 0.16f, 130.0f, 190.0f);
        if (x + dot + w <= limit) {
            outline(NUIRect(x - 2.0f, y, dot + w + 4.0f, h), false);
            renderer.fillRoundedRect(NUIRect(x + 5.0f, y + (h - 8.0f) * 0.5f, 8.0f, 8.0f), 4.0f, m_unitColor); // a flat chip of the unit's colour
            m_unitDropdown->setBounds(NUIRect(x + dot, y, w, h));
            m_unitDropdown->onRender(renderer);
            x += dot + w + gap;
        } else {
            park(m_unitDropdown);
        }
    }

    // ---- SNAP: the label, then the dropdown showing its value ----
    if (m_snapDropdown) {
        const float labelW = renderer.measureText("SNAP", small).width;
        const float w = 86.0f;
        if (x + labelW + 6.0f + w <= limit) {
            renderer.drawText("SNAP", NUIPoint(x, renderer.calculateTextY(NUIRect(x, y, labelW, h), small)), small, text.withAlpha(0.45f));
            m_snapDropdown->setBounds(NUIRect(x + labelW + 6.0f, y, w, h));
            m_snapDropdown->onRender(renderer);
            x += labelW + 6.0f + w + gap;
        } else {
            park(m_snapDropdown);
        }
    }

    // ---- LENGTH: [-] 8 Bars [+] ----
    {
        const int bars = std::max(1, static_cast<int>(std::lround(m_patternLengthBeats / 4.0)));
        const std::string value = std::to_string(bars) + (bars == 1 ? " Bar" : " Bars");
        const float labelW = renderer.measureText("LENGTH", small).width;
        const float valueW = std::max(52.0f, renderer.measureText(value, label).width + 16.0f);
        const float total = labelW + 6.0f + h + valueW + h;
        if (x + total <= limit) {
            renderer.drawText("LENGTH", NUIPoint(x, renderer.calculateTextY(NUIRect(x, y, labelW, h), small)), small, text.withAlpha(0.45f));
            float lx = x + labelW + 6.0f;
            const std::shared_ptr<NUIButton> steps[] = {m_lengthDownBtn, m_lengthUpBtn};
            m_lengthDownBtn->setBounds(NUIRect(lx, y, h, h));
            outline(m_lengthDownBtn->getBounds(), m_lengthDownBtn->isHovered());
            if (m_lengthDownIcon) {
                m_lengthDownIcon->setIconSize(14.0f, 14.0f);
                m_lengthDownIcon->setBounds(NUIRect(std::round(lx + (h - 14.0f) * 0.5f), std::round(y + (h - 14.0f) * 0.5f), 14.0f, 14.0f));
                m_lengthDownIcon->setColor(text.withAlpha(0.75f));
                m_lengthDownIcon->onRender(renderer);
            }
            lx += h;
            renderer.drawText(value, NUIPoint(lx + (valueW - renderer.measureText(value, label).width) * 0.5f,
                                              renderer.calculateTextY(NUIRect(lx, y, valueW, h), label)), label, text.withAlpha(0.9f));
            lx += valueW;
            m_lengthUpBtn->setBounds(NUIRect(lx, y, h, h));
            outline(m_lengthUpBtn->getBounds(), m_lengthUpBtn->isHovered());
            if (m_lengthUpIcon) {
                m_lengthUpIcon->setIconSize(14.0f, 14.0f);
                m_lengthUpIcon->setBounds(NUIRect(std::round(lx + (h - 14.0f) * 0.5f), std::round(y + (h - 14.0f) * 0.5f), 14.0f, 14.0f));
                m_lengthUpIcon->setColor(text.withAlpha(0.75f));
                m_lengthUpIcon->onRender(renderer);
            }
            (void)steps;
            x += total + gap;
        } else {
            park(m_lengthDownBtn);
            park(m_lengthUpBtn);
        }
    }

    // ---- SCALE: the value, opening the scale menu ----
    if (m_scaleBtn) {
        const std::string value = m_scaleType == ScaleType::Chromatic ? std::string("Off")
                                                                       : MusicTheory::getRootNames()[std::clamp(m_rootKey, 0, 11)] + " " +
                                                                             MusicTheory::getScales()[static_cast<size_t>(m_scaleType)].name;
        const float labelW = renderer.measureText("SCALE", small).width;
        const float valueW = renderer.measureText(value, label).width;
        const float w = labelW + 8.0f + valueW + 24.0f;
        if (x + w <= limit) {
            const NUIRect r(x, y, w, h);
            m_scaleBtn->setBounds(r);
            outline(r, m_scaleBtn->isHovered());
            renderer.drawText("SCALE", NUIPoint(r.x + 8.0f, renderer.calculateTextY(r, small)), small, text.withAlpha(0.45f));
            renderer.drawText(value, NUIPoint(r.x + 8.0f + labelW + 8.0f, renderer.calculateTextY(r, label)), label, text.withAlpha(m_scaleType == ScaleType::Chromatic ? 0.6f : 0.9f));
            const float cx = r.right() - 11.0f;
            const float cy = std::round(r.y + r.height * 0.5f);
            for (int i = 0; i < 3; ++i) // a drawn caret: the UI font has no triangle glyph
                renderer.fillRect(NUIRect(cx - 2.0f + static_cast<float>(i), cy - 1.0f + static_cast<float>(i), 5.0f - 2.0f * static_cast<float>(i), 1.0f), text.withAlpha(0.6f));
            x += w + gap;
        } else {
            park(m_scaleBtn);
        }
    }

    // ---- Follow (a toggle) and More, anchored right ----
    {
        const NUIRect r(followX, y, followW, h);
        m_followBtn->setBounds(r);
        m_followBtn->setText("");
        if (m_followPlayhead) renderer.fillRoundedRect(r, 5.0f, accent.withAlpha(0.28f));
        outline(r, m_followBtn->isHovered());
        const float glyph = 14.0f;
        const float labelW = renderer.measureText("Follow", label).width;
        const float gx = std::round(r.x + (r.width - (glyph + 6.0f + labelW)) * 0.5f);
        if (m_followIcon) {
            m_followIcon->setIconSize(glyph, glyph);
            m_followIcon->setBounds(NUIRect(gx, std::round(r.y + (r.height - glyph) * 0.5f), glyph, glyph));
            m_followIcon->setColor(text.withAlpha(m_followPlayhead ? 1.0f : 0.62f));
            m_followIcon->onRender(renderer);
        }
        renderer.drawText("Follow", NUIPoint(gx + glyph + 6.0f, renderer.calculateTextY(r, label)), label, text.withAlpha(m_followPlayhead ? 1.0f : 0.62f));
    }
    {
        const NUIRect r(moreX, y, h, h);
        m_menuBtn->setBounds(r);
        m_menuBtn->setText("");
        outline(r, m_menuBtn->isHovered());
        if (m_menuIcon) {
            m_menuIcon->setIconSize(14.0f, 14.0f);
            m_menuIcon->setBounds(NUIRect(std::round(r.x + (r.width - 14.0f) * 0.5f), std::round(r.y + (r.height - 14.0f) * 0.5f), 14.0f, 14.0f));
            m_menuIcon->setColor(text.withAlpha(m_menuBtn->isHovered() ? 0.9f : 0.65f));
            m_menuIcon->onRender(renderer);
        }
    }
    park(m_centerBtn); // "Center on playhead" lives under More now
}

void PianoRollToolbar::setGrid(std::shared_ptr<PianoRollGrid> grid) {
    grid_ = grid;
    if (grid) {
        // Sync initial default state if needed
    }
}

void PianoRollToolbar::setNoteLayer(std::shared_ptr<PianoRollNoteLayer> notes) {
    notes_ = notes;
    if (notes) {
        notes->setTool(activeTool_);
    }
}

void PianoRollToolbar::setActiveTool(GlobalTool tool) {
    activeTool_ = tool;
    if (auto n = notes_.lock()) n->setTool(tool);
    repaint();
}


bool PianoRollToolbar::onMouseEvent(const NUIMouseEvent& event) {
    // Dropdowns first — an open one's popup list extends below the toolbar strip,
    // so it must get first crack at clicks (and this override otherwise never
    // forwards events to the dropdown children at all, which is why the Pattern /
    // Unit / Snap dropdowns did nothing).
    if (m_patternDropdown && m_patternDropdown->onMouseEvent(event)) return true;
    if (m_unitDropdown && m_unitDropdown->onMouseEvent(event)) return true;
    if (m_snapDropdown && m_snapDropdown->onMouseEvent(event)) return true;

    if (m_menuBtn->onMouseEvent(event)) return true;
    if (m_scaleBtn && m_scaleBtn->onMouseEvent(event)) return true;
    if (m_followBtn && m_followBtn->onMouseEvent(event)) return true;
    if (m_ptrBtn->onMouseEvent(event)) return true;
    if (m_pencilBtn->onMouseEvent(event)) return true;
    if (m_eraserBtn->onMouseEvent(event)) return true;
    if (m_lengthDownBtn->onMouseEvent(event)) return true;
    if (m_lengthUpBtn->onMouseEvent(event)) return true;

    return false;
}

} // namespace AestraUI
