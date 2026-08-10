// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUIComponent.h"
#include "NUITheme.h"
#include "NUIThemeSystem.h"
#include "NUIRenderer.h"
#include <algorithm>
#include <iostream>
#include <utility>
#include <vector>

// Forward declare NUIRenderer to avoid dependency
namespace AestraUI {
    class NUIRenderer;
}

namespace AestraUI {

namespace {
    NUIComponent* g_focusedComponent = nullptr;
}

// Define static tooltip state
TooltipState NUIComponent::s_tooltipState;
bool NUIComponent::s_cursorCaptureActive = false;



NUIComponent::NUIComponent() {
}

NUIComponent::~NUIComponent() {
    hideRemoteTooltip(this);
    if (g_focusedComponent == this) {
        g_focusedComponent = nullptr;
    }
}

// ============================================================================
// Lifecycle
// ============================================================================

void NUIComponent::onRender(NUIRenderer& renderer) {
    if (!visible_) return;
    
    // Skip rendering leaf components that haven't changed
    if (!dirty_ && children_.empty()) return;
    
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
    //
    // An invisible child must not be offered ordinary input (#672). The guard
    // on line 71 is NOT sufficient on its own: this loop dispatches VIRTUALLY,
    // so any subclass whose onMouseEvent returns before delegating to this
    // implementation never executes it. That is exactly how a hidden
    // AuditionPanel swallowed Timeline clicks (#671) — it returned true from
    // its own override, and the base guard it would eventually have called was
    // never reached. There are 80+ overrides in this codebase; relying on each
    // one to re-check visibility is how the invariant gets lost.
    //
    // Enforcing it here, in the parent, is the only place an override cannot
    // bypass.
    //
    // Deliberately visibility ONLY. No bounds check belongs here: captured
    // drags, popovers and intentionally extended hit regions all rely on
    // out-of-bounds delivery (TrackUIComponent's trim/clip-drag/fader-capture
    // paths depend on it). Bounds remain each component's own business.
    //
    // Capture exemption: while the cursor is captured, the owning component
    // must keep receiving events even if something hides it mid-drag, or the
    // drag hangs with no terminating release. This framework has no capture-
    // owner registry, so the captured flag on the event is the available
    // signal — during capture the filter is skipped entirely, preserving the
    // pre-existing delivery behaviour for that case.
    const bool cursorCaptureInFlight = event.cursorCaptured;

    bool eventHandledByChild = false;
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if (!cursorCaptureInFlight && !(*it)->isVisible()) {
            continue;  // hidden: not eligible for ordinary input
        }
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
    if (wasHovered != shouldBeHovered && !event.cursorCaptured) {
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

    if (!tooltipText_.empty()) {
        NUIPoint localCenter(bounds_.width * 0.5f, bounds_.height + 6.0f);
        showRemoteTooltip(tooltipText_, localToGlobal(localCenter), this);
    }

    setDirty();
}

void NUIComponent::onMouseLeave() {
    hovered_ = false;
    hideRemoteTooltip(this);

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

namespace {
// The dispatch guard is a per-thread context: begin/endEventDispatch and the
// hierarchy mutations they bracket all run on the same (UI) thread. Depth
// counts nested dispatches; queued entries preserve mutation order and hold
// strong refs so affected children stay alive until dispatch unwinds.
thread_local int g_eventDispatchDepth = 0;

enum class DeferredHierarchyOperationType { Add, Remove, RemoveAll, BringToFront };

struct DeferredHierarchyOperation {
    DeferredHierarchyOperationType type;
    NUIComponent* parent;
    std::shared_ptr<NUIComponent> child;
};

thread_local std::vector<DeferredHierarchyOperation> g_deferredHierarchyOperations;
} // namespace

void NUIComponent::addChild(std::shared_ptr<NUIComponent> child) {
    if (!child) return;

    // Adding a popup from a mouse callback can otherwise reallocate the
    // parent's children vector while that same vector is being iterated.
    if (g_eventDispatchDepth > 0) {
        g_deferredHierarchyOperations.push_back({DeferredHierarchyOperationType::Add, this, std::move(child)});
        return;
    }
    
    // Remove from previous parent
    if (child->parent_) {
        child->parent_->removeChild(child);
    }
    
    child->parent_ = this; // Raw pointer is safe because parent owns child (strong ref)
    // No shared_from_this() needed unless we want weak_ptr parent, but here we use raw * for parent back-pointer
    children_.push_back(child);
    
    // Inherit theme if child doesn't have one
    if (!child->theme_ && theme_) {
        child->setTheme(theme_);
    }

    setDirty();
}

bool NUIComponent::dispatchMouseEvent(NUIComponent* target, const NUIMouseEvent& event) {
    if (!target) {
        return false;
    }

    struct DispatchGuard {
        DispatchGuard() { NUIComponent::beginEventDispatch(); }
        ~DispatchGuard() { NUIComponent::endEventDispatch(); }
    } guard;

    return target->onMouseEvent(event);
}

void NUIComponent::beginEventDispatch() {
    ++g_eventDispatchDepth;
}

void NUIComponent::endEventDispatch() {
    if (g_eventDispatchDepth > 0) {
        --g_eventDispatchDepth;
    }
    if (g_eventDispatchDepth != 0 || g_deferredHierarchyOperations.empty()) {
        return;
    }

    auto pending = std::move(g_deferredHierarchyOperations);
    g_deferredHierarchyOperations.clear();
    for (auto& operation : pending) {
        switch (operation.type) {
        case DeferredHierarchyOperationType::Add:
            if (operation.parent) {
                operation.parent->addChild(operation.child);
            }
            break;
        case DeferredHierarchyOperationType::Remove:
            if (operation.parent) {
                operation.parent->removeChild(operation.child);
            }
            break;
        case DeferredHierarchyOperationType::RemoveAll:
            if (operation.parent) {
                operation.parent->removeAllChildren();
            }
            break;
        case DeferredHierarchyOperationType::BringToFront:
            if (operation.child) {
                operation.child->bringToFront();
            }
            break;
        }
    }
}

void NUIComponent::removeChild(std::shared_ptr<NUIComponent> child) {
    if (!child) {
        return;
    }
    if (g_eventDispatchDepth > 0) {
        // Defer until the dispatch unwinds; the strong ref keeps the child alive
        // through the rest of the current event.
        g_deferredHierarchyOperations.push_back({DeferredHierarchyOperationType::Remove, this, std::move(child)});
        return;
    }
    auto it = std::find(children_.begin(), children_.end(), child);
    if (it != children_.end()) {
        (*it)->parent_ = nullptr;
        children_.erase(it);
        setDirty();
    }
}

void NUIComponent::removeAllChildren() {
    if (g_eventDispatchDepth > 0) {
        g_deferredHierarchyOperations.push_back({DeferredHierarchyOperationType::RemoveAll, this, nullptr});
        return;
    }

    for (auto& child : children_) {
        child->parent_ = nullptr;
    }
    children_.clear();
    setDirty();
}

void NUIComponent::bringToFront() {
    if (!parent_) return;

    if (g_eventDispatchDepth > 0) {
        g_deferredHierarchyOperations.push_back(
            {DeferredHierarchyOperationType::BringToFront, parent_, shared_from_this()});
        return;
    }

    auto& store = parent_->children_;
    // Check if valid before searching
    if (store.empty()) return;

    // shared_from_this() is safe if we are managed by shared_ptr (which children usually are)
    auto self = shared_from_this();

    auto it = std::find(store.begin(), store.end(), self);
    if (it != store.end() && it != std::prev(store.end())) {
        store.erase(it);
        store.push_back(self);
        parent_->setDirty();
    }
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
        if (!visible_) {
            setHovered(false);
            hideRemoteTooltip(this);
        }
        setDirty();
    }
}

void NUIComponent::setEnabled(bool enabled) {
    if (enabled_ != enabled) {
        enabled_ = enabled;
        if (!enabled_) {
            setHovered(false);
        }
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
    if (s_cursorCaptureActive && hovered)
        return;
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

void NUIComponent::onThemeChanged(const NUIThemeProperties& theme) {
    (void)theme;
    setDirty(true);
    for (auto& child : children_)
        child->onThemeChanged(theme);
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
    if (tooltipText_ == text)
        return;
    hideRemoteTooltip(this);
    tooltipText_ = text;
    if (hovered_ && !tooltipText_.empty()) {
        const NUIPoint localCenter(bounds_.width * 0.5f, bounds_.height + 6.0f);
        showRemoteTooltip(tooltipText_, localToGlobal(localCenter), this);
    }
}

void NUIComponent::showRemoteTooltip(const std::string& text, const NUIPoint& position, const void* owner, bool force) {
    constexpr float kTooltipDelaySeconds = 0.45f;

    if (text.empty()) {
        hideRemoteTooltip(owner);
        return;
    }

    if (!force && s_cursorCaptureActive)
        return;

    const bool canResumePendingDismiss = !s_tooltipState.active && s_tooltipState.dismissGraceTimer > 0.0f &&
        owner != nullptr &&
        s_tooltipState.owner == owner && s_tooltipState.text == text;
    if (canResumePendingDismiss) {
        s_tooltipState.active = true;
        s_tooltipState.dismissGraceTimer = 0.0f;
        if (force) {
            s_tooltipState.position = position;
            s_tooltipState.immediate = true;
            s_tooltipState.delayTimer = kTooltipDelaySeconds;
        }
        return;
    }

    const bool ownerChanged = !s_tooltipState.active || s_tooltipState.owner != owner;
    const bool contentChanged = s_tooltipState.text != text;
    const bool shouldRestart = ownerChanged || (contentChanged && !force);

    if (shouldRestart) {
        s_tooltipState.text = text;
        s_tooltipState.position = position;
        s_tooltipState.owner = owner;
        s_tooltipState.active = true;
        s_tooltipState.immediate = force;
        s_tooltipState.delayTimer = 0.0f;
        s_tooltipState.alpha = 0.0f;
        s_tooltipState.dismissGraceTimer = 0.0f;
    } else if (force) {
        s_tooltipState.text = text;
        s_tooltipState.position = position;
        s_tooltipState.immediate = true;
        s_tooltipState.delayTimer = kTooltipDelaySeconds;
    }
}

void NUIComponent::hideRemoteTooltip(const void* owner) {
    constexpr float kDismissGraceSeconds = 0.50f;

    if (owner != nullptr && s_tooltipState.owner != owner) {
        return;
    }
    if (owner != nullptr) {
        // Keep a short resume window. Complex components can route adjacent
        // pointer events through multiple internal tooltip regions; the same
        // owner may reassert the tooltip without restarting its delay or fade.
        s_tooltipState.active = false;
        s_tooltipState.dismissGraceTimer = kDismissGraceSeconds;
        return;
    }
    s_tooltipState.active = false;
    s_tooltipState.owner = nullptr;
    s_tooltipState.immediate = false;
    s_tooltipState.alpha = 0.0f;
    s_tooltipState.delayTimer = 0.0f;
    s_tooltipState.dismissGraceTimer = 0.0f;
}

void NUIComponent::updateGlobalTooltip(double deltaTime) {
    constexpr float kTooltipDelaySeconds = 0.45f;
    constexpr float kFadeSpeed = 8.0f;

    if (s_tooltipState.active) {
        s_tooltipState.dismissGraceTimer = 0.0f;
        s_tooltipState.delayTimer += static_cast<float>(std::max(0.0, deltaTime));
        if (s_tooltipState.immediate || s_tooltipState.delayTimer >= kTooltipDelaySeconds) {
            s_tooltipState.alpha =
                std::min(1.0f, s_tooltipState.alpha + static_cast<float>(std::max(0.0, deltaTime) * kFadeSpeed));
        }
    } else {
        s_tooltipState.dismissGraceTimer -= static_cast<float>(std::max(0.0, deltaTime));
        if (s_tooltipState.dismissGraceTimer <= 0.0f) {
            s_tooltipState.alpha = 0.0f;
            s_tooltipState.owner = nullptr;
            s_tooltipState.immediate = false;
            s_tooltipState.delayTimer = 0.0f;
            s_tooltipState.dismissGraceTimer = 0.0f;
        }
    }
}

NUIRect NUIComponent::calculateTooltipBounds(const NUIPoint& anchor, const NUISize& tooltipSize,
                                             const NUIRect& viewport) {
    constexpr float kMargin = 4.0f;
    float x = anchor.x + 10.0f;
    float y = anchor.y - tooltipSize.height - 6.0f;

    const bool hasViewport = viewport.width > 0.0f && viewport.height > 0.0f;
    const float top = hasViewport ? viewport.y + kMargin : kMargin;
    if (y < top) {
        y = anchor.y + 16.0f;
    }

    if (hasViewport) {
        const float left = viewport.x + kMargin;
        const float right = viewport.right() - kMargin;
        const float bottom = viewport.bottom() - kMargin;
        x = std::clamp(x, left, std::max(left, right - tooltipSize.width));
        y = std::clamp(y, top, std::max(top, bottom - tooltipSize.height));
    } else {
        x = std::max(kMargin, x);
        y = std::max(kMargin, y);
    }

    return {x, y, tooltipSize.width, tooltipSize.height};
}

void NUIComponent::renderGlobalTooltip(NUIRenderer& renderer, const NUIRect& viewport) {
    if (!s_tooltipState.active || s_tooltipState.alpha <= 0.01f)
        return;
    auto& theme = NUIThemeManager::getInstance();
    const auto& props = theme.getCurrentTheme();

    // Reuse the compact minimap tooltip styling around the stable hover anchor.
    const float tooltipPadX = props.spacingS;
    const float tooltipPadY = props.spacingXS;
    const float tooltipRadius = props.radiusS;
    const float fontSize = props.fontSizeXS;
    const auto size = renderer.measureText(s_tooltipState.text, fontSize);

    const float w = size.width + tooltipPadX * 2.0f;
    const float h = size.height + tooltipPadY * 2.0f;

    const NUIRect tipRect = calculateTooltipBounds(s_tooltipState.position, {w, h}, viewport);

    // Theme colors (matching minimap style)
    const NUIColor bg = theme.getColor("elevatedPanel").withAlpha(0.98f * s_tooltipState.alpha);
    const NUIColor border = theme.getColor("borderStrong").withAlpha(0.88f * s_tooltipState.alpha);
    const NUIColor text = theme.getColor("textPrimary").withAlpha(0.96f * s_tooltipState.alpha);

    renderer.fillRoundedRect(tipRect, tooltipRadius, bg);
    renderer.strokeRoundedRect(tipRect, tooltipRadius, props.layout.dividerWidth, border);
    renderer.drawTextCentered(s_tooltipState.text, tipRect, fontSize, text);
}

void NUIComponent::setCursorCaptureActive(bool active) {
    if (s_cursorCaptureActive == active)
        return;
    s_cursorCaptureActive = active;
    if (active) {
        hideRemoteTooltip();
    }
}

} // namespace AestraUI
