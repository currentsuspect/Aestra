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
    m_menuBtn->setTooltip("Scale, Snap & Settings");
    
    const char* menuSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M3 18h18v-2H3v2zm0-5h18v-2H3v2zm0-7v2h18V6H3z"/></svg>)";
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
            rootMenu->addItem(roots[i], [this, i]() {
                if (auto g = grid_.lock()) g->setRootKey(static_cast<int>(i));
                if (auto n = notes_.lock()) n->setRootKey(static_cast<int>(i));
            });
        }
        menu->addSubmenu("Root Key", rootMenu);
        
        // --- SCALE SUBMENU ---
        auto scaleMenu = std::make_shared<NUIContextMenu>();
        auto scales = MusicTheory::getScales();
        for (size_t i = 0; i < scales.size(); ++i) {
            scaleMenu->addItem(scales[i].name, [this, i]() {
                if (auto g = grid_.lock()) g->setScaleType(static_cast<ScaleType>(i));
                if (auto n = notes_.lock()) n->setScaleType(static_cast<ScaleType>(i));
            });
        }
        menu->addSubmenu("Scale Type", scaleMenu);

        // --- SNAP TO SCALE TOGGLE ---
        bool snapToScaleActive = false;
        if (auto n = notes_.lock()) {
            snapToScaleActive = n->getSnapToScale();
        }
        menu->addCheckbox("Snap to Scale", snapToScaleActive, [this](bool) {
            if (auto n = notes_.lock()) {
                n->setSnapToScale(!n->getSnapToScale());
            }
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

        // --- HUMANIZE ---
        menu->addItem("Humanize Velocity", [this]() {
            if (auto n = notes_.lock()) n->humanizeSelectedVelocities();
        });

        // --- SHORTCUT CHEAT SHEET ---
        menu->addItem("Keyboard Shortcuts", [this]() {
            if (onShowShortcutHelp_) onShowShortcutHelp_();
        });

        auto b = m_menuBtn->getBounds();
        if (auto* parent = getParent()) {
            parent->addChild(m_activeContextMenu);
            const auto pb = parent->getBounds();
            menu->showAt(static_cast<int>(b.x - pb.x),
                         static_cast<int>(b.y - pb.y + b.height + 2.0f));
        } else {
            addChild(m_activeContextMenu);
            menu->showAt(static_cast<int>(b.x), static_cast<int>(b.y + b.height + 2.0f));
        }
    });

    // 1. Tool Buttons
    m_ptrBtn = std::make_shared<NUIButton>("");
    m_ptrBtn->setOnClick([this](){ setActiveTool(GlobalTool::Pointer); });

    m_pencilBtn = std::make_shared<NUIButton>("");
    m_pencilBtn->setOnClick([this](){ setActiveTool(GlobalTool::Pencil); });

    m_eraserBtn = std::make_shared<NUIButton>("");
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

    addChild(m_menuBtn);
    addChild(m_ptrBtn);
    addChild(m_pencilBtn);
    addChild(m_eraserBtn);
    addChild(m_patternDropdown);
    addChild(m_unitDropdown);
    addChild(m_snapDropdown);
    addChild(m_lengthDownBtn);
    addChild(m_lengthUpBtn);
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

    m_updatingUnitDropdown = true;
    m_unitDropdown->clearItems();
    for (const auto& choice : choices) {
        m_unitDropdown->addItem(choice.label, choice.value);
    }
    m_unitDropdown->setSelectedByValue(selectedValue);
    m_updatingUnitDropdown = false;
    repaint();
}


