// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "SettingsDialog.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraCore/include/AestraLog.h"

namespace Aestra {

SettingsDialog::SettingsDialog() 
    : m_visible(false)
    , m_dialogBounds(0, 0, 950, 600) // Larger than AudioSettingsDialog
    , m_closeButtonHovered(false)
    , m_blinkAnimation(0.0f)
{
    createUI();
}

void SettingsDialog::createUI() {
    // TODO: Add a "Workflow / UI Behavior" settings page that includes:
    //   - "Skip send-type confirmation dialog" toggle (default: off).
    //     When enabled, quick-sends created via drag-to-add-send in the
    //     routing map will default to Audio Send without prompting.
    //   - Other workflow preferences (e.g. default send level, auto-fit on open).

    // Footer buttons
    m_applyButton = std::make_shared<AestraUI::NUIButton>();
    m_applyButton->setText("Apply");
    m_applyButton->setStyle(AestraUI::NUIButton::Style::Secondary);
    m_applyButton->setOnClick([this]() {
        if (m_activePage) m_activePage->applyChanges();
    });
    addChild(m_applyButton);

    m_cancelButton = std::make_shared<AestraUI::NUIButton>();
    m_cancelButton->setText("Cancel");
    m_cancelButton->setStyle(AestraUI::NUIButton::Style::Secondary);
    m_cancelButton->setOnClick([this]() {
        if (m_activePage) m_activePage->cancelChanges();
        hide();
    });
    addChild(m_cancelButton);

    m_okButton = std::make_shared<AestraUI::NUIButton>();
    m_okButton->setText("OK");
    m_okButton->setOnClick([this]() {
        if (m_activePage) m_activePage->applyChanges();
        hide();
    });
    addChild(m_okButton);
}

void SettingsDialog::show() {
    m_visible = true;
    setVisible(true);
    
    // Ensure we fill the parent window (Modal Overlay)
    if (getParent()) {
        auto parentBounds = getParent()->getBounds();
        if (parentBounds.width > 0 && parentBounds.height > 0) {
            setBounds(parentBounds); // Force resize to parent
        }
    }

    // Center on screen
    auto componentBounds = getBounds();
    if (componentBounds.width > 0 && componentBounds.height > 0) {
        m_dialogBounds.x = componentBounds.x + (componentBounds.width - m_dialogBounds.width) / 2;
        m_dialogBounds.y = componentBounds.y + (componentBounds.height - m_dialogBounds.height) / 2;
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
        m_activePage->onShow();
        
        // Update selection state
        for (auto& item : m_sidebarItems) {
            item.active = (item.id == pageID);
        }
        layoutComponents();
    }
}

void SettingsDialog::layoutComponents() {
    if (!m_visible) return;
    
    float padding = 20.0f;
    float sidebarWidth = 220.0f;
    float footerHeight = 60.0f;
    float titleHeight = 32.0f;

    // Sidebar
    m_sidebarBounds = AestraUI::NUIRect(
        m_dialogBounds.x,
        m_dialogBounds.y + titleHeight,
        sidebarWidth,
        m_dialogBounds.height - titleHeight
    );

    // Content Area
    m_contentBounds = AestraUI::NUIRect(
        m_dialogBounds.x + sidebarWidth,
        m_dialogBounds.y + titleHeight,
        m_dialogBounds.width - sidebarWidth,
        m_dialogBounds.height - titleHeight - footerHeight
    );

    // Close button — matches AestraPanelWindow chrome (24x24 at right-28, top+4)
    m_closeButtonBounds = AestraUI::NUIRect(
        m_dialogBounds.x + m_dialogBounds.width - 28,
        m_dialogBounds.y + 4,
        24, 24
    );

    // Sidebar items layout
    float itemY = m_sidebarBounds.y + padding;
    float itemHeight = 36.0f;
    for (auto& item : m_sidebarItems) {
        item.bounds = AestraUI::NUIRect(m_dialogBounds.x, itemY, sidebarWidth, itemHeight);
        itemY += itemHeight;
    }
    
    // Footer buttons
    float buttonWidth = 100.0f;
    float buttonHeight = 32.0f;
    float buttonY = m_dialogBounds.y + m_dialogBounds.height - 46.0f;
    float rightX = m_dialogBounds.x + m_dialogBounds.width - padding;
    
    m_okButton->setBounds(AestraUI::NUIRect(rightX - buttonWidth, buttonY, buttonWidth, buttonHeight));
    m_cancelButton->setBounds(AestraUI::NUIRect(rightX - buttonWidth*2 - 10, buttonY, buttonWidth, buttonHeight));
    m_applyButton->setBounds(AestraUI::NUIRect(rightX - buttonWidth*3 - 20, buttonY, buttonWidth, buttonHeight));
    
    // Active Page
    if (m_activePage) {
        m_activePage->setBounds(m_contentBounds);
        // Pages usually need to layout their own internal components
        // We'll rely on onResize propagating the bounds change
    }
}

void SettingsDialog::onResize(int width, int height) {
    // Parent bounds (window size)
    setBounds(AestraUI::NUIRect(0, 0, (float)width, (float)height));
    
    // Center dialog
    m_dialogBounds.x = (width - m_dialogBounds.width) / 2;
    m_dialogBounds.y = (height - m_dialogBounds.height) / 2;
    
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

void SettingsDialog::onRender(AestraUI::NUIRenderer& renderer) {
    if (!m_visible) return;
    
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    
    // 1. Dimmed Background Overlay
    renderer.fillRect(getBounds(), AestraUI::NUIColor(0, 0, 0, 0.5f));
    
    // 2. Dialog Window Shadow
    // (Simple drop shadow simulation if renderer supports it, or just dark rect)
    
    // 3. Dialog Window Background - FORCE correct key to avoid purple fallback
    // Use rounded rect with standard radius (e.g. 8px or theme radius)
    float radius = 8.0f; // Standard rounded corner radius
    renderer.fillRoundedRect(m_dialogBounds, radius, theme.getColor("backgroundPrimary"));
    renderer.strokeRoundedRect(m_dialogBounds, radius, 1.0f, theme.getColor("borderSubtle"));
    
    // 4. Title bar background (unified 32px chrome)
    AestraUI::NUIRect titleBar(m_dialogBounds.x, m_dialogBounds.y, m_dialogBounds.width, 32.0f);
    renderer.fillRect(titleBar, theme.getColor("backgroundPrimary"));
    renderer.drawLine({titleBar.x, titleBar.bottom() - 0.5f},
                      {titleBar.right(), titleBar.bottom() - 0.5f},
                      0.5f, theme.getColor("borderSubtle"));

    // 5. Title text — vertically centered via font metrics
    float titleBaseline = renderer.calculateTextY(titleBar, 12.0f);
    renderer.drawText("Settings", {m_dialogBounds.x + 12.0f, titleBaseline}, 12.0f,
                      theme.getColor("textSecondary"));

    // 6. Close button — line-drawn X matching AestraPanelWindow chrome
    if (m_closeButtonHovered) {
        renderer.fillRoundedRect(m_closeButtonBounds, 6.0f, AestraUI::NUIColor(0xff, 0xd9, 0x5f).withAlpha(0.22f));
    }
    float cx = m_closeButtonBounds.x + m_closeButtonBounds.width * 0.5f;
    float cy = m_closeButtonBounds.y + m_closeButtonBounds.height * 0.5f;
    float d = 5.0f;
    AestraUI::NUIColor xColor = m_closeButtonHovered
                                    ? AestraUI::NUIColor(0xff, 0xe5, 0x73)
                                    : theme.getColor("textDisabled");
    renderer.drawLine({cx - d, cy - d}, {cx + d, cy + d}, 1.5f, xColor);
    renderer.drawLine({cx + d, cy - d}, {cx - d, cy + d}, 1.5f, xColor);

    // 7. Sidebar Background
    renderer.fillRect(m_sidebarBounds, theme.getColor("backgroundSecondary"));
    renderer.fillRect(AestraUI::NUIRect(m_sidebarBounds.x + m_sidebarBounds.width - 1, m_sidebarBounds.y, 1, m_sidebarBounds.height),
                     theme.getColor("divider"));

    // 8. Sidebar Items
    for (const auto& item : m_sidebarItems) {
        AestraUI::NUIColor bg = item.active ? theme.getColor("primary").withAlpha(0.2f) :
                              (item.hovered ? theme.getColor("list.hover") : AestraUI::NUIColor(0,0,0,0));

        if (item.active || item.hovered) {
            renderer.fillRect(item.bounds, bg);
        }

        // Active indicator strip
        if (item.active) {
            renderer.fillRect(AestraUI::NUIRect(item.bounds.x, item.bounds.y, 3, item.bounds.height), theme.getColor("primary"));
        }

        renderer.drawText(item.title, AestraUI::NUIPoint(item.bounds.x + 20, item.bounds.y + 8), 14.0f,
                         item.active ? theme.getColor("textSelect") : theme.getColor("text"));
    }

    // 9. Footer Divider
    float footerY = m_dialogBounds.y + m_dialogBounds.height - 60.0f;
    renderer.fillRect(AestraUI::NUIRect(m_dialogBounds.x + 220, footerY, m_dialogBounds.width - 220, 1),
                     theme.getColor("divider"));

    // 10. Blink Effect
    if (m_blinkAnimation > 0.0f) {
        renderer.strokeRoundedRect(m_dialogBounds, radius, 2.0f, AestraUI::NUIColor(1.0f, 1.0f, 1.0f, m_blinkAnimation * 0.5f));
    }

    // Render Children (Page content + Buttons)
    // We only want to render the active page
    if (m_activePage) {
        m_activePage->onRender(renderer);
    }
    
    // Render footer buttons
    m_applyButton->onRender(renderer);
    m_cancelButton->onRender(renderer);
    m_okButton->onRender(renderer);
}

bool SettingsDialog::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    if (!m_visible) return false;
    
    // Footer buttons interaction
    if (m_applyButton->onMouseEvent(event)) return true;
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
        hide();
        return true;
    }
    
    return true; // Modal
}

} // namespace Aestra
