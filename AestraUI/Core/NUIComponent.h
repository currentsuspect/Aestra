// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUITypes.h"
#include <vector>
#include <memory>
#include <string>

namespace AestraUI {

class NUIRenderer;
class NUITheme;
struct NUIThemeProperties;
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
    const void* owner = nullptr;
    bool active = false;
    bool immediate = false;
    float alpha = 0.0f;
    float delayTimer = 0.0f;
    float dismissGraceTimer = 0.0f;
};

/**
 * Base class for all UI components in the Aestra UI framework.
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

    // Event-dispatch guard. While a mouse-event dispatch is in flight (bracketed
    // by NUIApp around the root onMouseEvent), addChild()/removeChild() defer the
    // actual hierarchy mutation until dispatch unwinds. This prevents callbacks
    // that open or close popups from mutating a parent's children_ mid-iteration
    // (iterator invalidation) or freeing a component still on the call stack
    // (use-after-free). Nestable via an internal depth counter.
    static void beginEventDispatch();
    static void endEventDispatch();

    /**
     * @brief Moves this component to the top of its parent's children list (rendered last, receives events first)
     */
    void bringToFront(); 
    
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

    // Drag-and-drop target eligibility only — NOT mouse dispatch.
    //
    // isComponentEligibleForDragTarget() (NUIDragDrop.cpp) walks the ancestor
    // chain and rejects a drop target if any node has this cleared. Mouse event
    // routing never consults it: onMouseEvent() gates on visible_/enabled_, and
    // the parent-side filter added in #674 gates on isVisible(). The name reads
    // like a general hit-testing switch, which is misleading in both directions —
    // clearing it does not make a component click-through, and setting it does not
    // make one clickable. UIRoutingMap's constructor used to call the setter with
    // `true`, its own default, evidently expecting some effect; that no-op is gone.
    //
    // Nothing in the tree currently clears it, so the drag-eligibility check above
    // is inert today. Left in place because it is a functioning hook with a real
    // consumer, not dead code (#672 checklist: decided keep, on that evidence).
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
    /** Refresh cached theme state, then propagate the change to descendants. */
    virtual void onThemeChanged(const NUIThemeProperties& theme);
    
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
    static bool s_cursorCaptureActive;
    
    std::shared_ptr<NUITheme> theme_;
    
public:
    // Tooltips
    void setTooltip(const std::string& text);
    std::string getTooltip() const { return tooltipText_; }
    
    // Global Tooltip Management
    static void showRemoteTooltip(const std::string& text, const NUIPoint& position, const void* owner = nullptr, bool force = false);
    static void hideRemoteTooltip(const void* owner = nullptr);
    static void renderGlobalTooltip(NUIRenderer& renderer, const NUIRect& viewport = {});
    static void updateGlobalTooltip(double deltaTime);
    static NUIRect calculateTooltipBounds(const NUIPoint& anchor, const NUISize& tooltipSize,
                                          const NUIRect& viewport);
    static const TooltipState& getGlobalTooltipState() { return s_tooltipState; }
    static void setCursorCaptureActive(bool active);
    static bool isCursorCaptureActive() { return s_cursorCaptureActive; }

};

} // namespace AestraUI
