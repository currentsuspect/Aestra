// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"
#include "NUIIcon.h"
#include <functional>
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace AestraUI {

// Forward declaration
class NUIContextMenu;

/**
 * NUIContextMenuItem - A single item in a context menu
 */
class NUIContextMenuItem
{
public:
    enum class Type
    {
        Normal,     // Regular menu item
        Separator,  // Visual separator
        Submenu,    // Item with submenu
        Checkbox,   // Checkable item
        Radio       // Radio button item (exclusive selection)
    };

    NUIContextMenuItem(const std::string& text = "", Type type = Type::Normal);
    ~NUIContextMenuItem() = default;

    // Properties
    void setText(const std::string& text);
    const std::string& getText() const { return text_; }

    void setType(Type type);
    Type getType() const { return type_; }

    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled_; }

    void setVisible(bool visible);
    bool isVisible() const { return visible_; }

    void setChecked(bool checked);
    bool isChecked() const { return checked_; }

    void setShortcut(const std::string& shortcut);
    const std::string& getShortcut() const { return shortcut_; }

    /** @brief A verb that destroys something (Delete…): drawn in the error colour, listed last behind a divider. */
    /** @brief A 4 px colour swatch before the label (a channel's colour in the route list). */
    void setSwatch(const NUIColor& color) { swatch_ = color; hasSwatch_ = true; }
    bool hasSwatch() const { return hasSwatch_; }
    const NUIColor& getSwatch() const { return swatch_; }

    void setDestructive(bool destructive) { destructive_ = destructive; }
    bool isDestructive() const { return destructive_; }

    void setIcon(const std::string& iconPath);
    const std::string& getIcon() const { return iconPath_; }
    
    void setIconObject(std::shared_ptr<NUIIcon> icon);
    std::shared_ptr<NUIIcon> getIconObject() const { return icon_; }

    // Submenu
    void setSubmenu(std::shared_ptr<NUIContextMenu> submenu);
    std::shared_ptr<NUIContextMenu> getSubmenu() const { return submenu_; }

    // Callbacks
    void setOnClick(std::function<void()> callback);
    std::function<void()> getOnClick() const { return onClickCallback_; }

    // Radio group
    void setRadioGroup(const std::string& group);
    const std::string& getRadioGroup() const { return radioGroup_; }

private:
    std::string text_;
    Type type_ = Type::Normal;
    bool enabled_ = true;
    bool visible_ = true;
    bool checked_ = false;
    bool destructive_ = false;
    bool hasSwatch_ = false;
    NUIColor swatch_{};
    std::string shortcut_;
    std::string iconPath_;
    std::shared_ptr<NUIIcon> icon_;
    std::shared_ptr<NUIContextMenu> submenu_;
    std::function<void()> onClickCallback_;
    std::string radioGroup_;
};

/**
 * NUIContextMenu - A context menu component
 * Supports hierarchical menus, shortcuts, icons, and various item types
 * Replaces juce::PopupMenu with AestraUI styling and theming
 */
class NUIContextMenu : public NUIComponent
{
public:
    NUIContextMenu();
    ~NUIContextMenu() override = default;

    // Component interface
    void onRender(NUIRenderer& renderer) override;
    void onThemeChanged(const NUIThemeProperties& theme) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    bool onKeyEvent(const NUIKeyEvent& event) override;
    void onMouseEnter() override;
    void onMouseLeave() override;

    // Menu management
    /**
     * @brief Append an item; a submenu on it inherits this menu's bound owner.
     *
     * Every add path funnels through here, so submenu owner inheritance does
     * not depend on whether the submenu was attached before or after setOwner().
     */
    void addItem(std::shared_ptr<NUIContextMenuItem> item);
    void addItem(const std::string& text, std::function<void()> callback = nullptr);
    void addSeparator();
    void addSubmenu(const std::string& text, std::shared_ptr<NUIContextMenu> submenu);
    void addCheckbox(const std::string& text, bool checked, std::function<void(bool)> callback = nullptr);
    void addRadioItem(const std::string& text, const std::string& group, bool selected, std::function<void()> callback = nullptr);
    void clear();

