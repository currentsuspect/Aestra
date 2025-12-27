// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUIComponent.h"
#include "NUITheme.h"
#include "NUIThemeSystem.h"
#include "../Graphics/NUIRenderer.h"
#include <algorithm>
#include <iostream>

// Forward declare NUIRenderer to avoid dependency
namespace NomadUI {
    class NUIRenderer;
}

namespace NomadUI {

namespace {
    NUIComponent* g_focusedComponent = nullptr;
}

// Define static tooltip state
TooltipState NUIComponent::s_tooltipState;



NUIComponent::NUIComponent() {
}

NUIComponent::~NUIComponent() {
    if (g_focusedComponent == this) {
        g_focusedComponent = nullptr;
    }
}

// ============================================================================
// Lifecycle
// ============================================================================

void NUIComponent::onRender(NUIRenderer& renderer) {
    if (!visible_) return;
    
    // Render children
    renderChildren(renderer);
    
    // Clear dirty flag after rendering
    dirty_ = false;
}

void NUIComponent::onUpdate(double deltaTime) {
    if (!visible_) return;
    
    // Update children
    updateChildren(deltaTime);
}

void NUIComponent::onResize(int width, int height) {
    setBounds(bounds_.x, bounds_.y, static_cast<float>(width), static_cast<float>(height));
}

// ============================================================================
// Event Handling
// ============================================================================

bool NUIComponent::onMouseEvent(const NUIMouseEvent& event) {
    if (!visible_ || !enabled_) return false;

    // Store original hover state before processing
    bool wasHovered = hovered_;

    // First, let children handle the event (front to back)
    bool eventHandledByChild = false;
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if ((*it)->onMouseEvent(event)) {
            eventHandledByChild = true;
            break;  // Stop at first child that handles the event
        }
    }

    // Handle the event ourselves if no child handled it
    bool eventHandledBySelf = false;
    if (!eventHandledByChild) {
        // Handle callbacks
        if (event.pressed && onMouseDown) {
            onMouseDown(event);
            eventHandledBySelf = true;
        }

        if (event.released && onMouseUp) {
            onMouseUp(event);
            eventHandledBySelf = true;
        }

        if (onMouseMove) {
            onMouseMove(event);
        }

        if (event.wheelDelta != 0.0f && onMouseWheel) {
            onMouseWheel(event);
            eventHandledBySelf = true;
        }
    }

    // Now check hover state AFTER event propagation
    bool isWithinBounds = containsPoint(event.position);
    bool shouldBeHovered = isWithinBounds;

    // Update hover state if it changed
    if (wasHovered != shouldBeHovered) {
        // Store the mouse position when hover state changes for tooltip positioning
        if (shouldBeHovered && !tooltipText_.empty()) {
            s_tooltipState.hoverPos = event.position;
        }
        setHovered(shouldBeHovered);
    }
    
    // Update hover position continuously while hovering (for accurate tooltip placement)
    if (hovered_ && !tooltipText_.empty() && s_tooltipState.text == tooltipText_) {
        s_tooltipState.hoverPos = event.position;
    }

    return eventHandledByChild || eventHandledBySelf;
}

bool NUIComponent::onKeyEvent(const NUIKeyEvent& event) {
    if (!visible_ || !enabled_ || !focused_) return false;
    
    // Handle callbacks
    if (event.pressed && onKeyDown) {
        onKeyDown(event);
        return true;
    }
    
    if (event.released && onKeyUp) {
        onKeyUp(event);
        return true;
    }
    
    return false;
}

void NUIComponent::onFocusGained() {
    focused_ = true;
    setDirty();
}

void NUIComponent::onFocusLost() {
    focused_ = false;
    setDirty();
}

void NUIComponent::onMouseEnter() {
    hovered_ = true;
    
    // Trigger global tooltip if text is set
    if (!tooltipText_.empty()) {
        s_tooltipState.text = tooltipText_;
        
        // Get global position for tooltip using localToGlobal for accurate transformation
        // Calculate center-bottom of this component in local coords, then convert to global
        NUIPoint localCenter(bounds_.width * 0.5f, bounds_.height + 6.0f);
        NUIPoint globalPos = localToGlobal(localCenter);
        s_tooltipState.position = globalPos;
        
        s_tooltipState.active = true;
        s_tooltipState.delayTimer = 0.0f; // Reset delay
        s_tooltipState.alpha = 0.0f; // Start faded out
    }
    
    setDirty();
}

void NUIComponent::onMouseLeave() {
    hovered_ = false;
    
    // Hide tooltip if this component was the source (simple check)
    // In a complex system we might check if s_tooltipState.text == tooltipText_
    if (!tooltipText_.empty() && s_tooltipState.text == tooltipText_) {
        s_tooltipState.active = false;
    }
    
    setDirty();
}

