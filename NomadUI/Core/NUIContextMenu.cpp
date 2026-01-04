// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUIContextMenu.h"
#include "../Graphics/NUIRenderer.h"
#include "NUIThemeSystem.h"
#include <algorithm>

#ifdef _WIN32
#include <Windows.h>
#endif
#include <cmath>
#include <iostream>

namespace NomadUI {

// ============================================================================
// NUIContextMenuItem Implementation
// ============================================================================

NUIContextMenuItem::NUIContextMenuItem(const std::string& text, Type type)
    : text_(text)
    , type_(type)
{
}

void NUIContextMenuItem::setText(const std::string& text)
{
    text_ = text;
}

void NUIContextMenuItem::setType(Type type)
{
    type_ = type;
}

void NUIContextMenuItem::setEnabled(bool enabled)
{
    enabled_ = enabled;
}

void NUIContextMenuItem::setVisible(bool visible)
{
    visible_ = visible;
}

void NUIContextMenuItem::setChecked(bool checked)
{
    checked_ = checked;
}

void NUIContextMenuItem::setShortcut(const std::string& shortcut)
{
    shortcut_ = shortcut;
}

void NUIContextMenuItem::setIcon(const std::string& iconPath)
{
    iconPath_ = iconPath;
}

void NUIContextMenuItem::setIconObject(std::shared_ptr<NUIIcon> icon)
{
    icon_ = icon;
}

void NUIContextMenuItem::setSubmenu(std::shared_ptr<NUIContextMenu> submenu)
{
    submenu_ = submenu;
}

void NUIContextMenuItem::setOnClick(std::function<void()> callback)
{
    onClickCallback_ = callback;
}

void NUIContextMenuItem::setRadioGroup(const std::string& group)
{
    radioGroup_ = group;
}

// ============================================================================
// NUIContextMenu Implementation
// ============================================================================

NUIContextMenu::NUIContextMenu()
    : NUIComponent()
{
    setSize(200, 100); // Default size
    
    // Apply Nomad theme colors - using the new layered system
    auto& themeManager = NUIThemeManager::getInstance();
    backgroundColor_ = themeManager.getColor("surfaceTertiary");      // #242428 - Dialogs/popups
    borderColor_ = themeManager.getColor("borderActive");             // #8B7FFF - Active purple border
    textColor_ = themeManager.getColor("textPrimary");                // #E5E5E8 - Main text
    hoverColor_ = themeManager.getColor("primary");                   // #8B7FFF - Nomad purple highlight
    separatorColor_ = themeManager.getColor("borderSubtle");          // #2c2c2f - Subtle dividers
    shortcutColor_ = themeManager.getColor("textSecondary");          // #A6A6AA - Muted shortcuts
}

void NUIContextMenu::onRender(NUIRenderer& renderer)
{
    if (!isVisible()) return;

    drawBackground(renderer);

    // Draw items
    for (int i = 0; i < getItemCount(); ++i)
    {
        auto item = getItem(i);
        if (item && item->isVisible())
        {
            if (item->getType() == NUIContextMenuItem::Type::Separator)
            {
                drawSeparator(renderer, i);
            }
            else
            {
                drawItem(renderer, item, i);
            }
        }
    }

    // Render active submenu on top
    if (activeSubmenu_ && activeSubmenu_->isVisible())
    {
        activeSubmenu_->onRender(renderer);
    }
}

bool NUIContextMenu::onMouseEvent(const NUIMouseEvent& event)
{
    if (!isVisible()) return false;

    NUIRect bounds = getBounds();
    
    // Give priority to submenu
    if (activeSubmenu_ && activeSubmenu_->isVisible())
    {
        if (activeSubmenu_->onMouseEvent(event))
        {
            return true;
        }
    }

    if (!bounds.contains(event.position)) return false;

    int itemIndex = getItemAtPosition(event.position);

    if (event.pressed && event.button == NUIMouseButton::Left)
    {
        pressedItemIndex_ = itemIndex;
        setDirty(true);
        return true;
    }
    else if (event.released && event.button == NUIMouseButton::Left)
    {
        if (pressedItemIndex_ == itemIndex && itemIndex >= 0)
        {
            handleItemClick(itemIndex);
        }
        pressedItemIndex_ = -1;
        setDirty(true);
        return true;
    }
    else if (event.button == NUIMouseButton::None) // Mouse move
    {
        if (itemIndex != hoveredItemIndex_)
        {
            hoveredItemIndex_ = itemIndex;
            if (itemIndex >= 0)
            {
                handleItemHover(itemIndex);
            }
            setDirty(true);
        }
        return true;
    }

    return false;
}

bool NUIContextMenu::onKeyEvent(const NUIKeyEvent& event)
{
    if (!isVisible()) return false;

    if (event.pressed)
    {
        switch (event.keyCode)
        {
            case NUIKeyCode::Escape:
                hide();
                return true;
            case NUIKeyCode::Up:
                if (hoveredItemIndex_ > 0)
                {
                    hoveredItemIndex_--;
                    setDirty(true);
                }
                return true;
            case NUIKeyCode::Down:
                if (hoveredItemIndex_ < getItemCount() - 1)
                {
                    hoveredItemIndex_++;
                    setDirty(true);
                }
                return true;
            case NUIKeyCode::Enter:
            case NUIKeyCode::Space:
                if (hoveredItemIndex_ >= 0)
                {
                    handleItemClick(hoveredItemIndex_);
                }
                return true;
            default:
                break;
        }
    }

    return false;
}

void NUIContextMenu::onMouseEnter()
{
    // Context menu doesn't need mouse enter handling
}

void NUIContextMenu::onMouseLeave()
{
    hoveredItemIndex_ = -1;
    setDirty(true);
}

void NUIContextMenu::addItem(std::shared_ptr<NUIContextMenuItem> item)
{
    items_.push_back(item);
    updateLayout();
    setDirty(true);
}

void NUIContextMenu::addItem(const std::string& text, std::function<void()> callback)
{
    auto item = std::make_shared<NUIContextMenuItem>(text);
    item->setOnClick(callback);
    addItem(item);
}

void NUIContextMenu::addSeparator()
{
    auto separator = std::make_shared<NUIContextMenuItem>("", NUIContextMenuItem::Type::Separator);
    addItem(separator);
}

void NUIContextMenu::addSubmenu(const std::string& text, std::shared_ptr<NUIContextMenu> submenu)
{
    auto item = std::make_shared<NUIContextMenuItem>(text, NUIContextMenuItem::Type::Submenu);
    item->setSubmenu(submenu);
    
    // Chain Close: If an item in the submenu is clicked, close this parent menu too.
    submenu->setOnItemClick([this](std::shared_ptr<NUIContextMenuItem> clickedItem) {
        if (closeOnSelection_) {
            hide();
        }
        // Propagate the click event up if needed (optional, logic usually handled by leaf callback)
        if (onItemClickCallback_) {
            onItemClickCallback_(clickedItem);
        }
    });
    
    addItem(item);
}

void NUIContextMenu::addCheckbox(const std::string& text, bool checked, std::function<void(bool)> callback)
{
    auto item = std::make_shared<NUIContextMenuItem>(text, NUIContextMenuItem::Type::Checkbox);
    item->setChecked(checked);
    if (callback)
    {
        item->setOnClick([callback, checked]() { callback(!checked); });
    }
    addItem(item);
}

void NUIContextMenu::addRadioItem(const std::string& text, const std::string& group, bool selected, std::function<void()> callback)
{
    auto item = std::make_shared<NUIContextMenuItem>(text, NUIContextMenuItem::Type::Radio);
    item->setRadioGroup(group);
    item->setChecked(selected);
    item->setOnClick(callback);
    addItem(item);
}

void NUIContextMenu::clear()
{
    items_.clear();
    hoveredItemIndex_ = -1;
    pressedItemIndex_ = -1;
    updateLayout();
    setDirty(true);
}

void NUIContextMenu::showAt(const NUIPoint& position)
{
    showAt(static_cast<int>(position.x), static_cast<int>(position.y));
}

void NUIContextMenu::showAt(int x, int y)
{
    updateLayout();
    float menuWidth = getBounds().width;
    float menuHeight = getBounds().height;
    
    // Smart positioning - near cursor but adjust if off-screen
    float posX = x;
    float posY = y;
    
    // Get window bounds by traversing up to find the root parent
    // This ensures submenus use the actual window bounds, not parent menu bounds
    NUIRect windowBounds(0, 0, 1920, 1080);  // Larger default fallback
    bool foundParent = false;
    NUIComponent* current = getParent();
    while (current) {
        windowBounds = current->getBounds();
        foundParent = true;
        current = current->getParent();
    }
    
    // Only apply clamping if we found valid parent bounds
    if (foundParent && windowBounds.width > 0 && windowBounds.height > 0) {
        float windowRight = windowBounds.x + windowBounds.width;
        float windowBottom = windowBounds.y + windowBounds.height;
        
        // Check if menu would go off the right edge of the window - move it left
        if (posX + menuWidth > windowRight) {
            posX = windowRight - menuWidth - 10; // 10px margin from window edge
        }
        
        // Check if menu would go off the bottom edge of the window - move it up
        if (posY + menuHeight > windowBottom) {
            posY = windowBottom - menuHeight - 10; // 10px margin from window edge
        }
        
        // Ensure menu doesn't go off the left or top edges of the window
        if (posX < windowBounds.x) posX = windowBounds.x + 10; // 10px margin from window edge
        if (posY < windowBounds.y) posY = windowBounds.y + 10; // 10px margin from window edge
    }
    
    setPosition(posX, posY);
    isVisible_ = true;
    hoveredItemIndex_ = 0; // Start with first item selected
    pressedItemIndex_ = -1;
    triggerShow();
    setDirty(true);
}

void NUIContextMenu::hide()
{
    isVisible_ = false;
    hoveredItemIndex_ = -1;
    pressedItemIndex_ = -1;
    hideSubmenu();
    triggerHide();
    setDirty(true);
}

void NUIContextMenu::setBackgroundColor(const NUIColor& color)
{
    backgroundColor_ = color;
    setDirty(true);
}

void NUIContextMenu::setBorderColor(const NUIColor& color)
{
    borderColor_ = color;
    setDirty(true);
}

void NUIContextMenu::setTextColor(const NUIColor& color)
{
    textColor_ = color;
    setDirty(true);
}

void NUIContextMenu::setHoverColor(const NUIColor& color)
{
    hoverColor_ = color;
    setDirty(true);
}

void NUIContextMenu::setSeparatorColor(const NUIColor& color)
{
    separatorColor_ = color;
    setDirty(true);
}

void NUIContextMenu::setShortcutColor(const NUIColor& color)
{
    shortcutColor_ = color;
    setDirty(true);
}

void NUIContextMenu::setBorderWidth(float width)
{
    borderWidth_ = width;
    setDirty(true);
}

void NUIContextMenu::setBorderRadius(float radius)
{
    borderRadius_ = radius;
    setDirty(true);
}

void NUIContextMenu::setItemHeight(float height)
{
    itemHeight_ = height;
    updateLayout();
    setDirty(true);
}

void NUIContextMenu::setItemPadding(float padding)
{
    itemPadding_ = padding;
    updateLayout();
    setDirty(true);
}

void NUIContextMenu::setIconSize(float size)
{
    iconSize_ = size;
    setDirty(true);
}

void NUIContextMenu::setAutoHide(bool autoHide)
{
    autoHide_ = autoHide;
}

void NUIContextMenu::setCloseOnSelection(bool close)
{
    closeOnSelection_ = close;
}

void NUIContextMenu::setMaxHeight(float height)
{
    maxHeight_ = height;
    updateLayout();
    setDirty(true);
}

void NUIContextMenu::setScrollable(bool scrollable)
{
    scrollable_ = scrollable;
    updateLayout();
    setDirty(true);
}

void NUIContextMenu::setOnShow(std::function<void()> callback)
{
    onShowCallback_ = callback;
}

void NUIContextMenu::setOnHide(std::function<void()> callback)
{
    onHideCallback_ = callback;
}

void NUIContextMenu::setOnItemClick(std::function<void(std::shared_ptr<NUIContextMenuItem>)> callback)
{
    onItemClickCallback_ = callback;
}

void NUIContextMenu::navigateUp()
{
    if (hoveredItemIndex_ > 0) {
        hoveredItemIndex_--;
        setDirty(true);
    }
}

void NUIContextMenu::navigateDown()
{
    if (hoveredItemIndex_ < getItemCount() - 1) {
        hoveredItemIndex_++;
        setDirty(true);
    }
}


std::shared_ptr<NUIContextMenuItem> NUIContextMenu::getItem(int index) const
{
    if (index >= 0 && index < static_cast<int>(items_.size()))
    {
        return items_[index];
    }
    return nullptr;
}

void NUIContextMenu::drawBackground(NUIRenderer& renderer)
{
    NUIRect bounds = getBounds();
    
    // Draw background
    renderer.fillRoundedRect(bounds, borderRadius_, backgroundColor_);
    
    // Draw border
    renderer.strokeRoundedRect(bounds, borderRadius_, borderWidth_, borderColor_);
}

void NUIContextMenu::drawItem(NUIRenderer& renderer, std::shared_ptr<NUIContextMenuItem> item, int index)
{
    if (!item || !item->isVisible()) return;

    NUIRect itemRect = getItemRect(index);
    
    // Draw hover background with Nomad's purple highlight
    if (index == hoveredItemIndex_)
    {
        auto& themeManager = NUIThemeManager::getInstance();
        NUIColor purpleHighlight = themeManager.getColor("primary").withAlpha(0.15f);
        renderer.fillRoundedRect(itemRect, 2.0f, purpleHighlight);
    }
    
    // Draw item content
    float x = itemRect.x + itemPadding_ + 4.0f;
    const float itemFontSize = 13.0f;
    float y = std::round(renderer.calculateTextY(itemRect, itemFontSize));
    
    // Draw icon if present
    if (item->getIconObject())
    {
        float iconY = itemRect.y + (itemRect.height - iconSize_) * 0.5f;
        item->getIconObject()->setPosition(x, iconY);
        item->getIconObject()->setIconSize(iconSize_, iconSize_);
        item->getIconObject()->onRender(renderer);
        x += iconSize_ + itemPadding_ * 0.5f;
    }
    
    // Draw checkbox/radio indicator
    if (item->getType() == NUIContextMenuItem::Type::Checkbox || 
        item->getType() == NUIContextMenuItem::Type::Radio)
    {
        float indicatorSize = 14.0f;
        float indicatorY = itemRect.y + (itemRect.height - indicatorSize) * 0.5f;
        NUIRect indicatorRect(x, indicatorY, indicatorSize, indicatorSize);
        
        auto& themeManager = NUIThemeManager::getInstance();
        
        if (item->getType() == NUIContextMenuItem::Type::Checkbox)
        {
            // Checkbox - rounded square
            renderer.strokeRoundedRect(indicatorRect, 3.0f, 1.0f, themeManager.getColor("borderSubtle"));
            
            if (item->isChecked())
            {
                // Fill with purple
                renderer.fillRoundedRect(indicatorRect, 3.0f, themeManager.getColor("primary"));
                
                // Use NUIIcon for checkmark
                auto checkIcon = NUIIcon::createCheckIcon();
                checkIcon->setIconSize(indicatorSize * 0.8f, indicatorSize * 0.8f);
                checkIcon->setColor(NUIColor(1.0f, 1.0f, 1.0f, 1.0f));
                NUIPoint center = indicatorRect.center();
                checkIcon->setPosition(center.x - indicatorSize * 0.4f, center.y - indicatorSize * 0.4f);
                checkIcon->onRender(renderer);
            }
        }
        else
        {
            // Radio - "Dotted" style (Selected = Solid Dot, Unselected = Empty Ring)
            NUIPoint center = indicatorRect.center();
            
            if (item->isChecked())
            {
                // Selected: Solid filled "dot" in Nomad Purple
                NUIColor radioFill = themeManager.getColor("accentPrimary");
                renderer.fillCircle(center, indicatorSize * 0.4f, radioFill);
            }
            else
            {
                // Unselected: Empty ring (faint)
                renderer.strokeCircle(center, indicatorSize * 0.5f, 1.0f, themeManager.getColor("borderSubtle").withAlpha(0.5f));
            }
        }
        
        x += indicatorSize + itemPadding_;
    }
    
    // Draw text
    NUIColor textColor = item->isEnabled() ? textColor_ : textColor_.withAlpha(0.4f);
    renderer.drawText(item->getText(), NUIPoint(x, y), itemFontSize, textColor);
    
    // Draw shortcut
    if (!item->getShortcut().empty())
    {
        auto& themeManager = NUIThemeManager::getInstance();
        float shortcutX = itemRect.x + itemRect.width - itemPadding_ - 60.0f;
        const float shortcutFontSize = 12.0f;
        float shortcutY = std::round(renderer.calculateTextY(itemRect, shortcutFontSize));
        renderer.drawText(item->getShortcut(), NUIPoint(shortcutX, shortcutY), shortcutFontSize, themeManager.getColor("textSecondary"));
    }
    
    // Draw submenu arrow using NUIIcon
    if (item->getType() == NUIContextMenuItem::Type::Submenu)
    {
        drawSubmenuArrow(renderer, index);
    }
}

void NUIContextMenu::drawSeparator(NUIRenderer& renderer, int index)
{
    NUIRect itemRect = getItemRect(index);
    float centerY = itemRect.y + itemRect.height * 0.5f;
    
    auto& themeManager = NUIThemeManager::getInstance();
    NUIPoint p1(itemRect.x + itemPadding_ + 4.0f, centerY);
    NUIPoint p2(itemRect.x + itemRect.width - itemPadding_ - 4.0f, centerY);
    
    renderer.drawLine(p1, p2, 1.0f, themeManager.getColor("borderSubtle"));
}

void NUIContextMenu::drawSubmenuArrow(NUIRenderer& renderer, int index)
{
    NUIRect itemRect = getItemRect(index);
    float arrowSize = 12.0f;
    float arrowX = itemRect.x + itemRect.width - itemPadding_ - arrowSize - 4.0f;
    float arrowY = itemRect.y + (itemRect.height - arrowSize) * 0.5f;
    
    // Use chevron right icon for submenu arrow
    auto chevronIcon = NUIIcon::createChevronRightIcon();
    chevronIcon->setIconSize(arrowSize, arrowSize);
    chevronIcon->setColor(textColor_);
    chevronIcon->setPosition(arrowX, arrowY);
    chevronIcon->onRender(renderer);
}

void NUIContextMenu::updateLayout()
{
    updateSize();
}

NUIRect NUIContextMenu::getItemRect(int index) const
{
    if (index < 0 || index >= getItemCount()) return NUIRect();
    
    NUIRect bounds = getBounds();
    float y = bounds.y;
    
    // Calculate Y position accounting for separator heights
    for (int i = 0; i < index; ++i)
    {
        auto item = getItem(i);
        if (item && item->getType() == NUIContextMenuItem::Type::Separator)
        {
            y += 8.0f; // Separators are shorter
        }
        else
        {
            y += itemHeight_;
        }
    }
    
    auto currentItem = getItem(index);
    float height = (currentItem && currentItem->getType() == NUIContextMenuItem::Type::Separator) ? 8.0f : itemHeight_;
    
    return NUIRect(bounds.x, y, bounds.width, height);
}

float NUIContextMenu::calculateMenuHeight() const
{
    float height = 0.0f;
    for (const auto& item : items_)
    {
        if (item && item->isVisible())
        {
            if (item->getType() == NUIContextMenuItem::Type::Separator)
            {
                height += 8.0f; // Separators are shorter
            }
            else
            {
                height += itemHeight_;
            }
        }
    }
    
    return (height < maxHeight_) ? height : maxHeight_;
}

int NUIContextMenu::getItemAtPosition(const NUIPoint& position) const
{
    NUIRect bounds = getBounds();
    if (!bounds.contains(position)) return -1;
    
    float relativeY = position.y - bounds.y;
    float currentY = 0.0f;
    
    for (int i = 0; i < getItemCount(); ++i)
    {
        auto item = getItem(i);
        if (item && item->isVisible())
        {
            float itemH = (item->getType() == NUIContextMenuItem::Type::Separator) ? 8.0f : itemHeight_;
            
            if (relativeY >= currentY && relativeY < currentY + itemH)
            {
                // Don't allow selecting separators
                if (item->getType() != NUIContextMenuItem::Type::Separator)
                {
                    return i;
                }
                return -1;
            }
            
            currentY += itemH;
        }
    }
    
    return -1;
}

void NUIContextMenu::handleItemClick(int index)
{
    auto item = getItem(index);
    if (!item || !item->isEnabled()) return;
    
    // Handle radio group selection
    if (item->getType() == NUIContextMenuItem::Type::Radio && !item->getRadioGroup().empty())
    {
        // Uncheck other items in the same group
        for (auto& otherItem : items_)
        {
            if (otherItem && otherItem != item && 
                otherItem->getRadioGroup() == item->getRadioGroup())
            {
                otherItem->setChecked(false);
            }
        }
        item->setChecked(true);
    }
    
    // Handle submenu
    if (item->getType() == NUIContextMenuItem::Type::Submenu && item->getSubmenu() != nullptr)
    {
        showSubmenu(index);
        return;
    }
    
    // Trigger item click
    triggerItemClick(item);
    
    // Close menu if configured to do so
    if (closeOnSelection_)
    {
        hide();
    }
}

void NUIContextMenu::handleItemHover(int index)
{
    auto item = getItem(index);
    if (item && item->getType() == NUIContextMenuItem::Type::Submenu && item->getSubmenu() != nullptr)
    {
        showSubmenu(index);
    }
    else if (activeSubmenu_)
    {
        hideSubmenu();
    }
}

void NUIContextMenu::updateSize()
{
    float height = calculateMenuHeight();
    float width = 220.0f; // Clean, compact width
    
    setSize(width, height);
}

void NUIContextMenu::showSubmenu(int itemIndex)
{
    auto item = getItem(itemIndex);
    if (!item || item->getSubmenu() == nullptr) return;
    
    hideSubmenu();
    
    activeSubmenu_ = item->getSubmenu();
    submenuItemIndex_ = itemIndex;
    
    // Position submenu to the right of the current menu
    // getItemRect returns rect in parent coords (includes menu's position)
    NUIRect itemRect = getItemRect(itemIndex);
    NUIRect myBounds = getBounds();
    
    // Calculate position: right edge of menu + gap, same Y as the item
    float targetX = myBounds.x + myBounds.width + 2.0f;
    float targetY = itemRect.y;
    
    activeSubmenu_->showAt(static_cast<int>(targetX), static_cast<int>(targetY));
}

void NUIContextMenu::hideSubmenu()
{
    if (activeSubmenu_)
    {
        activeSubmenu_->hide();
        activeSubmenu_ = nullptr;
        submenuItemIndex_ = -1;
    }
}

void NUIContextMenu::triggerItemClick(std::shared_ptr<NUIContextMenuItem> item)
{
    if (item->getOnClick())
    {
        item->getOnClick()();
    }
    
    if (onItemClickCallback_)
    {
        onItemClickCallback_(item);
    }
}

void NUIContextMenu::triggerShow()
{
    if (onShowCallback_)
    {
        onShowCallback_();
    }
}

void NUIContextMenu::triggerHide()
{
    if (onHideCallback_)
    {
        onHideCallback_();
    }
}

} // namespace NomadUI
