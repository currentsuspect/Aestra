// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
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
 * Base class for all UI components in the Nomad UI framework.
 * 
 * Design principles:
 * - Lightweight and fast
 * - GPU-accelerated rendering
 * - Event-driven architecture
 * - Composable hierarchy
 * 
 * âš ï¸ CRITICAL COORDINATE SYSTEM WARNING:
 * NomadUI uses ABSOLUTE screen coordinates for all components.
 * Child components are NOT automatically positioned relative to their parent.
 * 
 * When positioning children:
 * 1. Always add parent's X,Y offset: child->setBounds(parentX + offsetX, parentY + offsetY, w, h)
 * 2. Never reset to (0,0) in onResize() - preserve the X,Y position
 * 3. Use getBounds() to get current absolute position
 * 
 * See docs/COORDINATE_SYSTEM_QUICK_REF.md for quick reference
 * See NomadDocs/NOMADUI_COORDINATE_SYSTEM.md for full documentation
 */
class NUIComponent : public std::enable_shared_from_this<NUIComponent> {
public:
    NUIComponent();
    virtual ~NUIComponent();
    
    // ========================================================================
    // Lifecycle
    // ========================================================================
    
    /**
     * Called every frame to render the component.
     * Override this to implement custom rendering.
     */
    virtual void onRender(NUIRenderer& renderer);
    
    /**
     * Called every frame to update component state.
     * Use this for animations, timers, etc.
     */
    virtual void onUpdate(double deltaTime);
    
    /**
     * Called when the component is resized.
     */
    virtual void onResize(int width, int height);
    
    // ========================================================================
    // Event Handling
    // ========================================================================
    
    /**
     * Called when a mouse event occurs within this component.
     * Return true if the event was handled (stops propagation).
     */
    virtual bool onMouseEvent(const NUIMouseEvent& event);
    
    /**
     * Called when a key event occurs and this component has focus.
     * Return true if the event was handled.
     */
    virtual bool onKeyEvent(const NUIKeyEvent& event);
    
    /**
     * Called when the component gains focus.
     */
    virtual void onFocusGained();
    
    /**
     * Called when the component loses focus.
     */
    virtual void onFocusLost();
    
    /**
     * Called when the mouse enters the component bounds.
     */
    virtual void onMouseEnter();
    
    /**
     * Called when the mouse leaves the component bounds.
     */
    virtual void onMouseLeave();
    
    
    // ========================================================================
    // Layout & Bounds
    // ========================================================================
    
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
    
    // ========================================================================
    // Hierarchy
    // ========================================================================
    
    void addChild(std::shared_ptr<NUIComponent> child);
    void removeChild(std::shared_ptr<NUIComponent> child);
    void removeAllChildren();
    
    NUIComponent* getParent() const { return parent_; }
    const std::vector<std::shared_ptr<NUIComponent>>& getChildren() const { return children_; }
    
    /**
     * Find a child component by ID.
     */
    std::shared_ptr<NUIComponent> findChildById(const std::string& id);
    
    /**
     * Convert local coordinates to global (screen) coordinates.
     */
    NUIPoint localToGlobal(const NUIPoint& local) const;
    
    /**
     * Convert global (screen) coordinates to local coordinates.
     */
    NUIPoint globalToLocal(const NUIPoint& global) const;
    
    // ========================================================================
    // State
    // ========================================================================
    
    void setVisible(bool visible);
    bool isVisible() const { return visible_; }
    
    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled_; }
    
    void setFocused(bool focused);
    bool isFocused() const { return focused_; }
    
    void setHovered(bool hovered);
    bool isHovered() const { return hovered_; }
    
    void setId(const std::string& id) { id_ = id; }
    std::string getId() const { return id_; }
    
    void setLayer(NUILayer layer) { layer_ = layer; }
    NUILayer getLayer() const { return layer_; }
    
    // ========================================================================
    // Rendering State
    // ========================================================================
    
    /**
     * Mark this component as needing a redraw.
     */
    void setDirty(bool dirty = true);
    bool isDirty() const { return dirty_; }
    
    /**
     * Convenience method to trigger a repaint.
     */
    void repaint() { setDirty(true); }
    
    /**
     * Set the opacity of this component (0.0 = transparent, 1.0 = opaque).
     */
    void setOpacity(float opacity);
    float getOpacity() const { return opacity_; }
    
    // ========================================================================
    // Theme
    // ========================================================================
    
    void setTheme(std::shared_ptr<NUITheme> theme);
    std::shared_ptr<NUITheme> getTheme() const;
    
    // ========================================================================
    // Callbacks
    // ========================================================================
    
    NUIMouseCallback onMouseDown;
    NUIMouseCallback onMouseUp;
    NUIMouseCallback onMouseMove;
    NUIMouseCallback onMouseWheel;
    NUIKeyCallback onKeyDown;
    NUIKeyCallback onKeyUp;
    
protected:
    /**
     * Render all children (called automatically by onRender).
     */
    void renderChildren(NUIRenderer& renderer);
    
    /**
     * Update all children (called automatically by onUpdate).
     */
    void updateChildren(double deltaTime);
    
    /**
     * Check if a point is within this component's bounds.
     */
    bool containsPoint(const NUIPoint& point) const;
    
    /**
     * Find the topmost child at the given point.
     */
    std::shared_ptr<NUIComponent> findChildAt(const NUIPoint& point);
    
private:
    // Layout
    NUIRect bounds_;
    
    // Hierarchy
    NUIComponent* parent_ = nullptr;
    std::vector<std::shared_ptr<NUIComponent>> children_;
    
    // State
    std::string id_;
    bool visible_ = true;
    bool enabled_ = true;
    bool focused_ = false;
    bool hovered_ = false;
    bool dirty_ = true;
    float opacity_ = 1.0f;
    NUILayer layer_ = NUILayer::Content;
    
    // Theme
    std::shared_ptr<NUITheme> theme_;
};

} // namespace NomadUI