// ============================================================================
// Layout & Bounds
// ============================================================================

void NUIComponent::setBounds(float x, float y, float width, float height) {
    if (bounds_.x != x || bounds_.y != y || 
        bounds_.width != width || bounds_.height != height) {
        bounds_ = NUIRect(x, y, width, height);
        setDirty();
        onResize(static_cast<int>(width), static_cast<int>(height));
    }
}

void NUIComponent::setBounds(const NUIRect& bounds) {
    setBounds(bounds.x, bounds.y, bounds.width, bounds.height);
}

void NUIComponent::setPosition(float x, float y) {
    setBounds(x, y, bounds_.width, bounds_.height);
}

void NUIComponent::setSize(float width, float height) {
    setBounds(bounds_.x, bounds_.y, width, height);
}

NUIRect NUIComponent::getGlobalBounds() const {
    NUIRect r = getBounds();
    const NUIComponent* p = getParent();
    while (p) {
        const NUIRect& pb = p->getBounds();
        r.x += pb.x;
        r.y += pb.y;
        p = p->getParent();
    }
    return r;
}

// ============================================================================
// Hierarchy
// ============================================================================

void NUIComponent::addChild(std::shared_ptr<NUIComponent> child) {
    if (!child) return;
    
    // Remove from previous parent
    if (child->parent_) {
        child->parent_->removeChild(child);
    }
    
    child->parent_ = this;
    children_.push_back(child);
    
    // Inherit theme if child doesn't have one
    if (!child->theme_ && theme_) {
        child->setTheme(theme_);
    }
    
    setDirty();
}

void NUIComponent::removeChild(std::shared_ptr<NUIComponent> child) {
    auto it = std::find(children_.begin(), children_.end(), child);
    if (it != children_.end()) {
        (*it)->parent_ = nullptr;
        children_.erase(it);
        setDirty();
    }
}

void NUIComponent::removeAllChildren() {
    for (auto& child : children_) {
        child->parent_ = nullptr;
    }
    children_.clear();
    setDirty();
}

std::shared_ptr<NUIComponent> NUIComponent::findChildById(const std::string& id) {
    for (auto& child : children_) {
        if (child->getId() == id) {
            return child;
        }
        
        // Recursive search
        auto found = child->findChildById(id);
        if (found) {
            return found;
        }
    }
    return nullptr;
}

NUIPoint NUIComponent::localToGlobal(const NUIPoint& local) const {
    NUIPoint global = local;
    global.x += bounds_.x;
    global.y += bounds_.y;
    
    if (parent_) {
        global = parent_->localToGlobal(global);
    }
    
    return global;
}

NUIPoint NUIComponent::globalToLocal(const NUIPoint& global) const {
    NUIPoint local = global;
    
    if (parent_) {
        local = parent_->globalToLocal(local);
    }
    
    local.x -= bounds_.x;
    local.y -= bounds_.y;
    
    return local;
}

// ============================================================================
// State
// ============================================================================

void NUIComponent::setVisible(bool visible) {
    if (visible_ != visible) {
        visible_ = visible;
        setDirty();
    }
}

void NUIComponent::setEnabled(bool enabled) {
    if (enabled_ != enabled) {
        enabled_ = enabled;
        setDirty();
    }
}

void NUIComponent::setFocused(bool focused) {
    if (focused) {
        if (g_focusedComponent != this) {
            if (g_focusedComponent) {
                g_focusedComponent->setFocused(false);
            }
            g_focusedComponent = this;
        }

        if (!focused_) {
            onFocusGained();
        }
        return;
    }

    if (g_focusedComponent == this) {
        g_focusedComponent = nullptr;
    }

    if (focused_) {
        onFocusLost();
    }
}

NUIComponent* NUIComponent::getFocusedComponent() {
    return g_focusedComponent;
}

void NUIComponent::clearFocusedComponent() {
    if (g_focusedComponent) {
        g_focusedComponent->setFocused(false);
    }
}

void NUIComponent::setHovered(bool hovered) {
    if (hovered_ != hovered) {
        hovered_ = hovered;
        if (hovered) {
            onMouseEnter();
        } else {
            onMouseLeave();
        }
        setDirty();
    }
}

void NUIComponent::setDirty(bool dirty) {
    dirty_ = dirty;
    
    // Propagate to parent
    if (dirty && parent_) {
        parent_->setDirty(true);
    }
}

