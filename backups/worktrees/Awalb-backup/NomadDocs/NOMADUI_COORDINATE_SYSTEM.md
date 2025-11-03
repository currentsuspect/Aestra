# NomadUI Coordinate System Guide

## Overview

This document explains the coordinate system used in NomadUI and critical patterns for positioning components correctly. Understanding this is essential for avoiding layout issues, especially when working with nested components.

**Last Updated:** January 2025  
**Status:** Production

---

## Coordinate System Fundamentals

### Coordinate Origin

**NomadUI uses a standard 2D screen coordinate system:**

- **Origin (0, 0)** is at the **top-left** corner of the window
- **X-axis** increases from left to right (→)
- **Y-axis** increases from top to bottom (↓)
- All coordinates are in **pixels** (float precision)

```
(0,0) ────────────────────→ X
  │
  │    Window Content
  │
  ↓
  Y
```

This is consistent with most UI frameworks and graphics APIs (OpenGL, DirectX, HTML Canvas).

### Key Insight: No Automatic Coordinate Transformation

**NomadUI does NOT automatically transform child component coordinates relative to their parent.**

This means:
- When you set a child's bounds to `(0, 0, 100, 50)`, it renders at **absolute screen position** `(0, 0)`
- It does NOT render relative to its parent's position
- Each component must be positioned at its **absolute screen coordinates**

### Example of the Problem

```cpp
// Parent component at (0, 100, 800, 600)
parentComponent->setBounds(NUIRect(0, 100, 800, 600));

// Child positioned at (0, 0) - WRONG!
childComponent->setBounds(NUIRect(0, 0, 200, 50));
// This renders at screen position (0, 0), NOT at parent's position!

// Child positioned correctly - RIGHT!
NUIRect parentBounds = parentComponent->getBounds();
childComponent->setBounds(NUIRect(parentBounds.x, parentBounds.y, 200, 50));
// This renders at screen position (0, 100), which is correct!
```

---

## Critical Pattern: Component Positioning

### Rule 1: Always Use Absolute Coordinates

When positioning child components, always add the parent's X and Y offset:

```cpp
void MyComponent::layoutChildren() {
    NUIRect bounds = getBounds();  // Get our absolute position
    
    // Position child at absolute coordinates
    float childX = bounds.x + 10;  // Parent X + offset
    float childY = bounds.y + 20;  // Parent Y + offset
    
    childComponent->setBounds(NUIRect(childX, childY, 100, 50));
}
```

### Rule 2: Preserve Position in onResize()

**CRITICAL:** Never reset bounds to `(0, 0)` in `onResize()` - this destroys the parent's positioning!

```cpp
// WRONG - Resets position to (0, 0)!
void MyComponent::onResize(int width, int height) {
    setBounds(NUIRect(0, 0, width, height));  // ❌ BAD!
}

// CORRECT - Preserves X,Y position
void MyComponent::onResize(int width, int height) {
    NUIRect currentBounds = getBounds();
    setBounds(NUIRect(currentBounds.x, currentBounds.y, width, height));  // ✅ GOOD!
}
```

### Rule 3: Render Using Absolute Coordinates

When rendering, use absolute coordinates from `getBounds()`:

```cpp
void MyComponent::onRender(NUIRenderer& renderer) {
    NUIRect bounds = getBounds();  // Absolute position
    
    // Draw background at absolute position
    renderer.fillRect(bounds, backgroundColor);
    
    // Draw text at absolute position
    float textX = bounds.x + 10;
    float textY = bounds.y + 20;
    renderer.drawText("Hello", NUIPoint(textX, textY), 16, textColor);
    
    // Render children (they handle their own absolute positioning)
    renderChildren(renderer);
}
```

---

## Render Order and Z-Index

### How Stacking Works

**Important:** Component stacking order is determined by **render order**, not by coordinates.

```cpp
void ParentComponent::onRender(NUIRenderer& renderer) {
    // 1. Draw parent background (bottom layer)
    renderer.fillRect(getBounds(), backgroundColor);
    
    // 2. Draw parent content (middle layer)
    renderer.drawText("Parent", position, 16, textColor);
    
    // 3. Render children (top layer)
    renderChildren(renderer);  // Children draw on top of parent
}
```

### Child Rendering Order

Children are rendered in the order they were added to the parent:

```cpp
// First child added - renders first (bottom)
addChild(backgroundPanel);

// Second child added - renders second (middle)
addChild(contentPanel);

// Third child added - renders third (top)
addChild(overlayPanel);
```

