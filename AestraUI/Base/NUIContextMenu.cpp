// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUIContextMenu.h"
#include "NUIScrollbar.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include <algorithm>

#ifdef _WIN32
#include <Windows.h>
#endif
#include <cmath>
#include <iostream>

namespace AestraUI {

namespace {
/** @brief Narrowest a menu is drawn, so a two-word menu still reads as a menu. */
constexpr float kMenuMinWidth = 132.0f;
/** @brief Widest, before long labels would start dominating the window. */
constexpr float kMenuMaxWidth = 380.0f;
/** @brief Clear space between a label and its shortcut. */
constexpr float kMenuShortcutGap = 28.0f;
constexpr float kMenuIndicatorSize = 14.0f;
constexpr float kMenuArrowSize = 12.0f;
} // namespace


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
    
    // Apply Aestra theme colors - using the new layered system
    auto& themeManager = NUIThemeManager::getInstance();
    backgroundColor_ = themeManager.getColor("elevatedPanel");
    borderColor_ = themeManager.getColor("borderStrong");
    textColor_ = themeManager.getColor("textPrimary");                // #E5E5E8 - Main text
    hoverColor_ = themeManager.getColor("controlHover");
    separatorColor_ = themeManager.getColor("borderSubtle");          // #2c2c2f - Subtle dividers
    shortcutColor_ = themeManager.getColor("textSecondary");          // #A6A6AA - Muted shortcuts
}

void NUIContextMenu::onRender(NUIRenderer& renderer)
{
    if (!isVisible()) return;

    if (needsMeasure_) measureAndFit(renderer);

    drawBackground(renderer);

    // Tall menus keep their items inside the popup surface. Submenus are
    // rendered after the clip is cleared so they remain independent popups.
    const NUIRect bounds = getBounds();
    renderer.setClipRect({bounds.x + borderWidth_, bounds.y + borderWidth_,
                          std::max(0.0f, bounds.width - borderWidth_ * 2.0f),
                          std::max(0.0f, bounds.height - borderWidth_ * 2.0f)});
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
    renderer.clearClipRect();

    const float contentHeight = calculateContentHeight();
    if (scrollable_ && contentHeight > bounds.height) {
        // Same painter as every other scrollbar in the DAW (spec 2 §2).
        const float sbW = kOverlayScrollbarThickness;
        const NUIRect gutter(bounds.right() - sbW - 2.0f, bounds.y + 4.0f, sbW,
                             std::max(1.0f, bounds.height - 8.0f));
        const float thumbHeight =
            std::max(kOverlayScrollbarMinThumb, gutter.height * (bounds.height / contentHeight));
        const float travel = std::max(0.0f, gutter.height - thumbHeight);
        const float progress = maximumScrollOffset() > 0.0f ? scrollOffset_ / maximumScrollOffset() : 0.0f;
        drawOverlayScrollbar(renderer, gutter, NUIRect(gutter.x, gutter.y + travel * progress, sbW, thumbHeight),
                             ScrollbarPaintState{});
    }

    // Render active submenu on top
    if (activeSubmenu_ && activeSubmenu_->isVisible())
    {
        activeSubmenu_->onRender(renderer);
    }
}

void NUIContextMenu::onThemeChanged(const NUIThemeProperties& theme)
{
    if (!customColors_) {
        backgroundColor_ = theme.surfaceTertiary;
        borderColor_ = theme.borderStrong;
        textColor_ = theme.textPrimary;
        hoverColor_ = theme.buttonBgHover;
        separatorColor_ = theme.borderSubtle;
        shortcutColor_ = theme.textSecondary;
        borderRadius_ = theme.radiusM;
        itemHeight_ = theme.layout.standardMenuRowHeight;
        iconSize_ = theme.layout.standardIconSize;
        updateSize();
    }
    NUIComponent::onThemeChanged(theme);
}

