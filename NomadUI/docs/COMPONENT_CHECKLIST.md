# NomadUI Component Development Checklist

Use this checklist when creating or modifying NomadUI components to avoid common pitfalls.

---

## ✅ Component Creation Checklist

### 1. Header File (.h)

- [ ] Inherit from `NUIComponent`
- [ ] Include coordinate system warning comment if component has children
- [ ] Declare `onRender()` override
- [ ] Declare `onResize()` override if component has children
- [ ] Document all public methods

### 2. Constructor

- [ ] Initialize all member variables
- [ ] Create child components
- [ ] Call `addChild()` for all children
- [ ] Set initial state

### 3. onResize() Implementation

**⚠️ CRITICAL COORDINATE SYSTEM CHECK:**

- [ ] **NEVER** reset bounds to `(0, 0, width, height)`
- [ ] **ALWAYS** preserve X,Y position:
  ```cpp
  NUIRect current = getBounds();
  setBounds(NUIRect(current.x, current.y, width, height));
  ```
- [ ] Call layout method to position children
- [ ] Call parent `onResize()` at end

### 4. Layout Method (if component has children)

**⚠️ CRITICAL COORDINATE SYSTEM CHECK:**

- [ ] Get component bounds: `NUIRect bounds = getBounds();`
- [ ] **ALWAYS** add parent offsets to child positions:
  ```cpp
  float childX = bounds.x + offsetX;
  float childY = bounds.y + offsetY;
  ```
- [ ] Set child bounds with absolute coordinates
- [ ] Handle edge cases (empty children, zero size, etc.)

### 5. onRender() Implementation

**⚠️ CRITICAL COORDINATE SYSTEM CHECK:**

- [ ] Get component bounds: `NUIRect bounds = getBounds();`
- [ ] Use absolute coordinates for all drawing:
  ```cpp
  renderer.fillRect(bounds, color);
  renderer.drawText("Text", NUIPoint(bounds.x + 10, bounds.y + 20), size, color);
  ```
- [ ] Call `renderChildren(renderer)` to render children
- [ ] Don't assume (0,0) is the component origin

### 6. Event Handling

- [ ] Implement `onMouseEvent()` if needed
- [ ] Implement `onKeyEvent()` if needed
- [ ] Call parent event handler if not consumed
- [ ] Use `containsPoint()` for hit testing

### 7. Testing

- [ ] Test component standalone
- [ ] Test component as child of another component
- [ ] Test with window resize
- [ ] Test with fullscreen toggle
- [ ] Verify no overlapping with siblings
- [ ] Check positioning at different window sizes

---

## 🚫 Common Mistakes to Avoid

### Mistake 1: Resetting Position in onResize()
```cpp
// ❌ WRONG
void MyComponent::onResize(int width, int height) {
    setBounds(NUIRect(0, 0, width, height));  // Destroys parent positioning!
}

// ✅ CORRECT
void MyComponent::onResize(int width, int height) {
    NUIRect current = getBounds();
    setBounds(NUIRect(current.x, current.y, width, height));
}
```

### Mistake 2: Using Relative Coordinates for Children
```cpp
// ❌ WRONG
void MyComponent::layoutChildren() {
    child->setBounds(NUIRect(10, 20, 100, 50));  // Relative positioning doesn't work!
}

// ✅ CORRECT
void MyComponent::layoutChildren() {
    NUIRect bounds = getBounds();
    child->setBounds(NUIRect(bounds.x + 10, bounds.y + 20, 100, 50));
}
```

### Mistake 3: Assuming (0,0) Origin in Rendering
```cpp
// ❌ WRONG
void MyComponent::onRender(NUIRenderer& renderer) {
    renderer.drawText("Text", NUIPoint(10, 20), 16, color);  // Where is this?
}

// ✅ CORRECT
void MyComponent::onRender(NUIRenderer& renderer) {
    NUIRect bounds = getBounds();
    renderer.drawText("Text", NUIPoint(bounds.x + 10, bounds.y + 20), 16, color);
}
```

---

## 🔍 Debugging Checklist

If your component isn't positioning correctly:

- [ ] Add logging to `onResize()` to print bounds
- [ ] Add logging to layout method to print child positions
- [ ] Draw debug borders around component bounds
- [ ] Check parent component's positioning
- [ ] Verify you're not resetting to (0,0) anywhere
- [ ] Confirm you're adding parent offsets to children

### Debug Logging Template
```cpp
void MyComponent::onResize(int width, int height) {
    NUIRect current = getBounds();
    std::cout << "MyComponent::onResize() - Current bounds: " 
              << current.x << "," << current.y << " " 
              << current.width << "x" << current.height << std::endl;
    
    setBounds(NUIRect(current.x, current.y, width, height));
    layoutChildren();
}
```

### Debug Rendering Template
```cpp
void MyComponent::onRender(NUIRenderer& renderer) {
    NUIRect bounds = getBounds();
    
    // Draw debug border
    renderer.drawRect(bounds, 2.0f, NUIColor(1, 0, 0, 1));  // Red border
    
    // Normal rendering...
}
```

---

## 📚 Documentation References

Before starting, read:
- [COORDINATE_SYSTEM_QUICK_REF.md](COORDINATE_SYSTEM_QUICK_REF.md) - Quick reference
- [NOMADUI_COORDINATE_SYSTEM.md](../../NomadDocs/NOMADUI_COORDINATE_SYSTEM.md) - Full guide

When stuck, check:
- [TRANSPORT_BAR_FIX_SUMMARY.md](../../NomadDocs/TRANSPORT_BAR_FIX_SUMMARY.md) - Real-world example
- [NUIComponent.h](../Core/NUIComponent.h) - Base class API

---

## ✨ Best Practices

1. **Always think in absolute coordinates** - Origin (0,0) is top-left, Y increases downward
2. **Test with nested components early** - Verify positioning in hierarchy
3. **Add debug rendering during development** - Draw bounds to visualize
4. **Document coordinate assumptions in comments** - Help future maintainers
5. **Use utility helpers** - `NUIAbsolute()`, `NUICentered()`, etc.
6. **Remember render order = Z-order** - First rendered = bottom layer
7. **Review this checklist before committing** - Catch issues early

---

## 📋 Coordinate System Quick Facts

- **Origin:** (0, 0) at top-left corner
- **X-axis:** Increases left → right
- **Y-axis:** Increases top → bottom
- **Units:** Pixels (float precision)
- **Transformation:** None (absolute coordinates only)
- **Z-order:** Determined by render order, not coordinates

---

*Keep this checklist open while developing NomadUI components!*