**Result:** `overlayPanel` appears on top of `contentPanel`, which appears on top of `backgroundPanel`.

### Controlling Z-Order

To change stacking order:

1. **Remove and re-add children** in desired order
2. **Render manually** instead of using `renderChildren()`
3. **Use explicit layers** (future feature)

```cpp
void MyComponent::onRender(NUIRenderer& renderer) {
    // Render background layer
    backgroundChild->onRender(renderer);
    
    // Render middle layer
    contentChild->onRender(renderer);
    
    // Render overlay layer (on top)
    overlayChild->onRender(renderer);
}
```

### Future: Explicit Z-Index

A future version may add explicit z-index support:

```cpp
// Proposed API (not yet implemented)
child->setZIndex(100);  // Higher values render on top
```

For now, control stacking through render order.

---

## Real-World Example: Transport Bar Issue

### The Problem

The transport bar was covering the title bar because:

1. **NUICustomWindow** positioned content area at `(0, 32)` (below 32px title bar)
2. **NomadContent** set transport bar bounds to `(0, 0)` (relative positioning)
3. **TransportBar::onResize()** reset bounds to `(0, 0)` (destroyed parent positioning)
4. Result: Transport bar rendered at screen `(0, 0)`, covering the title bar!

### The Solution

**Main.cpp - Parent sets absolute position:**
```cpp
void NomadContent::onResize(int width, int height) {
    if (m_transportBar) {
        float transportHeight = 60.0f;
        NUIRect contentBounds = getBounds();  // Get absolute position (0, 32)
        
        // Set transport bar to absolute position
        m_transportBar->setBounds(NUIRect(contentBounds.x, contentBounds.y, 
                                         width, transportHeight));
        m_transportBar->onResize(width, static_cast<int>(transportHeight));
    }
}
```

**TransportBar.cpp - Preserve position in onResize:**
```cpp
void TransportBar::onResize(int width, int height) {
    // Preserve X,Y position set by parent
    NUIRect currentBounds = getBounds();
    setBounds(NUIRect(currentBounds.x, currentBounds.y, width, height));
    layoutComponents();
}
```

**TransportBar.cpp - Layout children with absolute coordinates:**
```cpp
void TransportBar::layoutComponents() {
    NUIRect bounds = getBounds();  // Absolute position
    
    // Position button at absolute coordinates
    float buttonX = bounds.x + 8;  // Add parent's X offset
    float buttonY = bounds.y + 10; // Add parent's Y offset
    
    m_playButton->setBounds(NUIRect(buttonX, buttonY, 40, 40));
}
```

---

## Best Practices

### ✅ DO

1. **Always use absolute coordinates** when positioning children
2. **Preserve X,Y in onResize()** - only update width/height
3. **Add parent offsets** when calculating child positions
4. **Use getBounds()** to get current absolute position
5. **Test with nested components** to verify positioning

### ❌ DON'T

1. **Never reset to (0, 0)** in onResize()
2. **Don't assume relative coordinates** - they don't exist in NomadUI
3. **Don't forget parent offsets** when positioning children
4. **Don't mix coordinate systems** - always use absolute

---

## Debugging Tips

### Add Position Logging

```cpp
void MyComponent::onResize(int width, int height) {
    NUIRect bounds = getBounds();
    std::stringstream ss;
    ss << "MyComponent bounds: " << bounds.x << "," << bounds.y 
       << " " << bounds.width << "x" << bounds.height;
    Log::info(ss.str());
}
```

### Visual Debugging

Draw component bounds to see positioning:

```cpp
void MyComponent::onRender(NUIRenderer& renderer) {
    NUIRect bounds = getBounds();
    
    // Draw debug border
    renderer.drawRect(bounds, 2.0f, NUIColor(1, 0, 0, 1));  // Red border
    
    // Normal rendering...
}
```

### Check Parent-Child Hierarchy

```cpp
// Log component tree
void logComponentTree(NUIComponent* comp, int depth = 0) {
    std::string indent(depth * 2, ' ');
    NUIRect bounds = comp->getBounds();
    std::cout << indent << "Component at (" << bounds.x << "," << bounds.y 
              << ") size " << bounds.width << "x" << bounds.height << std::endl;
    
    for (auto& child : comp->getChildren()) {
        logComponentTree(child.get(), depth + 1);
    }
}
```

---

## Utility Helpers (v1.1+)

To make absolute positioning more ergonomic, NomadUI provides utility helpers in `NUITypes.h`:

