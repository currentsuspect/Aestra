// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "SettingsDialog.h"

#include "../AestraCore/include/AestraConfig.h"
#include "../AestraCore/include/AestraLog.h"
#include "../AestraUI/Core/NUIThemeSystem.h"

#include <algorithm>

namespace Aestra {

SettingsDialog::SettingsDialog()
    : m_visible(false), m_dialogBounds(0, 0, 940, 620), m_closeButtonHovered(false), m_blinkAnimation(0.0f) {
    createUI();
}

void SettingsDialog::createUI() {
    m_cancelButton = std::make_shared<AestraUI::NUIButton>();
    m_cancelButton->setText("Discard");
    m_cancelButton->setStyle(AestraUI::NUIButton::Style::Secondary);
    m_cancelButton->setOnClick([this]() { discardAll(); });
    addChild(m_cancelButton);

    m_okButton = std::make_shared<AestraUI::NUIButton>();
    m_okButton->setText("Save changes");
    m_okButton->setStyle(AestraUI::NUIButton::Style::Primary);
    m_okButton->setOnClick([this]() { saveAll(); });
    addChild(m_okButton);
}

int SettingsDialog::pendingChangeCount() const {
    int total = 0;
    for (const auto& [id, page] : m_pages) {
        total += page ? page->pendingChangeCount() : 0;
    }
    return total;
}

// Save and Discard act on every page, as the title's "N changes not saved" counts every page.
void SettingsDialog::saveAll() {
    for (const auto& item : m_sidebarItems) {
        auto it = m_pages.find(item.id);
        if (it != m_pages.end() && it->second && it->second->hasUnsavedChanges()) {
            it->second->applyChanges();
        }
    }
    setDirty(true);
}

void SettingsDialog::discardAll() {
    for (const auto& [id, page] : m_pages) {
        if (page && page->hasUnsavedChanges()) {
            page->cancelChanges();
        }
    }
    setDirty(true);
}

void SettingsDialog::show() {
    m_visible = true;
    setVisible(true);
    
    // Ensure we fill the parent window (Modal Overlay)
    if (getParent()) {
        auto parentBounds = getParent()->getBounds();
        if (parentBounds.width > 0 && parentBounds.height > 0) {
            setBounds(parentBounds); // Force resize to parent
            updateDialogBounds(parentBounds);
        }
    }
    
    layoutComponents();
    if (m_activePage) m_activePage->onShow();
}

void SettingsDialog::hide() {
    m_visible = false;
    setVisible(false);
    if (m_activePage) m_activePage->onHide();
    if (m_onClose) m_onClose();
}

void SettingsDialog::addPage(std::shared_ptr<ISettingsPage> page) {
    if (!page) return;
    
    m_pages[page->getPageID()] = page;
    m_sidebarItems.push_back({
        page->getPageID(),
        page->getTitle(),
        false, // hovered
        false, // active
        AestraUI::NUIRect(0,0,0,0) // bounds set in layout
    });

    addChild(page);
    
    // Select first page by default
    if (!m_activePage) {
        setActivePage(page->getPageID());
    }
    
    layoutComponents();
}

void SettingsDialog::setActivePage(const std::string& pageID) {
    auto it = m_pages.find(pageID);
    if (it != m_pages.end()) {
        if (m_activePage) m_activePage->onHide();
        
        m_activePage = it->second;
        m_activePageId = pageID;
        if (m_footerAction)
            removeChild(m_footerAction);
        m_footerAction = m_activePage->footerAction();
        if (m_footerAction)
            addChild(m_footerAction);
        m_activePage->onShow();
        
        // Update selection state
        for (auto& item : m_sidebarItems) {
            item.active = (item.id == pageID);
        }
        layoutComponents();
    }
}

// Board 2: 38 px title, 196 px sidebar, 50 px footer across the whole dialog; the page fills the rest.
void SettingsDialog::layoutComponents() {
    if (!m_visible) return;

    constexpr float kTitleHeight = 38.0f;
    constexpr float kSidebarWidth = 196.0f;
    constexpr float kFooterHeight = 50.0f;
    constexpr float kItemHeight = 32.0f;
    const float sidebarWidth = std::min(kSidebarWidth, m_dialogBounds.width * 0.3f);

    m_titleBounds = AestraUI::NUIRect(m_dialogBounds.x, m_dialogBounds.y, m_dialogBounds.width, kTitleHeight);
    m_footerBounds = AestraUI::NUIRect(m_dialogBounds.x, m_dialogBounds.bottom() - kFooterHeight, m_dialogBounds.width,
                                       kFooterHeight);
    m_sidebarBounds = AestraUI::NUIRect(m_dialogBounds.x, m_titleBounds.bottom(), sidebarWidth,
                                        m_footerBounds.y - m_titleBounds.bottom());
    m_contentBounds = AestraUI::NUIRect(m_sidebarBounds.right(), m_titleBounds.bottom(),
                                        m_dialogBounds.width - sidebarWidth, m_sidebarBounds.height);
    m_closeButtonBounds = AestraUI::NUIRect(m_dialogBounds.right() - 32.0f, m_dialogBounds.y + 7.0f, 24.0f, 24.0f);

    float itemY = m_sidebarBounds.y + 10.0f;
    for (auto& item : m_sidebarItems) {
        item.bounds = AestraUI::NUIRect(m_sidebarBounds.x + 8.0f, itemY, sidebarWidth - 16.0f, kItemHeight);
        itemY += kItemHeight + 2.0f;
    }

    const float buttonHeight = 28.0f;
    const float buttonY = m_footerBounds.y + (kFooterHeight - buttonHeight) * 0.5f;
    const float saveWidth = 112.0f;
    const float discardWidth = 80.0f;
    float rightX = m_footerBounds.right() - 14.0f;
    m_okButton->setBounds(AestraUI::NUIRect(rightX - saveWidth, buttonY, saveWidth, buttonHeight));
    m_cancelButton->setBounds(
        AestraUI::NUIRect(rightX - saveWidth - 8.0f - discardWidth, buttonY, discardWidth, buttonHeight));
    if (m_footerAction) {
        m_footerAction->setBounds(AestraUI::NUIRect(m_footerBounds.x + 14.0f, buttonY, 96.0f, buttonHeight));
    }

    if (m_activePage) {
        m_activePage->setBounds(m_contentBounds);
    }
}

void SettingsDialog::updateDialogBounds(const AestraUI::NUIRect& parentBounds) {
    m_layoutViewport = parentBounds;
    constexpr float kPreferredWidth = 940.0f;
    constexpr float kPreferredHeight = 620.0f;
    constexpr float kWindowMargin = 16.0f;
    constexpr float kMinimumWidth = 360.0f;
    constexpr float kMinimumHeight = 320.0f;

    const float availableWidth = std::max(kMinimumWidth, parentBounds.width - kWindowMargin * 2.0f);
    const float availableHeight = std::max(kMinimumHeight, parentBounds.height - kWindowMargin * 2.0f);
    m_dialogBounds.width = std::min(kPreferredWidth, availableWidth);
    m_dialogBounds.height = std::min(kPreferredHeight, availableHeight);
    m_dialogBounds.x = parentBounds.x + (parentBounds.width - m_dialogBounds.width) * 0.5f;
    m_dialogBounds.y = parentBounds.y + (parentBounds.height - m_dialogBounds.height) * 0.5f;
}

void SettingsDialog::onResize(int width, int height) {
    // Parent bounds (window size)
    setBounds(AestraUI::NUIRect(0, 0, (float)width, (float)height));
    
    updateDialogBounds(getBounds());
    
    layoutComponents();
}

void SettingsDialog::onUpdate(double deltaTime) {
    if (!m_visible) return;
    
    if (m_blinkAnimation > 0.0f) {
        m_blinkAnimation -= (float)deltaTime * 2.0f;
        if (m_blinkAnimation < 0.0f) m_blinkAnimation = 0.0f;
        setDirty(true);
    }
    
    AestraUI::NUIComponent::onUpdate(deltaTime);
}

// The dialog is built lazily, from inside the File menu's own callback. addChild()
// during event dispatch is deferred to the end of dispatch (g_eventDispatchDepth),
// so show() runs with getParent() still null and its centring is skipped — leaving
// the dialog centred in the 950x600 it was given at construction, which is why it
// opened near the window's top-left corner however large the window was.
//
// Centring at paint time instead of at show() covers that, an ordinary window
// resize, and a resize that happens while the dialog is hidden, with one rule: the
// layout is valid only for the viewport it was computed against, and by paint time
// the parent link always exists.
void SettingsDialog::syncToViewport() {
    auto* parent = getParent();
    if (!parent) return;

    const AestraUI::NUIRect viewport = parent->getBounds();
    if (viewport.width <= 0.0f || viewport.height <= 0.0f) return;
    if (viewport.x == m_layoutViewport.x && viewport.y == m_layoutViewport.y &&
        viewport.width == m_layoutViewport.width && viewport.height == m_layoutViewport.height) {
        return;
    }

    setBounds(viewport);
    updateDialogBounds(viewport);
    layoutComponents();
}

void SettingsDialog::onRender(AestraUI::NUIRenderer& renderer) {
    syncToViewport();
    if (!m_visible) return;

    auto& theme = AestraUI::NUIThemeManager::getInstance();
    const auto& props = theme.getCurrentTheme();
    const AestraUI::NUIColor border = theme.getColor("border");

    renderer.fillRect(getBounds(), AestraUI::NUIColor(0.0f, 0.0f, 0.0f, 0.35f));
    renderer.fillRect(m_dialogBounds, theme.getColor("backgroundSecondary"));
    renderer.strokeRect(m_dialogBounds, 1.0f, border);

    // Title: SETTINGS, and how many changes are waiting.
    renderer.drawLine({m_titleBounds.x, m_titleBounds.bottom() - 0.5f},
                      {m_titleBounds.right(), m_titleBounds.bottom() - 0.5f}, 1.0f, border);
    renderer.drawText("SETTINGS", {m_titleBounds.x + 16.0f, renderer.calculateTextY(m_titleBounds, props.fontSizeXS)},
                      props.fontSizeXS, theme.getColor("textMuted"));
    if (const int pending = pendingChangeCount(); pending > 0) {
        const std::string note = std::to_string(pending) + (pending == 1 ? " change not saved" : " changes not saved");
        const float noteFont = 11.5f;
        const float noteWidth = renderer.measureText(note, noteFont).width;
        const float noteX = m_closeButtonBounds.x - 14.0f - noteWidth;
        const AestraUI::NUIColor warn = theme.getColor("warning");
        renderer.fillCircle({noteX - 9.0f, m_titleBounds.y + m_titleBounds.height * 0.5f}, 3.0f, warn);
        renderer.drawText(note, {noteX, renderer.calculateTextY(m_titleBounds, noteFont)}, noteFont, warn);
    }
    if (m_closeButtonHovered) {
        renderer.fillRoundedRect(m_closeButtonBounds, props.radiusS, theme.getColor("controlHover"));
    }
    const float cx = m_closeButtonBounds.x + m_closeButtonBounds.width * 0.5f;
    const float cy = m_closeButtonBounds.y + m_closeButtonBounds.height * 0.5f;
    const float d = 4.5f;
    const AestraUI::NUIColor xColor = theme.getColor(m_closeButtonHovered ? "textPrimary" : "textMuted");
    renderer.drawLine({cx - d, cy - d}, {cx + d, cy + d}, 1.25f, xColor);
    renderer.drawLine({cx + d, cy - d}, {cx - d, cy + d}, 1.25f, xColor);

    // Sidebar: the pages, each with its own count of unsaved changes, and the version at the foot.
    renderer.drawLine({m_sidebarBounds.right() - 0.5f, m_sidebarBounds.y},
                      {m_sidebarBounds.right() - 0.5f, m_sidebarBounds.bottom()}, 1.0f, border);
    for (const auto& item : m_sidebarItems) {
        if (item.active) {
            renderer.fillRoundedRect(item.bounds, 4.0f, theme.getColor("primary").withAlpha(0.20f));
        } else if (item.hovered) {
            renderer.fillRoundedRect(item.bounds, 4.0f, theme.getColor("surfaceRaised"));
        }
        renderer.drawText(item.title, {item.bounds.x + 10.0f, renderer.calculateTextY(item.bounds, 13.0f)}, 13.0f,
                          theme.getColor(item.active ? "textPrimary" : "textSecondary"));
        auto page = m_pages.find(item.id);
        const int count = page != m_pages.end() && page->second ? page->second->pendingChangeCount() : 0;
        if (count > 0) {
            const std::string n = std::to_string(count);
            const float w = renderer.measureText(n, props.fontSizeXS).width;
            renderer.drawText(n,
                              {item.bounds.right() - 10.0f - w, renderer.calculateTextY(item.bounds, props.fontSizeXS)},
                              props.fontSizeXS, theme.getColor("warning"));
        }
    }
    const AestraUI::NUIRect versionRow(m_sidebarBounds.x + 18.0f, m_sidebarBounds.bottom() - 30.0f,
                                       m_sidebarBounds.width - 26.0f, 20.0f);
    renderer.drawText(std::string("Aestra ") + AESTRA_VERSION_STRING,
                      {versionRow.x, renderer.calculateTextY(versionRow, 11.0f)}, 11.0f,
                      theme.getColor("textDisabled"));

    // Footer, across the whole dialog.
    renderer.drawLine({m_footerBounds.x, m_footerBounds.y + 0.5f}, {m_footerBounds.right(), m_footerBounds.y + 0.5f},
                      1.0f, border);
    if (m_footerAction) {
        m_footerAction->onRender(renderer);
        const std::string note = m_activePage ? m_activePage->footerNote() : std::string();
        if (!note.empty()) {
            const auto& fb = m_footerAction->getBounds();
            renderer.drawText(note, {fb.right() + 10.0f, renderer.calculateTextY(fb, 11.5f)}, 11.5f,
                              theme.getColor("textMuted"));
        }
    }

    if (m_blinkAnimation > 0.0f) {
        renderer.strokeRect(m_dialogBounds, 2.0f, theme.getColor("focusRing").withAlpha(m_blinkAnimation * 0.5f));
    }

    if (m_activePage) {
        m_activePage->onRender(renderer);
    }
    const bool pending = pendingChangeCount() > 0;
    m_cancelButton->setEnabled(pending);
    m_okButton->setEnabled(pending);
    m_cancelButton->onRender(renderer);
    m_okButton->onRender(renderer);
}

bool SettingsDialog::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    if (!m_visible) return false;
    