void PianoRollToolbar::onRender(NUIRenderer& renderer) {
    auto b = getBounds();
    auto& themeManager = NUIThemeManager::getInstance();

    const auto toolbarBg = themeManager.getColor("backgroundSecondary").darkened(0.015f);
    const auto groupBg = themeManager.getColor("backgroundPrimary").withAlpha(0.72f);
    const auto groupBorder = themeManager.getColor("border").withAlpha(0.52f);
    renderer.fillRect(b, toolbarBg);
    renderer.drawLine(NUIPoint(b.x, b.bottom() - 0.5f),
                      NUIPoint(b.right(), b.bottom() - 0.5f),
                      1.0f,
                      groupBorder);

    // Let child buttons keep input/hit-testing, but draw our custom toolbar chrome and glyphs after them.
    renderChildren(renderer);

    const float buttonSize = 30.0f;
    const float buttonSpacing = 4.0f;
    const float innerPad = 10.0f;
    const float radius = 6.0f;

    float currentX = b.x + innerPad;
    float currentY = b.y + (b.height - buttonSize) * 0.5f;

    auto idleBg = themeManager.getColor("surfaceSecondary").withAlpha(0.76f);
    auto hoverBg = themeManager.getColor("buttonBgHover").withAlpha(0.98f);
    auto activeBg = themeManager.getColor("accentPrimary").withAlpha(0.84f);
    auto borderCol = themeManager.getColor("border").withAlpha(0.42f);
    auto iconIdle = themeManager.getColor("textPrimary").withAlpha(0.78f);
    auto iconActive = themeManager.getColor("textPrimary");

    auto drawGroup = [&](float x, float width) {
        NUIRect groupRect(x, currentY - 3.0f, width, buttonSize + 6.0f);
        renderer.fillRoundedRect(groupRect, 8.0f, groupBg);
        renderer.strokeRoundedRect(groupRect, 8.0f, 1.0f, groupBorder);
    };

    auto renderButton = [&](std::shared_ptr<NUIButton> btn, std::shared_ptr<NUIIcon> icon, bool isActive) {
        btn->setBounds(NUIRect(currentX, currentY, buttonSize, buttonSize));
        btn->setText("");

        AestraUI::NUIColor bg = idleBg;
        AestraUI::NUIColor border = borderCol;

        if (isActive) {
            bg = activeBg;
            border = themeManager.getColor("accentPrimary").lightened(0.18f).withAlpha(0.88f);
        } else if (btn->isHovered()) {
            bg = hoverBg;
        }

        renderer.fillRoundedRect(btn->getBounds(), radius, bg);
        renderer.strokeRoundedRect(btn->getBounds(), radius, 1.0f, border);

        if (icon) {
            const float iconSz = 15.0f;
            NUIRect iconRect(
                std::round(currentX + (buttonSize - iconSz) * 0.5f),
                std::round(currentY + (buttonSize - iconSz) * 0.5f),
                iconSz, iconSz
            );
            icon->setBounds(iconRect);
            icon->setColor(isActive ? iconActive : iconIdle);
            icon->onRender(renderer);
        }
        
        currentX += buttonSize + buttonSpacing;
    };

    drawGroup(currentX - 3.0f, buttonSize + 6.0f);
    renderButton(m_menuBtn, m_menuIcon, false);

    currentX += 8.0f;

    drawGroup(currentX - 3.0f, buttonSize * 3.0f + buttonSpacing * 2.0f + 6.0f);
    renderButton(m_ptrBtn, m_ptrIcon, activeTool_ == GlobalTool::Pointer);
    renderButton(m_pencilBtn, m_pencilIcon, activeTool_ == GlobalTool::Pencil);
    renderButton(m_eraserBtn, m_eraserIcon, activeTool_ == GlobalTool::Eraser);

    float patternDropdownRight = currentX;
    if (m_patternDropdown) {
        currentX += 8.0f;

        const float dropdownW = std::clamp(b.width * 0.24f, 180.0f, 250.0f);
        drawGroup(currentX - 3.0f, dropdownW + 6.0f);
        m_patternDropdown->setBounds(NUIRect(currentX, currentY, dropdownW, buttonSize));
        m_patternDropdown->onRender(renderer);
        patternDropdownRight = currentX + dropdownW;
        currentX += dropdownW + buttonSpacing;
    }

    if (m_unitDropdown) {
        currentX += 8.0f;

        const float unitDropdownW = std::clamp(b.width * 0.18f, 140.0f, 200.0f);
        drawGroup(currentX - 3.0f, unitDropdownW + 6.0f);
        m_unitDropdown->setBounds(NUIRect(currentX, currentY, unitDropdownW, buttonSize));
        m_unitDropdown->onRender(renderer);
        patternDropdownRight = currentX + unitDropdownW;
        currentX += unitDropdownW + buttonSpacing;
    }

    currentX += 8.0f;

    const int patternBars = std::max(1, static_cast<int>(std::lround(m_patternLengthBeats / 4.0)));
    const std::string lengthLabel = std::to_string(patternBars) + " Bars";
    const float lengthFontSize = themeManager.getFontSize("xs");
    const auto lengthSize = renderer.measureText(lengthLabel, lengthFontSize);
    // develop's layout (grouped [-][pill][+] with the down button first) with
    // this branch's semantic spacing/color tokens.
    const float pillPadX = themeManager.getSpacing("s");
    const float pillW = std::max(64.0f, lengthSize.width + pillPadX * 2.0f);
    const float lengthGroupX = currentX - 3.0f;
    const float lengthGroupW = buttonSize * 2.0f + pillW + buttonSpacing * 2.0f + 6.0f;
    drawGroup(lengthGroupX, lengthGroupW);

    renderButton(m_lengthDownBtn, m_lengthDownIcon, false);
    const NUIRect pillRect(currentX, currentY, pillW, buttonSize);
    renderer.fillRoundedRect(pillRect, radius, themeManager.getColor("controlBackground").withAlpha(0.92f));
    renderer.strokeRoundedRect(pillRect, radius, 1.0f, borderCol.withAlpha(0.35f));
    renderer.drawText(lengthLabel,
                      NUIPoint(pillRect.x + (pillRect.width - lengthSize.width) * 0.5f,
                               pillRect.y + (pillRect.height - lengthSize.height) * 0.5f + 1.0f),
                      lengthFontSize,
                      themeManager.getColor("textPrimary").withAlpha(0.82f));
    currentX += pillW + buttonSpacing;

    renderButton(m_lengthUpBtn, m_lengthUpIcon, false);

    // Snap selector — a real dropdown (was a dead "SNAP Beat" text pill). The
    // menu's Snap submenu and this dropdown stay in sync via applySnap().
    currentX += 8.0f;
    if (m_snapDropdown) {
        const float snapDropdownW = 104.0f;
        drawGroup(currentX - 3.0f, snapDropdownW + 6.0f);
        m_snapDropdown->setBounds(NUIRect(currentX, currentY, snapDropdownW, buttonSize));
        m_snapDropdown->onRender(renderer);
        currentX += snapDropdownW;
    }

    // (The redundant right-side "Pattern · Unit" label was removed — the Pattern
    // and Unit dropdowns already show that state.)
    (void)patternDropdownRight;
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
    if (m_ptrBtn->onMouseEvent(event)) return true;
    if (m_pencilBtn->onMouseEvent(event)) return true;
    if (m_eraserBtn->onMouseEvent(event)) return true;
    if (m_lengthDownBtn->onMouseEvent(event)) return true;
    if (m_lengthUpBtn->onMouseEvent(event)) return true;

    return false;
}

} // namespace AestraUI