### NUIAbsolute() - Simplified Positioning

```cpp
// Before: Manual calculation
NUIRect parentBounds = getBounds();
child->setBounds(NUIRect(parentBounds.x + 10, parentBounds.y + 20, 100, 50));

// After: Using helper
child->setBounds(NUIAbsolute(getBounds(), 10, 20, 100, 50));
```

### NUIAbsolutePoint() - Point Positioning

```cpp
// Before: Manual calculation
NUIRect bounds = getBounds();
renderer.drawText("Hello", NUIPoint(bounds.x + 10, bounds.y + 20), 16, color);

// After: Using helper
renderer.drawText("Hello", NUIAbsolutePoint(getBounds(), 10, 20), 16, color);
```

### NUICentered() - Center Child in Parent

```cpp
// Center a 200x100 component within parent
child->setBounds(NUICentered(getBounds(), 200, 100));
```

### NUIAligned() - Edge-Aligned Positioning

```cpp
// Fill parent with 10px margins on all sides
child->setBounds(NUIAligned(getBounds(), 10, 10, 10, 10));

// Dock to top with 10px margins (bottom stretches)
child->setBounds(NUIAligned(getBounds(), 10, 10, 10, -1));
```

### NUIStackHorizontal() - Horizontal Layout

```cpp
// Stack children horizontally with 10px spacing
std::vector<NUISize> childSizes = {{100, 50}, {200, 50}, {150, 50}};
auto rects = NUIStackHorizontal(getBounds(), childSizes, 10);
for (size_t i = 0; i < rects.size(); ++i) {
    children[i]->setBounds(rects[i]);
}
```

### NUIStackVertical() - Vertical Layout

```cpp
// Stack children vertically with 5px spacing
auto rects = NUIStackVertical(getBounds(), childSizes, 5);
for (size_t i = 0; i < rects.size(); ++i) {
    children[i]->setBounds(rects[i]);
}
```

### NUIGridCell() - Grid Positioning

```cpp
// Position in 3x4 grid at row 1, column 2
child->setBounds(NUIGridCell(getBounds(), 1, 2, 3, 4));
```

### NUIApplyScrollOffset() - Scrollable Containers

```cpp
// Adjust child positions for scroll in nested containers
for (auto& child : children) {
    NUIRect original = child->getBounds();
    child->setBounds(NUIApplyScrollOffset(original, scrollX, scrollY));
}
```

### NUIScreenClamp() - Popup/Tooltip Clamping

```cpp
// Ensure popups stay on screen
NUIRect clamped = NUIScreenClamp(popupBounds, screenWidth, screenHeight);
popup->setBounds(clamped);
```

### NUIRelativePosition() - Position Conversion

```cpp
// Convert absolute to relative for saving state
NUIRect relative = NUIRelativePosition(child->getBounds(), parent->getBounds());

// Convert back to absolute
child->setBounds(NUIAbsoluteFromRelative(relative, parent->getBounds()));
```

### NUIUnionRects() - Invalidation Optimization

```cpp
// Calculate minimal invalidation area
std::vector<NUIRect> dirtyRects = {child1->getBounds(), child2->getBounds()};
NUIRect invalidationArea = NUIUnionRects(dirtyRects);
renderer.invalidateRegion(invalidationArea);
```

These helpers maintain the absolute coordinate system while making code cleaner and less error-prone.

---

## YAML Configuration System (v1.3+)

### Pixel-Perfect Customization

NOMAD UI now supports comprehensive YAML-based configuration for pixel-perfect control over all visual aspects:

#### Configuration File Structure

```yaml
# Colors - Full RGBA support
colors:
  backgroundPrimary: "#121214"
  primary: "#00bcd4"
  textPrimary: "#e6e6eb"

# Layout dimensions - All sizes configurable
layout:
  trackHeight: 80.0
  trackControlsWidth: 150.0
  fileBrowserWidth: 200.0
  transportBarHeight: 60.0

# Spacing system
spacing:
  panelMargin: 10.0
  componentPadding: 8.0

# Typography
typography:
  fontSizeM: 14.0
  fontSizeL: 16.0
```

#### Usage

1. **Modify Configuration**: Edit `NomadUI/Config/nomad_ui_config.yaml`
2. **Rebuild Application**: Changes take effect after compilation
3. **Runtime Loading**: Configuration is loaded at startup

#### Benefits