    // Menu display
    void showAt(const NUIPoint& position);
    void showAt(int x, int y);
    void hide();
    bool isVisible() const { return isVisible_; }

    // Visual properties
    void setBackgroundColor(const NUIColor& color);
    NUIColor getBackgroundColor() const { return backgroundColor_; }

    void setBorderColor(const NUIColor& color);
    NUIColor getBorderColor() const { return borderColor_; }

    void setTextColor(const NUIColor& color);
    NUIColor getTextColor() const { return textColor_; }

    void setHoverColor(const NUIColor& color);
    NUIColor getHoverColor() const { return hoverColor_; }

    void setSeparatorColor(const NUIColor& color);
    NUIColor getSeparatorColor() const { return separatorColor_; }

    void setShortcutColor(const NUIColor& color);
    NUIColor getShortcutColor() const { return shortcutColor_; }

    void setBorderWidth(float width);
    float getBorderWidth() const { return borderWidth_; }

    void setBorderRadius(float radius);
    float getBorderRadius() const { return borderRadius_; }

    void setItemHeight(float height);
    float getItemHeight() const { return itemHeight_; }

    void setItemPadding(float padding);
    float getItemPadding() const { return itemPadding_; }

    void setIconSize(float size);
    float getIconSize() const { return iconSize_; }

    // Menu behavior
    void setAutoHide(bool autoHide);
    bool isAutoHide() const { return autoHide_; }

    void setCloseOnSelection(bool close);
    bool isCloseOnSelection() const { return closeOnSelection_; }

    void setMaxHeight(float height);
    float getMaxHeight() const { return maxHeight_; }

    void setScrollable(bool scrollable);
    bool isScrollable() const { return scrollable_; }

    // Event callbacks
    void setOnShow(std::function<void()> callback);
    void setOnHide(std::function<void()> callback);
    void setOnItemClick(std::function<void(std::shared_ptr<NUIContextMenuItem>)> callback);

    /**
     * @brief Bind the component whose lifetime gates this menu's callbacks.
     *
     * Context menus are attached to the *root* component so they can draw
     * outside their owner's bounds, which means a menu outlives the component
     * that built it. Its callbacks, however, almost always capture that
     * component. Binding the owner here makes the menu drop every callback
     * once the owner dies instead of calling into freed memory.
     *
     * Passing an empty or already-expired handle leaves the menu ungated, so
     * menus that are not owned by a component keep working unchanged.
     */
    void setOwner(std::weak_ptr<NUIComponent> owner);

    /** @brief False only when an owner was bound and has since been destroyed. */
    bool hasLiveOwner() const;

    // Utility
    std::vector<std::shared_ptr<NUIContextMenuItem>> getItems() const { return items_; }
    int getItemCount() const { return static_cast<int>(items_.size()); }
    std::shared_ptr<NUIContextMenuItem> getItem(int index) const;
    int getHoveredItemIndex() const { return hoveredItemIndex_; }
    
    // Keyboard navigation
    virtual void navigateUp();
    virtual void navigateDown();

protected:
    // Override these for custom menu appearance
    virtual void drawBackground(NUIRenderer& renderer);
    virtual void drawItem(NUIRenderer& renderer, std::shared_ptr<NUIContextMenuItem> item, int index);
    virtual void drawSeparator(NUIRenderer& renderer, int index);
    virtual void drawSubmenuArrow(NUIRenderer& renderer, int index);

    // Layout
    virtual void updateLayout();
    /**
     * @brief Resolve the menu's width from its own content, then re-fit it.
     *
     * Needs a renderer because text width is a font-metric question, so it runs
     * at the top of the first onRender() after the items change rather than in
     * showAt(). That is the same frame the menu first appears in, and it happens
     * before any of this menu's own drawing, so nothing mis-sized is ever shown.
     */
    void measureAndFit(NUIRenderer& renderer);

    /** @brief Clamp @p size into the parent viewport and position it there. */
    void fitInsideParent(const NUISize& size);

