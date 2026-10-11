# AestraUI Coordinate System - Quick Reference

⚠️ **CRITICAL:** AestraUI does NOT transform child coordinates!

## The Golden Rules

### 1. Always Use Absolute Coordinates
```cpp
// ❌ WRONG - Relative positioning
childComponent->setBounds(NUIRect(0, 0, 100, 50));

// ✅ CORRECT - Absolute positioning (manual)
NUIRect parentBounds = getBounds();
childComponent->setBounds(NUIRect(parentBounds.x, parentBounds.y, 100, 50));

// ✅ BETTER - Work in Local space, convert once (AestraUI/Layout/NUILayoutSpace.h)
using namespace AestraUI::Layout;
const NUIWindowPoint origin(getBounds().x, getBounds().y);
childComponent->setBounds(localToWindow(NUILocalRect(0, 0, 100, 50), origin).raw());
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
    
    // Better way - Local rect, one typed conversion
    child->setBounds(localToWindow(NUILocalRect(10, 20, 100, 50), NUIWindowPoint(bounds.x, bounds.y)).raw());
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
    
    renderChildren(renderer);
}
```

## Layout Helpers

The old `NUITypes.h` placement helpers (`NUIAbsolute`, `NUICentered`, `NUIStack*`,
`NUIGridCell`, …) were removed in V8-X2b phase 6. Use `AestraUI/Layout/`:

```cpp
using namespace AestraUI::Layout;
const NUIRect b = getBounds();
const NUIWindowPoint origin(b.x, b.y);
const NUILocalRect me(0, 0, b.width, b.height);

// Bands and columns
const auto bands = splitVertical(me, 28.0f);              // header + body
const auto cols  = splitHorizontal(bands.trailing, 200);  // sidebar + content

// Rows of fixed-size items (leading or trailing edge), scrolling stacks
auto buttons = arrangeTrailingRow(bands.leading, {24, 24, 24}, 24, 4, 8);
auto rows    = arrangeScrollingStack(cols.trailing, NUIAxis::Vertical, 38, 0, count, scroll);

// One conversion to the window-absolute bounds setBounds() expects
closeButton->setBounds(localToWindow(buttons[0], origin).raw());
```

Every algorithm takes an optional `NUILayoutRecorder*`; with
`AESTRA_LAYOUT_TRACE=<surface>` the layout explains each rect it placed
(`NUILayoutExplain.h`).

## Common Mistakes

| Mistake | Result | Fix |
|---------|--------|-----|
| `setBounds(0, 0, w, h)` in onResize | Component jumps to (0,0) | Preserve current x,y |
| Positioning child at (0, 0) | Child renders at screen origin | Convert with `localToWindow()` |
| Using relative coordinates | Overlapping components | Always use absolute coords |
| Manual offset calculations | Error-prone, verbose | Use the `Layout/` algorithms |

## Quick Reference Table

| Rule | Purpose | Code Pattern |
|------|---------|--------------|
| **Preserve X,Y in onResize()** | Prevents visual drift | `setBounds(current.x, current.y, w, h)` |
| **Add parent offsets** | Correct global placement | `localToWindow(localRect, parentOrigin)` |
| **Use layout algorithms** | Cleaner, explainable | `splitVertical()`, `arrangeTrailingRow()` |
| **Render order = Z-order** | Control stacking | First added = bottom layer |
| **Origin (0,0) = top-left** | Standard coordinates | Y increases downward |

## YAML Configuration System

Customize all UI dimensions and colors by editing `AestraUI/Config/Aestra_ui_config.yaml`:

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

The complete coordinate-system guide with worked examples is maintained as an
internal engineering note and is not published with this repository. This quick
reference is the public summary; `AestraUI/Core/` is the authority in code.

---

*Keep this reference open when working with AestraUI components!*