✅ **Pixel-Perfect Control**: Adjust any dimension or color without code changes
✅ **Theme Consistency**: Centralized styling system
✅ **Easy Customization**: Simple YAML format for designers
✅ **Compile-Time Optimization**: All values resolved at build time

## Advanced Utilities (v1.2+)

### Addressing System-Wide Issues

The following utilities address common pitfalls in absolute coordinate systems:

#### Scrollable Containers
```cpp
// In your scrollable component's layout method
void ScrollableContainer::updateScroll() {
    for (auto& child : children) {
        NUIRect original = child->getBounds();
        child->setBounds(NUIApplyScrollOffset(original, m_scrollX, m_scrollY));
    }
}
```

#### Window Resizing & Layout Propagation
```cpp
// Trigger full layout update after window resize
void MyWindow::onResize(int width, int height) {
    // Update root component bounds
    setBounds(NUIRect(0, 0, width, height));
    
    // Propagate layout to all children recursively
    layoutAllChildren();
}

void layoutAllChildren() {
    for (auto& child : children) {
        child->onResize(child->getWidth(), child->getHeight());
        child->layoutAllChildren(); // Recursive
    }
}
```

#### Popup/Tooltip Clamping
```cpp
// Ensure tooltips stay visible
NUIRect tooltipRect = calculateTooltipPosition(mousePos, tooltipSize);
NUIRect clamped = NUIScreenClamp(tooltipRect, screenWidth, screenHeight);
tooltip->setBounds(clamped);
```

#### Performance Optimization
```cpp
// Calculate minimal invalidation region
std::vector<NUIRect> changedRects;
for (auto& component : dirtyComponents) {
    changedRects.push_back(component->getBounds());
}
NUIRect invalidationArea = NUIUnionRects(changedRects);
renderer.invalidateRegion(invalidationArea);
```

## Future Improvements

### Potential Solutions

1. **Add coordinate transformation to NUIComponent::renderChildren()**
    - Automatically translate renderer context for children
    - Would allow relative positioning
    - Requires renderer push/pop transform support

2. **Create RelativeComponent base class**
    - Handles coordinate transformation internally
    - Opt-in for components that want relative positioning

3. **Add layout managers**
    - VBoxLayout, HBoxLayout, GridLayout
    - Handle positioning automatically
    - Similar to Qt/Java Swing layout managers

---

## Related Documentation

- [CUSTOM_WINDOW_INTEGRATION.md](CUSTOM_WINDOW_INTEGRATION.md) - Custom window and title bar setup
- [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md) - General development guidelines
- NomadUI/Core/NUIComponent.h - Component base class API

---

## Summary

**Remember:** NomadUI uses absolute screen coordinates for all components. Always add parent offsets when positioning children, and never reset position in `onResize()`. This pattern is critical for correct layout behavior.

---

*"Absolute coordinates, absolute clarity."*


---

## Quick Reference Table

| Rule | Purpose | Example |
|------|---------|---------|
| **Origin at (0,0) top-left** | Standard screen coordinates | Y increases downward |
| **No automatic transformation** | Explicit positioning required | Children use absolute coords |
| **Preserve X,Y in onResize()** | Prevents visual drift | `setBounds(current.x, current.y, w, h)` |
| **Add parent offsets** | Ensures correct global placement | `child.x = parent.x + offset` |
| **Use utility helpers** | Cleaner absolute math | `NUIAbsolute(parent, x, y, w, h)` |
| **Render order = Z-order** | First rendered = bottom layer | Control via add order |
| **Test with nesting** | Verify positioning works | Check parent-child hierarchy |

---

## Related Documentation

- [CUSTOM_WINDOW_INTEGRATION.md](CUSTOM_WINDOW_INTEGRATION.md) - Custom window and title bar setup
- [COORDINATE_UTILITIES_V1.1.md](COORDINATE_UTILITIES_V1.1.md) - Utility helpers for positioning
- [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md) - General development guidelines
- [COMPONENT_CHECKLIST.md](../NomadUI/docs/COMPONENT_CHECKLIST.md) - Component development checklist
- NomadUI/Core/NUIComponent.h - Component base class API
- NomadUI/Core/NUITypes.h - Coordinate utility functions

---

## Summary

**Remember:** NomadUI uses absolute screen coordinates for all components. Always add parent offsets when positioning children, and never reset position in `onResize()`. Use the utility helpers (`NUIAbsolute`, `NUICentered`, etc.) to make positioning cleaner and less error-prone. Render order determines stacking (Z-order).

---

*"Absolute coordinates, absolute clarity."*
