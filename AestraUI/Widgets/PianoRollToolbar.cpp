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

        // The board's header has no room for these, so they live here (FD-63).
        {
            const int bars = std::max(1, static_cast<int>(std::lround(m_patternLengthBeats / 4.0)));
            auto lengthMenu = std::make_shared<NUIContextMenu>();
            lengthMenu->addItem("Extend by 2 Bars", [this]() { if (onAdjustPatternLength_) onAdjustPatternLength_(2); });
            lengthMenu->addItem("Shorten by 2 Bars", [this]() { if (onAdjustPatternLength_) onAdjustPatternLength_(-2); });
            menu->addSubmenu("Length: " + std::to_string(bars) + (bars == 1 ? " Bar" : " Bars"), lengthMenu);
        }
        menu->addCheckbox("Follow Playhead", m_followPlayhead, [this](bool on) {
            m_followPlayhead = on;
            if (onFollowPlayheadChanged_) onFollowPlayheadChanged_(m_followPlayhead);
            repaint();
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

    // FD-63 header: the breadcrumb and Snap open menus. The dropdowns stay as the choice lists' state but are no
    // longer shown; Length and Follow moved under More.
    m_patternCrumbBtn = std::make_shared<NUIButton>("");
    m_patternCrumbBtn->setTooltip("Pattern");
    m_patternCrumbBtn->setOnClick([this]() {
        openChoiceMenu(m_patternCrumbBtn->getBounds(), m_appliedPatternChoices, m_appliedPatternSelection,
                       "piano-roll-pattern", [this](int v) { if (onPatternChoiceSelected_) onPatternChoiceSelected_(v); });
    });
    m_unitCrumbBtn = std::make_shared<NUIButton>("");
    m_unitCrumbBtn->setTooltip("Unit");
    m_unitCrumbBtn->setOnClick([this]() {
        openChoiceMenu(m_unitCrumbBtn->getBounds(), m_appliedUnitChoices, m_appliedUnitSelection, "piano-roll-unit",
                       [this](int v) { if (onUnitChoiceSelected_) onUnitChoiceSelected_(v); });
    });
    m_snapBtn = std::make_shared<NUIButton>("");
    m_snapBtn->setTooltip("Snap");
    m_snapBtn->setOnClick([this]() { openSnapMenu(); });

    addChild(m_menuBtn);
    addChild(m_patternCrumbBtn);
    addChild(m_unitCrumbBtn);
    addChild(m_ptrBtn);
    addChild(m_pencilBtn);
    addChild(m_eraserBtn);
    addChild(m_snapBtn);
    addChild(m_scaleBtn);

    // The row draws its own chrome (outline, active fill, glyphs) over these; the buttons only take input, so their own
    // fills must not show through as purple blocks.
    for (const auto& btn : {m_menuBtn, m_patternCrumbBtn, m_unitCrumbBtn, m_ptrBtn, m_pencilBtn, m_eraserBtn, m_snapBtn,
                            m_scaleBtn, m_lengthDownBtn, m_lengthUpBtn, m_followBtn, m_centerBtn}) {
        if (!btn) continue;
        const NUIColor clear(0.0f, 0.0f, 0.0f, 0.0f);
        btn->setBackgroundColor(clear);
        btn->setHoverColor(clear);
        btn->setPressedColor(clear);
        btn->setBorderWidth(0.0f);
    }
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

void PianoRollToolbar::openChoiceMenu(const NUIRect& anchor, const std::vector<PatternChoice>& choices, int selected,
                                      const std::string& group, std::function<void(int)> onPick) {
    if (m_activeContextMenu) {
        const bool wasOpen = m_activeContextMenu->isVisible();
        closeActiveContextMenu();
        if (wasOpen) return;
    }
    auto menu = std::make_shared<NUIContextMenu>();
    m_activeContextMenu = std::static_pointer_cast<NUIComponent>(menu);
    for (const auto& choice : choices) {
        const int value = choice.value;
        menu->addRadioItem(choice.label, group, value == selected, [onPick, value]() { onPick(value); });
    }
    if (auto* parent = getParent()) parent->addChild(m_activeContextMenu);
    else addChild(m_activeContextMenu);
    menu->showAt(static_cast<int>(anchor.x), static_cast<int>(anchor.y + anchor.height + 2.0f));
}

void PianoRollToolbar::openSnapMenu() {
    std::vector<PatternChoice> snaps;
    for (auto snap : MusicTheory::getSnapOptions()) snaps.push_back({static_cast<int>(snap), MusicTheory::getSnapName(snap)});
    openChoiceMenu(m_snapBtn->getBounds(), snaps, static_cast<int>(m_currentSnap), "piano-roll-snap",
                   [this](int v) { applySnap(static_cast<SnapGrid>(v)); });
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
    if (auto keys = keys_.lock()) keys->setScale(m_rootKey, m_scaleType);
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

    // FD-63, board "A + C": a 40 px header. Left to right: the unit's colour, the pattern / the unit (each opens its
    // list), a Draw | Select | Erase segment, "Snap" and "Key" each with a boxed value. More sits at the right; the
    // panel draws the close box after it. Length, Follow and the rest live under More.
    const auto bed = themeManager.getColor("backgroundPrimary");
    const auto raised = themeManager.getColor("surfaceRaised");
    const auto secondary = themeManager.getColor("textSecondary");
    const auto muted = themeManager.getColor("textMuted");
    const float fs = 12.0f;
    const float y = b.y;
    const float hRow = b.height;
    const auto park = [](const std::shared_ptr<NUIComponent>& w) {
        if (w) w->setBounds(NUIRect(0.0f, 0.0f, 0.0f, 0.0f));
    };
    for (const auto& w : {std::static_pointer_cast<NUIComponent>(m_lengthDownBtn), std::static_pointer_cast<NUIComponent>(m_lengthUpBtn),
                          std::static_pointer_cast<NUIComponent>(m_followBtn), std::static_pointer_cast<NUIComponent>(m_centerBtn)})
        park(w);
    const auto textY = [&](const NUIRect& r, float size) { return renderer.calculateTextY(r, size); };
    const auto labelOf = [](const std::vector<PatternChoice>& choices, int selected, const std::string& fallback) {
        for (const auto& c : choices)
            if (c.value == selected) return c.label;
        return fallback;
    };

    float x = b.x + 14.0f;
    // ---- Breadcrumb: [colour] Pattern / Unit ----
    renderer.fillRect(NUIRect(x, std::round(y + (hRow - 10.0f) * 0.5f), 10.0f, 10.0f), m_unitColor);
    x += 18.0f;
    {
        const std::string pattern = labelOf(m_appliedPatternChoices, m_appliedPatternSelection, m_patternName);
        const float w = renderer.measureText(pattern, 13.0f).width;
        const NUIRect r(x, y + 8.0f, w, hRow - 16.0f);
        m_patternCrumbBtn->setBounds(r);
        renderer.drawText(pattern, NUIPoint(x, textY(r, 13.0f)), 13.0f, text.withAlpha(m_patternCrumbBtn->isHovered() ? 1.0f : 0.96f));
        x += w + 8.0f;
        renderer.drawText("/", NUIPoint(x, textY(r, fs)), fs, muted);
        x += renderer.measureText("/", fs).width + 8.0f;
        const std::string unit = labelOf(m_appliedUnitChoices, m_appliedUnitSelection, "");
        const float uw = std::min(renderer.measureText(unit, fs).width, 180.0f);
        const NUIRect ur(x, y + 8.0f, uw, hRow - 16.0f);
        m_unitCrumbBtn->setBounds(ur);
        renderer.drawText(unit, NUIPoint(x, textY(ur, fs)), fs, m_unitCrumbBtn->isHovered() ? text : secondary);
        x += uw + 30.0f;
    }

    // ---- Tools: a boxed segment, the active one raised ----
    {
        struct Tool { std::shared_ptr<NUIButton> button; GlobalTool tool; const char* name; };
        const Tool tools[] = {{m_pencilBtn, GlobalTool::Pencil, "Draw"},
                              {m_ptrBtn, GlobalTool::Pointer, "Select"},
                              {m_eraserBtn, GlobalTool::Eraser, "Erase"}};
        const float segH = 30.0f;
        const float sy = std::round(y + (hRow - segH) * 0.5f);
        float segW = 4.0f;
        float widths[3];
        for (int i = 0; i < 3; ++i) {
            widths[i] = renderer.measureText(tools[i].name, fs).width + 20.0f;
            segW += widths[i] + (i > 0 ? 2.0f : 0.0f);
        }
        const NUIRect seg(x, sy, segW, segH);
        renderer.fillRect(seg, bed);
        renderer.strokeRect(seg, 1.0f, border);
        float tx = x + 2.0f;
        for (int i = 0; i < 3; ++i) {
            const NUIRect r(tx, sy + 2.0f, widths[i], segH - 4.0f);
            tools[i].button->setBounds(r);
            tools[i].button->setText("");
            const bool active = activeTool_ == tools[i].tool;
            if (active) renderer.fillRect(r, raised);
            renderer.drawText(tools[i].name, NUIPoint(r.x + 10.0f, textY(r, fs)), fs,
                              active || tools[i].button->isHovered() ? text : secondary);
            tx += widths[i] + 2.0f;
        }
        x += segW + 14.0f;
    }

    // ---- "Snap" and "Key", each a label and a boxed value ----
    const auto valueBox = [&](const char* label, const std::string& value, const std::shared_ptr<NUIButton>& button,
                              const NUIColor& valueColor) {
        renderer.drawText(label, NUIPoint(x, textY(NUIRect(x, y, 1.0f, hRow), fs)), fs, secondary);
        x += renderer.measureText(label, fs).width + 6.0f;
        const float w = renderer.measureText(value, fs).width + 16.0f;
        const NUIRect r(x, std::round(y + (hRow - 24.0f) * 0.5f), w, 24.0f);
        button->setBounds(r);
        renderer.fillRect(r, bed);
        renderer.strokeRect(r, 1.0f, button->isHovered() ? muted : border);
        renderer.drawText(value, NUIPoint(r.x + 8.0f, textY(r, fs)), fs, valueColor);
        x += w + 14.0f;
    };
    valueBox("Snap", MusicTheory::getSnapName(m_currentSnap), m_snapBtn, text);
    const bool keyed = m_scaleType != ScaleType::Chromatic;
    const std::string key = keyed ? MusicTheory::getRootNames()[std::clamp(m_rootKey, 0, 11)] + " " +
                                        MusicTheory::getScales()[static_cast<size_t>(m_scaleType)].name
                                  : std::string("None");
    valueBox("Key", key, m_scaleBtn, keyed ? text : muted);

    // ---- More, at the right edge (the panel's close box is beyond it) ----
    {
        const float s = 28.0f;
        const NUIRect r(b.right() - s, std::round(y + (hRow - s) * 0.5f), s, s);
        m_menuBtn->setBounds(r);
        m_menuBtn->setText("");
        renderer.strokeRect(r, 1.0f, m_menuBtn->isHovered() ? muted : border);
        if (m_menuIcon) {
            m_menuIcon->setIconSize(14.0f, 14.0f);
            m_menuIcon->setBounds(NUIRect(std::round(r.x + 7.0f), std::round(r.y + 7.0f), 14.0f, 14.0f));
            m_menuIcon->setColor(m_menuBtn->isHovered() ? text : secondary);
            m_menuIcon->onRender(renderer);
        }
    }
    (void)accent;
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