    /** @brief Width of everything on a row that is not the label or the shortcut. */
    /** @brief True when any visible row has an icon or a check column, so every row reserves the same leading slot. */
    bool hasLeadingSlot() const;
    float decorationWidthFor(const std::shared_ptr<NUIContextMenuItem>& item) const;

    /** @brief Renderer-free width guess, used only until measureAndFit() runs. */
    float estimateItemWidth(const std::shared_ptr<NUIContextMenuItem>& item) const;
    virtual NUIRect getItemRect(int index) const;
    virtual float calculateMenuHeight() const;

    // Interaction
    virtual int getItemAtPosition(const NUIPoint& position) const;
    virtual void handleItemClick(int index);
    virtual void handleItemHover(int index);

private:
    void updateSize();
    void showSubmenu(int itemIndex);
    void hideSubmenu();
    void triggerItemClick(std::shared_ptr<NUIContextMenuItem> item);
    void triggerShow();
    void triggerHide();
    int findSelectableItem(int startIndex, int direction) const;
    bool isSelectableItem(int index) const;
    bool ownsMenuFocus() const;
    float calculateContentHeight() const;
    float maximumScrollOffset() const;
    void clampScrollOffset();
    void ensureItemVisible(int index);

    // Menu items
    std::vector<std::shared_ptr<NUIContextMenuItem>> items_;

    // Visual properties
    bool exactPlacement_ = false; // a submenu is placed against its parent row; a top-level menu is nudged clear of the pointer
    NUIColor backgroundColor_ = NUIColor::fromHex(0xff2a2d32);
    NUIColor borderColor_ = NUIColor::fromHex(0xff666666);
    NUIColor textColor_ = NUIColor::fromHex(0xffffffff);
    NUIColor hoverColor_ = NUIColor::fromHex(0xff3a3d42);
    NUIColor separatorColor_ = NUIColor::fromHex(0xff666666);
    NUIColor shortcutColor_ = NUIColor::fromHex(0xff888888);
    bool customColors_ = false;
    float borderWidth_ = 1.0f;
    float borderRadius_ = 7.0f;
    float itemHeight_ = 28.0f;
    float itemPadding_ = 10.0f;
    float iconSize_ = 16.0f;

    // Menu behavior
    bool autoHide_ = true;
    bool closeOnSelection_ = true;
    float maxHeight_ = 300.0f;
    bool scrollable_ = true;

    // Interaction state
    bool isVisible_ = false;
    int hoveredItemIndex_ = -1;
    int pressedItemIndex_ = -1;
    std::shared_ptr<NUIContextMenu> activeSubmenu_;
    int submenuItemIndex_ = -1;
    std::weak_ptr<NUIComponent> previousFocus_;

    // Owner lifetime gate. ownerBound_ distinguishes "no owner" from "dead
    // owner": a default-constructed weak_ptr is already expired, so the flag
    // is what keeps un-owned menus ungated.
    std::weak_ptr<NUIComponent> owner_;
    bool ownerBound_ = false;

    // Layout state
    float menuWidth_ = 0.0f;
    float menuHeight_ = 0.0f;
    float scrollOffset_ = 0.0f;
    /** @brief Where the caller asked for the menu, before any edge clamping. */
    NUIPoint requestedPosition_;
    // A submenu is painted by the menu that owns it, so it has no parent to measure against. showSubmenu() hands it the
    // viewport and its parent menu's left edge, so that its MEASURED width (not the estimate) decides whether it opens
    // to the right or flips to the left. Without them the first paint put it back at its requested spot, off-screen.
    std::optional<NUIRect> viewport_;
    bool flipsLeft_{false};
    float flipLeftOf_{0.0f};
    /** @brief Set whenever the items change; cleared once measureAndFit() has run. */
    bool needsMeasure_ = true;

    // Callbacks
    std::function<void()> onShowCallback_;
    std::function<void()> onHideCallback_;
    std::function<void(std::shared_ptr<NUIContextMenuItem>)> onItemClickCallback_;
};

} // namespace AestraUI
