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
    float titleHeight = 40.0f;
    
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
    
    // Close button
    m_closeButtonBounds = AestraUI::NUIRect(
        m_dialogBounds.x + m_dialogBounds.width - 40,
        m_dialogBounds.y + 10,
        30, 30
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
    
    // 4. Sidebar Background - FORCE correct key
    // We need to respect the rounded corners on the left side.
    // Simplifying: Clip sidebar to rounded rect or just draw rounded rect for sidebar too?
    // Since sidebar is exactly the left part, we can draw a rounded rect for it but clipped?
    // EASIEST: Just draw sidebar as a sharp rect but offset slightly to not bleed out corners?
    // BETTER: Draw the sidebar as a PATH with rounded-left corners. But renderer API is limited.
    // PROPOSAL: Draw rounded rect for sidebar, then cover right side?
    // Actually, visually 5px/8px radius is small enough that drawing a sharp sidebar inside might look OK 
    // IF the sidebar background matches window background. But it's slightly lighter ("backgroundSecondary").
    // Let's try drawing sidebar as a rounded rect, but extend it to the right so the right corners are hidden by content?
    // No, content is to the right.
    // Let's just draw standard fillRect for sidebar for now. The slight corner issue is usually acceptable or handled by z-order.
    // Actually, if we draw the rounded window background first (which we did), and sidebar is on top...
    // The sidebar will be sharp.
    // Let's stick to sharp sidebar for now or check if we have drawRoundedRectDifferentCorners (unlikely).
    // User asked for "box a rounded conners one". Main shape refers to the window.
    
    renderer.fillRect(m_sidebarBounds, theme.getColor("backgroundSecondary")); 
    renderer.fillRect(AestraUI::NUIRect(m_sidebarBounds.x + m_sidebarBounds.width - 1, m_sidebarBounds.y, 1, m_sidebarBounds.height), 
                     theme.getColor("divider"));
    
    // 5. Sidebar Items
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
    
    // 6. Title
    renderer.drawText("Settings", AestraUI::NUIPoint(m_dialogBounds.x + 20, m_dialogBounds.y + 10), 18.0f, theme.getColor("text"));
    
    // 7. Footer Divider
    float footerY = m_dialogBounds.y + m_dialogBounds.height - 60.0f;
    renderer.fillRect(AestraUI::NUIRect(m_dialogBounds.x + 220, footerY, m_dialogBounds.width - 220, 1), 
                     theme.getColor("divider"));
    
    // 8. Close Button
    // Improve visibility: standard "X" and slightly clearer hover effect
    if (m_closeButtonHovered) {
        // Use rounded box instead of circle
        renderer.fillRoundedRect(m_closeButtonBounds, 6.0f, theme.getColor("error").withAlpha(0.8f));
    }
    // Centered X
    // Assuming 30x30 button and 16px font
    renderer.drawText("X", AestraUI::NUIPoint(m_closeButtonBounds.x + 9, m_closeButtonBounds.y + 7), 16.0f, 
                     m_closeButtonHovered ? AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 1.0f) : theme.getColor("text"));// Forced white on error red hover

    // 9. Blink Effect
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