    // Footer buttons interaction
    if (m_footerAction && m_footerAction->onMouseEvent(event))
        return true;
    if (m_cancelButton->onMouseEvent(event)) return true;
    if (m_okButton->onMouseEvent(event)) return true;
    
    // Active Page interaction
    if (m_activePage && m_activePage->onMouseEvent(event)) return true;
    
    // Sidebar interaction
    bool sidebarHovered = false;
    for (auto& item : m_sidebarItems) {
        if (item.bounds.contains(event.position)) {
            if (!item.hovered) {
                item.hovered = true;
                setDirty(true);
            }
            sidebarHovered = true;
            
            if (event.pressed && event.button == AestraUI::NUIMouseButton::Left) {
                setActivePage(item.id);
                return true;
            }
        } else {
            if (item.hovered) {
                item.hovered = false;
                setDirty(true);
            }
        }
    }
    
    // Close button
    bool closeHovered = m_closeButtonBounds.contains(event.position);
    if (closeHovered != m_closeButtonHovered) {
        m_closeButtonHovered = closeHovered;
        setDirty(true);
    }
    
    if (event.pressed && event.button == AestraUI::NUIMouseButton::Left) {
        if (closeHovered) {
            discardAll();
            hide();
            return true;
        }
        
        // Click outside dialog?
        if (!m_dialogBounds.contains(event.position)) {
            m_blinkAnimation = 1.0f;
            setDirty(true);
            return true;
        }
    }
    
    return true; // Consume all events to stay modal
}

bool SettingsDialog::onKeyEvent(const AestraUI::NUIKeyEvent& event) {
    if (!m_visible) return false;
    
    if (m_activePage && m_activePage->onKeyEvent(event)) return true;
    
    if (event.pressed && event.keyCode == AestraUI::NUIKeyCode::Escape) {
        discardAll();
        hide();
        return true;
    }
    
    return true; // Modal
}

} // namespace Aestra