void NUIComponent::setOpacity(float opacity) {
    opacity_ = std::max(0.0f, std::min(1.0f, opacity));
    setDirty();
}

// ============================================================================
// Theme
// ============================================================================

void NUIComponent::setTheme(std::shared_ptr<NUITheme> theme) {
    theme_ = theme;
    
    // Propagate to children
    for (auto& child : children_) {
        if (!child->theme_) {
            child->setTheme(theme);
        }
    }
    
    setDirty();
}

std::shared_ptr<NUITheme> NUIComponent::getTheme() const {
    if (theme_) {
        return theme_;
    }
    
    // Inherit from parent
    if (parent_) {
        return parent_->getTheme();
    }
    
    return nullptr;
}

// ============================================================================
// Protected Methods
// ============================================================================

void NUIComponent::renderChildren(NUIRenderer& renderer) {
    for (auto& child : children_) {
        if (child->isVisible()) {
            child->onRender(renderer);
        }
    }
}

void NUIComponent::updateChildren(double deltaTime) {
    for (auto& child : children_) {
        if (child->isVisible()) {
            child->onUpdate(deltaTime);
        }
    }
}

bool NUIComponent::containsPoint(const NUIPoint& point) const {
    return bounds_.contains(point);
}

std::shared_ptr<NUIComponent> NUIComponent::findChildAt(const NUIPoint& point) {
    // Check children in reverse order (front to back)
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if ((*it)->isVisible() && (*it)->containsPoint(point)) {
            // Check if any of its children contain the point
            auto childResult = (*it)->findChildAt(point);
            if (childResult) {
                return childResult;
            }
            return *it;
        }
    }
    return nullptr;
}



// ============================================================================
// Tooltip Implementation
// ============================================================================

void NUIComponent::setTooltip(const std::string& text) {
    tooltipText_ = text;
}

void NUIComponent::showRemoteTooltip(const std::string& text, const NUIPoint& position) {
    s_tooltipState.text = text;
    s_tooltipState.position = position;
    s_tooltipState.hoverPos = position;  // Set both for manual tooltip calls
    s_tooltipState.active = true;
    s_tooltipState.delayTimer = 0.0f;
    s_tooltipState.alpha = 0.0f;
}

void NUIComponent::hideRemoteTooltip() {
    s_tooltipState.active = false;
}

void NUIComponent::updateGlobalTooltip(double deltaTime) {
    if (s_tooltipState.active) {
        // Fade in
        const float FADE_SPEED = 5.0f;
        s_tooltipState.alpha = std::min(1.0f, s_tooltipState.alpha + static_cast<float>(deltaTime * FADE_SPEED));
    } else {
         s_tooltipState.alpha = 0.0f; // Instant hide for responsiveness
    }
}

void NUIComponent::renderGlobalTooltip(NUIRenderer& renderer) {
    if (!s_tooltipState.active || s_tooltipState.alpha <= 0.01f) return;
    
    // Reuse minimap tooltip pattern - position relative to hoverPos (actual mouse position)
    constexpr float kTooltipPadX = 6.0f;
    constexpr float kTooltipPadY = 3.0f;
    constexpr float kTooltipRadius = 4.0f;
    
    const float fontSize = 10.0f;
    const auto size = renderer.measureText(s_tooltipState.text, fontSize);
    
    const float w = size.width + kTooltipPadX * 2.0f;
    const float h = size.height + kTooltipPadY * 2.0f;
    
    // Position: offset from mouse cursor (like minimap tooltip)
    float x = s_tooltipState.hoverPos.x + 10.0f;
    float y = s_tooltipState.hoverPos.y - h - 6.0f;
    
    // If tooltip would go above screen, show below cursor instead
    if (y < 4.0f) {
        y = s_tooltipState.hoverPos.y + 16.0f;
    }
    
    // Clamp to reasonable screen bounds
    if (x < 4.0f) x = 4.0f;
    
    const NUIRect tipRect(x, y, w, h);
    
    // Theme colors (matching minimap style)
    const NUIColor bg = NUIColor(0.12f, 0.12f, 0.15f, 0.92f * s_tooltipState.alpha);
    const NUIColor border = NUIColor(0.4f, 0.4f, 0.45f, 0.65f * s_tooltipState.alpha);
    const NUIColor text = NUIColor(0.95f, 0.95f, 0.95f, 0.92f * s_tooltipState.alpha);
    
    renderer.fillRoundedRect(tipRect, kTooltipRadius, bg);
    renderer.strokeRoundedRect(tipRect, kTooltipRadius, 1.0f, border);
    renderer.drawTextCentered(s_tooltipState.text, tipRect, fontSize, text);
}

} // namespace NomadUI