bool NUIContextMenu::onMouseEvent(const NUIMouseEvent& event)
{
    if (!isVisible() || !isEnabled()) return false;

    // This UI tree lays popup/menu bounds in the same absolute/window space
    // as incoming mouse events, so hit-test against raw bounds here.
    NUIRect menuBounds = getBounds();
    
    // Give priority to submenu
    if (activeSubmenu_ && activeSubmenu_->isVisible())
    {
        if (activeSubmenu_->onMouseEvent(event))
        {
            return true;
        }
    }

    // Check if mouse is within our global bounds
    if (!menuBounds.contains(event.position)) {
        if (event.pressed) hide(); // dismiss on click outside, don't consume the event
        return false;
    }

    if (event.wheelDelta != 0.0f && scrollable_ && maximumScrollOffset() > 0.0f) {
        scrollOffset_ += event.wheelDelta > 0.0f ? -itemHeight_ : itemHeight_;
        clampScrollOffset();
        NUIPoint localPos = event.position;
        localPos.x -= menuBounds.x;
        localPos.y -= menuBounds.y;
        hoveredItemIndex_ = getItemAtPosition(localPos);
        setDirty(true);
        return true;
    }

    // Convert absolute/window position to menu-local for item lookup
    NUIPoint localPos = event.position;
    localPos.x -= menuBounds.x;
    localPos.y -= menuBounds.y;

    int itemIndex = getItemAtPosition(localPos);

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
    else if (event.button == NUIMouseButton::None)
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

    return true;
}

