// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUITypes.h"
#include <vector>
#include <memory>
#include <string>

namespace NomadUI {

class NUIRenderer;
class NUITheme;
class NUIFont;

/**
 * Render layers for proper Z-order management
 */
enum class NUILayer {
    Background = 0,
    Content = 1,
    Overlay = 2,
    Dropdown = 3,
    Tooltip = 4,
    Modal = 5
};

/**
 * Global Tooltip State
 */
struct TooltipState {
    std::string text;
    NUIPoint position;
    NUIPoint hoverPos;  // Actual mouse position when tooltip was triggered
    bool active = false;
    float alpha = 0.0f;
    float delayTimer = 0.0f;
};

/**
 * Base class for all UI components in the Nomad UI framework.
 */
class NUIComponent : public std::enable_shared_from_this<NUIComponent> {
public:
    NUIComponent();
    virtual ~NUIComponent();
    
    // Lifecycle
    virtual void onRender(NUIRenderer& renderer);
    virtual void onUpdate(double deltaTime);
    virtual void onResize(int width, int height);
    
    // Event Handling
    virtual bool onMouseEvent(const NUIMouseEvent& event);
    virtual bool onKeyEvent(const NUIKeyEvent& event);
    virtual void onFocusGained();
    virtual void onFocusLost();
    virtual void onMouseEnter();
    virtual void onMouseLeave();
    
    // Layout & Bounds
    void setBounds(float x, float y, float width, float height);
    void setBounds(const NUIRect& bounds);
    NUIRect getBounds() const { return bounds_; }
    NUIRect getGlobalBounds() const;
    
    void setPosition(float x, float y);
    NUIPoint getPosition() const { return {bounds_.x, bounds_.y}; }
    
    void setSize(float width, float height);
    NUISize getSize() const { return {bounds_.width, bounds_.height}; }
    
    float getX() const { return bounds_.x; }
    float getY() const { return bounds_.y; }
    float getWidth() const { return bounds_.width; }
    float getHeight() const { return bounds_.height; }
    
    // Hierarchy
    void addChild(std::shared_ptr<NUIComponent> child);
    void removeChild(std::shared_ptr<NUIComponent> child);
    void removeAllChildren();
    
    NUIComponent* getParent() const { return parent_; }
    const std::vector<std::shared_ptr<NUIComponent>>& getChildren() const { return children_; }
    
    std::shared_ptr<NUIComponent> findChildById(const std::string& id);
    NUIPoint localToGlobal(const NUIPoint& local) const;
    NUIPoint globalToLocal(const NUIPoint& global) const;
    
    // State
    void setVisible(bool visible);
    bool isVisible() const { return visible_; }
    
    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled_; }
    
    void setFocused(bool focused);
    bool isFocused() const { return focused_; }

    void setHitTestVisible(bool visible) { hitTestVisible_ = visible; }
    bool isHitTestVisible() const { return hitTestVisible_; }
    
    static NUIComponent* getFocusedComponent();
    static void clearFocusedComponent();
    
    void setHovered(bool hovered);
    bool isHovered() const { return hovered_; }
    
    void setId(const std::string& id) { id_ = id; }
    std::string getId() const { return id_; }
    
    void setLayer(NUILayer layer) { layer_ = layer; }
    NUILayer getLayer() const { return layer_; }
    
    // Rendering State
    void setDirty(bool dirty = true);
    bool isDirty() const { return dirty_; }
    void repaint() { setDirty(true); }
    
    void setOpacity(float opacity);
    float getOpacity() const { return opacity_; }
    
    // Theme
    void setTheme(std::shared_ptr<NUITheme> theme);
    std::shared_ptr<NUITheme> getTheme() const;
    
    // Callbacks
    NUIMouseCallback onMouseDown;
    NUIMouseCallback onMouseUp;
    NUIMouseCallback onMouseMove;
    NUIMouseCallback onMouseWheel;
    NUIKeyCallback onKeyDown;
    NUIKeyCallback onKeyUp;
    
protected:
    void renderChildren(NUIRenderer& renderer);
    void updateChildren(double deltaTime);
    bool containsPoint(const NUIPoint& point) const;
    std::shared_ptr<NUIComponent> findChildAt(const NUIPoint& point);
    
private:
    NUIRect bounds_;
    NUIComponent* parent_ = nullptr;
    std::vector<std::shared_ptr<NUIComponent>> children_;
    
    std::string id_;
    bool visible_ = true;
    bool enabled_ = true;
    bool focused_ = false;
    bool hovered_ = false;
    bool dirty_ = true;
    float opacity_ = 1.0f;
    bool hitTestVisible_ = true;
    NUILayer layer_ = NUILayer::Content;
    
    std::string tooltipText_;
    
    // Static Global Tooltip State
    static TooltipState s_tooltipState;
    
    std::shared_ptr<NUITheme> theme_;
    
public:
    // Tooltips
    void setTooltip(const std::string& text);
    std::string getTooltip() const { return tooltipText_; }
    
    // Global Tooltip Management
    static void showRemoteTooltip(const std::string& text, const NUIPoint& position);
    static void hideRemoteTooltip();
    static void renderGlobalTooltip(NUIRenderer& renderer);
    static void updateGlobalTooltip(double deltaTime);

};

} // namespace NomadUI
