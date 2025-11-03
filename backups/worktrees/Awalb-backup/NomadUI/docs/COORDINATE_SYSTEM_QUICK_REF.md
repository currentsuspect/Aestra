# NomadUI Coordinate System - Quick Reference

⚠️ **CRITICAL:** NomadUI does NOT transform child coordinates!

## The Golden Rules

### 1. Always Use Absolute Coordinates
```cpp
// ❌ WRONG - Relative positioning
childComponent->setBounds(NUIRect(0, 0, 100, 50));

// ✅ CORRECT - Absolute positioning (manual)
NUIRect parentBounds = getBounds();
childComponent->setBounds(NUIRect(parentBounds.x, parentBounds.y, 100, 50));

// ✅ BETTER - Using utility helper
childComponent->setBounds(NUIAbsolute(getBounds(), 0, 0, 100, 50));
```

### 2. Never Reset Position in onResize()
```cpp
// ❌ WRONG - Destroys parent positioning
void onResize(int width, int height) {
    setBounds(NUIRect(0, 0, width, height));
}

// ✅ CORRECT - Preserves position
void onResize(int width, int height) {
    NUIRect current = getBounds();
    setBounds(NUIRect(current.x, current.y, width, height));
}
```

### 3. Add Parent Offsets When Positioning Children
```cpp
void layoutChildren() {
    NUIRect bounds = getBounds();
    
    // Manual way - add parent's X,Y to all child positions
    float childX = bounds.x + 10;
    float childY = bounds.y + 20;
    child->setBounds(NUIRect(childX, childY, 100, 50));
    
    // Better way - use utility helper
    child->setBounds(NUIAbsolute(bounds, 10, 20, 100, 50));
}
```

### 4. Render Using Absolute Coordinates
```cpp
void onRender(NUIRenderer& renderer) {
    NUIRect bounds = getBounds();
    
    // Use absolute coordinates for drawing
    renderer.fillRect(bounds, color);
    
    // Manual way
    renderer.drawText("Text", NUIPoint(bounds.x + 10, bounds.y + 20), 16, color);
    
    // Better way - use utility helper
    renderer.drawText("Text", NUIAbsolutePoint(bounds, 10, 20), 16, color);
    
    renderChildren(renderer);
}
```

## Utility Helpers

Make positioning easier with these helpers from `NUITypes.h`:

```cpp
// Position child with offset
child->setBounds(NUIAbsolute(getBounds(), 10, 20, 100, 50));

// Draw text at offset
renderer.drawText("Text", NUIAbsolutePoint(getBounds(), 10, 20), 16, color);

// Center child in parent
child->setBounds(NUICentered(getBounds(), 200, 100));

// Fill parent with margins
child->setBounds(NUIAligned(getBounds(), 10, 10, 10, 10));

// Stack children horizontally
std::vector<NUISize> sizes = {{100, 50}, {200, 50}};
auto rects = NUIStackHorizontal(getBounds(), sizes, 10);
for (size_t i = 0; i < rects.size(); ++i) {
    children[i]->setBounds(rects[i]);
}

// Position in grid
child->setBounds(NUIGridCell(getBounds(), 1, 2, 3, 4));

// Apply scroll offset for scrollable containers
child->setBounds(NUIApplyScrollOffset(originalBounds, 0, scrollY));

// Clamp popup to screen
popup->setBounds(NUIScreenClamp(popupBounds, screenWidth, screenHeight));

// Check if rects intersect for optimization
if (NUIRectsIntersect(rectA, rectB)) {
    // Handle intersection
}
```

## Common Mistakes

| Mistake | Result | Fix |
|---------|--------|-----|
| `setBounds(0, 0, w, h)` in onResize | Component jumps to (0,0) | Preserve current x,y |
| Positioning child at (0, 0) | Child renders at screen origin | Use `NUIAbsolute()` helper |
| Using relative coordinates | Overlapping components | Always use absolute coords |
| Manual offset calculations | Error-prone, verbose | Use utility helpers |

## Quick Reference Table

| Rule | Purpose | Code Pattern |
|------|---------|--------------|
| **Preserve X,Y in onResize()** | Prevents visual drift | `setBounds(current.x, current.y, w, h)` |
| **Add parent offsets** | Correct global placement | `NUIAbsolute(parent, offsetX, offsetY, w, h)` |
| **Use helpers** | Cleaner code | `NUICentered()`, `NUIAligned()` |
| **Render order = Z-order** | Control stacking | First added = bottom layer |
| **Origin (0,0) = top-left** | Standard coordinates | Y increases downward |

## YAML Configuration System

Customize all UI dimensions and colors by editing `NomadUI/Config/nomad_ui_config.yaml`:

```yaml
# Example: Adjust track height and colors
layout:
  trackHeight: 100.0        # Taller tracks
  trackControlsWidth: 180.0 # Wider control panel

colors:
  primary: "#ff6b35"        # Orange accent
  backgroundSecondary: "#1a1a1a" # Darker panels
```

**Changes take effect after rebuilding the application.**

## Full Documentation

See [NOMADUI_COORDINATE_SYSTEM.md](../../NomadDocs/NOMADUI_COORDINATE_SYSTEM.md) for complete guide with examples.

---

*Keep this reference open when working with NomadUI components!*