bool NUIContextMenu::onKeyEvent(const NUIKeyEvent& event)
{
    if (!isVisible() || !isEnabled()) return false;

    if (event.pressed)
    {
        switch (event.keyCode)
        {
            case NUIKeyCode::Escape:
                hide();
                return true;
            case NUIKeyCode::Up:
                navigateUp();
                return true;
            case NUIKeyCode::Down:
                navigateDown();
                return true;
            case NUIKeyCode::Home:
                hoveredItemIndex_ = findSelectableItem(0, 1);
                ensureItemVisible(hoveredItemIndex_);
                setDirty(true);
                return true;
            case NUIKeyCode::End:
                hoveredItemIndex_ = findSelectableItem(getItemCount() - 1, -1);
                ensureItemVisible(hoveredItemIndex_);
                setDirty(true);
                return true;
            case NUIKeyCode::Tab:
                if (event.modifiers & NUIModifiers::Shift) navigateUp();
                else navigateDown();
                return true;
            case NUIKeyCode::Right:
                if (isSelectableItem(hoveredItemIndex_)) {
                    auto item = getItem(hoveredItemIndex_);
                    if (item && item->getType() == NUIContextMenuItem::Type::Submenu && item->getSubmenu()) {
                        showSubmenu(hoveredItemIndex_);
                    }
                }
                return true;
            case NUIKeyCode::Left:
                if (auto previous = previousFocus_.lock()) {
                    if (dynamic_cast<NUIContextMenu*>(previous.get())) {
                        hide();
                    }
                }
                return true;
            case NUIKeyCode::Enter:
            case NUIKeyCode::Space:
                if (isSelectableItem(hoveredItemIndex_))
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
    scrollOffset_ = 0.0f;
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
    scrollOffset_ = 0.0f;
    requestedPosition_ = NUIPoint(static_cast<float>(x), static_cast<float>(y));
    // Fit against the provisional size now so the menu is roughly placed even
    // before a renderer has measured it; measureAndFit() repeats this with real
    // metrics at the top of the first paint.
    fitInsideParent(NUISize(getBounds().width, getBounds().height));

    if (!isVisible_) {
        previousFocus_.reset();
        if (auto* focused = NUIComponent::getFocusedComponent(); focused && focused != this) {
            try {
                previousFocus_ = focused->shared_from_this();
            } catch (const std::bad_weak_ptr&) {
                // Stack-owned focus targets cannot be restored safely.
            }
        }
    }

    isVisible_ = true;
    hoveredItemIndex_ = findSelectableItem(0, 1);
    ensureItemVisible(hoveredItemIndex_);
    pressedItemIndex_ = -1;
    setFocused(true);
    triggerShow();
    setDirty(true);
}

void NUIContextMenu::hide()
{
    const bool ownedFocus = ownsMenuFocus();
    auto previousFocus = previousFocus_.lock();
    // Mark the parent hidden before cascading so a submenu cannot restore focus
    // to an already-hidden menu.
    isVisible_ = false;
    hoveredItemIndex_ = -1;
    pressedItemIndex_ = -1;
    hideSubmenu();
    triggerHide();
    if (ownedFocus) {
        setFocused(false);
        if (previousFocus && previousFocus.get() != this && previousFocus->isVisible() && previousFocus->isEnabled()) {
            previousFocus->setFocused(true);
        }
    }
    previousFocus_.reset();
    setDirty(true);
}

void NUIContextMenu::setBackgroundColor(const NUIColor& color)
{
    backgroundColor_ = color;
    customColors_ = true;
    setDirty(true);
}

void NUIContextMenu::setBorderColor(const NUIColor& color)
{
    borderColor_ = color;
    customColors_ = true;
    setDirty(true);
}

void NUIContextMenu::setTextColor(const NUIColor& color)
{
    textColor_ = color;
    customColors_ = true;
    setDirty(true);
}

void NUIContextMenu::setHoverColor(const NUIColor& color)
{
    hoverColor_ = color;
    customColors_ = true;
    setDirty(true);
}

void NUIContextMenu::setSeparatorColor(const NUIColor& color)
{
    separatorColor_ = color;
    customColors_ = true;
    setDirty(true);
}

void NUIContextMenu::setShortcutColor(const NUIColor& color)
{
    shortcutColor_ = color;
    customColors_ = true;
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
    maxHeight_ = std::max(1.0f, height);
    clampScrollOffset();
    updateLayout();
    setDirty(true);
}

void NUIContextMenu::setScrollable(bool scrollable)
{
    scrollable_ = scrollable;
    if (!scrollable_) scrollOffset_ = 0.0f;
    clampScrollOffset();
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
    const int start = hoveredItemIndex_ < 0 ? getItemCount() - 1 : hoveredItemIndex_ - 1;
    const int next = findSelectableItem(start, -1);
    if (next >= 0) {
        hoveredItemIndex_ = next;
        ensureItemVisible(next);
        setDirty(true);
    }
}

void NUIContextMenu::navigateDown()
{
    const int start = hoveredItemIndex_ < 0 ? 0 : hoveredItemIndex_ + 1;
    const int next = findSelectableItem(start, 1);
    if (next >= 0) {
        hoveredItemIndex_ = next;
        ensureItemVisible(next);
        setDirty(true);
    }
}

bool NUIContextMenu::isSelectableItem(int index) const
{
    auto item = getItem(index);
    return item && item->isVisible() && item->isEnabled() &&
           item->getType() != NUIContextMenuItem::Type::Separator;
}

int NUIContextMenu::findSelectableItem(int startIndex, int direction) const
{
    if (direction == 0 || getItemCount() == 0) return -1;
    for (int index = startIndex; index >= 0 && index < getItemCount(); index += direction) {
        if (isSelectableItem(index)) return index;
    }
    return -1;
}

bool NUIContextMenu::ownsMenuFocus() const
{
    if (NUIComponent::getFocusedComponent() == this) return true;
    return activeSubmenu_ && activeSubmenu_->ownsMenuFocus();
}

float NUIContextMenu::calculateContentHeight() const
{
    float height = 0.0f;
    for (const auto& item : items_) {
        if (!item || !item->isVisible()) continue;
        height += item->getType() == NUIContextMenuItem::Type::Separator ? 8.0f : itemHeight_;
    }
    return height;
}

float NUIContextMenu::maximumScrollOffset() const
{
    if (!scrollable_) return 0.0f;
    return std::max(0.0f, calculateContentHeight() - getBounds().height);
}

void NUIContextMenu::clampScrollOffset()
{
    scrollOffset_ = std::clamp(scrollOffset_, 0.0f, maximumScrollOffset());
}

void NUIContextMenu::ensureItemVisible(int index)
{
    if (!scrollable_ || index < 0 || index >= getItemCount()) return;
    const NUIRect itemRect = getItemRect(index);
    const NUIRect bounds = getBounds();
    if (itemRect.y < bounds.y) {
        scrollOffset_ -= bounds.y - itemRect.y;
    } else if (itemRect.bottom() > bounds.bottom()) {
        scrollOffset_ += itemRect.bottom() - bounds.bottom();
    }
    clampScrollOffset();
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
    
    auto& themeManager = NUIThemeManager::getInstance();
    const auto& props = themeManager.getCurrentTheme();
    if (index == pressedItemIndex_ && item->isEnabled()) {
        renderer.fillRoundedRect(itemRect, props.radiusS, themeManager.getColor("controlPressed"));
    } else if (index == hoveredItemIndex_ && item->isEnabled()) {
        renderer.fillRoundedRect(itemRect, props.radiusS, hoverColor_);
    }
    
    // Draw item content
    float x = itemRect.x + itemPadding_ + 4.0f;
    const float itemFontSize = props.fontSizeM;
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
                // Selected: Solid filled "dot" in Aestra Purple
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
    NUIColor textColor = item->isEnabled() ? textColor_ : themeManager.getColor("textDisabled");
    renderer.drawText(item->getText(), NUIPoint(x, y), itemFontSize, textColor);
    
    // Draw shortcut
    if (!item->getShortcut().empty())
    {
        const float shortcutFontSize = props.fontSizeS;
        // Right-aligned against the measured width rather than a fixed 60px
        // reservation, which only landed correctly at the old fixed menu width.
        float shortcutX = itemRect.right() - itemPadding_ -
                          renderer.measureText(item->getShortcut(), shortcutFontSize).width;
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
    float y = bounds.y - scrollOffset_;
    
    // Calculate Y position accounting for separator heights
    for (int i = 0; i < index; ++i)
    {
        auto item = getItem(i);
        if (!item || !item->isVisible()) {
            continue;
        }
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
    const float contentHeight = calculateContentHeight();
    if (!scrollable_ || contentHeight <= maxHeight_) return contentHeight;

    // Clamp to whole rows. A raw min() left the last visible item sliced in half
    // at the menu's bottom edge, which reads as clipping rather than as "there is
    // more below".
    const float rows = std::floor(maxHeight_ / std::max(1.0f, itemHeight_));
    return std::max(itemHeight_, rows * itemHeight_);
}

int NUIContextMenu::getItemAtPosition(const NUIPoint& position) const
{
    // position is now in local coordinates (origin at menu's top-left)
    NUIRect localBounds = getBounds();
    NUIRect localRect(0, 0, localBounds.width, localBounds.height);
    if (!localRect.contains(position)) return -1;
    
    float relativeY = position.y + scrollOffset_;
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

// A menu's width is its content's width. Until a renderer has measured the text
// (measureAndFit, first onRender) this is the provisional estimate that keeps
// showAt()'s edge clamping roughly right; it is never what gets drawn, because
// measurement happens before this menu paints anything.
float NUIContextMenu::estimateItemWidth(const std::shared_ptr<NUIContextMenuItem>& item) const
{
    if (!item) return 0.0f;
    const float em = NUIThemeManager::getInstance().getCurrentTheme().fontSizeM;
    float w = item->getText().size() * em * 0.55f;
    if (!item->getShortcut().empty()) w += item->getShortcut().size() * em * 0.46f + kMenuShortcutGap;
    return w;
}

void NUIContextMenu::updateSize()
{
    float widest = 0.0f;
    for (const auto& item : items_) {
        if (!item || !item->isVisible()) continue;
        widest = std::max(widest, estimateItemWidth(item) + decorationWidthFor(item));
    }
    setSize(std::clamp(widest + itemPadding_ * 2.0f + 8.0f, kMenuMinWidth, kMenuMaxWidth),
            calculateMenuHeight());
    clampScrollOffset();
    needsMeasure_ = true;
}

// Everything on a row that is not the label or the shortcut: icon, check/radio
// indicator, submenu chevron. Shared by the estimate and the real measurement so
// the two cannot disagree about what a row has to fit.
float NUIContextMenu::decorationWidthFor(const std::shared_ptr<NUIContextMenuItem>& item) const
{
    if (!item) return 0.0f;
    float w = 4.0f; // drawItem's leading nudge past the padding
    if (item->getIconObject()) w += iconSize_ + itemPadding_ * 0.5f;
    if (item->getType() == NUIContextMenuItem::Type::Checkbox ||
        item->getType() == NUIContextMenuItem::Type::Radio) {
        w += kMenuIndicatorSize + itemPadding_;
    }
    if (item->getType() == NUIContextMenuItem::Type::Submenu) w += kMenuArrowSize + 4.0f;
    return w;
}

void NUIContextMenu::measureAndFit(NUIRenderer& renderer)
{
    needsMeasure_ = false;

    const auto& props = NUIThemeManager::getInstance().getCurrentTheme();
    float widest = 0.0f;
    for (const auto& item : items_) {
        if (!item || !item->isVisible()) continue;
        if (item->getType() == NUIContextMenuItem::Type::Separator) continue;

        float row = renderer.measureText(item->getText(), props.fontSizeM).width;
        if (!item->getShortcut().empty()) {
            row += kMenuShortcutGap + renderer.measureText(item->getShortcut(), props.fontSizeS).width;
        }
        widest = std::max(widest, row + decorationWidthFor(item));
    }

    const float width = std::clamp(widest + itemPadding_ * 2.0f + 8.0f, kMenuMinWidth, kMenuMaxWidth);
    fitInsideParent(NUISize(width, calculateMenuHeight()));
}

void NUIContextMenu::fitInsideParent(const NUISize& size)
{
    float width = size.width;
    float height = size.height;
    float posX = requestedPosition_.x;
    float posY = requestedPosition_.y;

    if (NUIComponent* parent = getParent()) {
        const NUIRect parentBounds = parent->getBounds();
        // Width yields before height: a menu that has to shrink horizontally is
        // still usable, whereas one pushed off-screen is not.
        width = std::min(width, std::max(kMenuMinWidth, parentBounds.width - 20.0f));
        const float availableHeight = std::max(1.0f, parentBounds.height - 20.0f);
        height = std::min(height, availableHeight);

        if (posX + width > parentBounds.right()) posX = parentBounds.right() - width - 10.0f;
        if (posY + height > parentBounds.bottom()) posY = parentBounds.bottom() - height - 10.0f;
        posX = std::max(posX, parentBounds.x + 10.0f);
        posY = std::max(posY, parentBounds.y + 10.0f);
    }

    setSize(width, height);
    setPosition(posX, posY);
    clampScrollOffset();
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

    // Submenus are rendered by their owning menu rather than attached as root
    // children, so constrain them explicitly to the root viewport. Prefer
    // opening left when the right edge has no room.
    if (auto* viewportOwner = getParent()) {
        const NUIRect viewport = viewportOwner->getBounds();
        NUIRect submenuBounds = activeSubmenu_->getBounds();
        const float availableHeight = std::max(1.0f, viewport.height - 20.0f);
        if (submenuBounds.height > availableHeight) {
            activeSubmenu_->setSize(submenuBounds.width, availableHeight);
            activeSubmenu_->clampScrollOffset();
            submenuBounds = activeSubmenu_->getBounds();
        }
        if (targetX + submenuBounds.width > viewport.right()) {
            targetX = myBounds.x - submenuBounds.width - 2.0f;
        }
        if (targetY + submenuBounds.height > viewport.bottom()) {
            targetY = viewport.bottom() - submenuBounds.height - 10.0f;
        }
        targetX = std::max(viewport.x + 10.0f, targetX);
        targetY = std::max(viewport.y + 10.0f, targetY);
        activeSubmenu_->setPosition(targetX, targetY);
    }
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

} // namespace AestraUI
