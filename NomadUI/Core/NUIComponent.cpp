// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUIComponent.h"
#include "NUITheme.h"
#include <algorithm>
#include <iostream>

// Forward declare NUIRenderer to avoid dependency
namespace NomadUI {
    class NUIRenderer;
}

namespace NomadUI {

NUIComponent::NUIComponent() {
}

NUIComponent::~NUIComponent() {
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
        setHovered(shouldBeHovered);
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
    setDirty();
}

void NUIComponent::onMouseLeave() {
    hovered_ = false;
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
    std::cout << "[getGlobalBounds] Starting with local bounds: (" << r.x << "," << r.y << "," << r.width << "," << r.height << ")" << std::endl;
    const NUIComponent* p = getParent();
    int depth = 0;
    while (p) {
        const NUIRect& pb = p->getBounds();
        std::cout << "[getGlobalBounds] Depth " << depth << " - Parent bounds: (" << pb.x << "," << pb.y << "," << pb.width << "," << pb.height << ")" << std::endl;
        r.x += pb.x;
        r.y += pb.y;
        std::cout << "[getGlobalBounds] After adding parent, global so far: (" << r.x << "," << r.y << ")" << std::endl;
        p = p->getParent();
        depth++;
    }
    std::cout << "[getGlobalBounds] Final global bounds: (" << r.x << "," << r.y << "," << r.width << "," << r.height << ")" << std::endl;
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
    if (focused_ != focused) {
        if (focused) {
            onFocusGained();
        } else {
            onFocusLost();
        }
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

} // namespace NomadUI
