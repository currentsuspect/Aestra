# UI Components Reference

## NUIButton

The cornerstone of the NomadUI interaction system.

### Styles

#### Primary Style
```cpp
auto button = std::make_shared<NUIButton>("Click Me");
button->setStyle(NUIButton::Style::Primary);
// Features: Gradient background, rounded corners, hover glow
```

#### Secondary Style
```cpp
auto button = std::make_shared<NUIButton>("Secondary");
button->setStyle(NUIButton::Style::Secondary);
// Features: Outlined style, hover border enhancement
```

#### Icon Style
```cpp
auto button = std::make_shared<NUIButton>();
button->setStyle(NUIButton::Style::Icon);
button->setSize(40, 40);
// Features: Circular design, perfect for toolbars
```

#### Text Style
```cpp
auto button = std::make_shared<NUIButton>("Link");
button->setStyle(NUIButton::Style::Text);
// Features: Minimal design, hover background only
```

### Event Handling

```cpp
// Click events
button->setOnClick([]() {
    Log::info("Button was clicked!");
});

// Toggle functionality (for checkboxes, etc.)
button->setToggleable(true);
button->setOnToggle([](bool toggled) {
    Log::info("Toggled: " + std::to_string(toggled));
});
```

### Recent Fixes

#### ✅ Hover System Improvements
- **Fixed Detection Timing** - Hover state now calculated after event propagation
- **Eliminated Lingering States** - No more stuck hover effects after mouse leave
- **Consistent Bounds Checking** - Proper coordinate system handling

#### ✅ Press Feedback
- **Removed Color Changes** - No jarring purple flashes on button press
- **Clean Visual Feedback** - Maintains consistent appearance when pressed
- **Smooth Transitions** - Professional interaction feel

### Color Customization

```cpp
// Background colors
button->setBackgroundColor(NUIColor(0.2f, 0.2f, 0.2f, 1.0f));
button->setHoverColor(NUIColor(0.3f, 0.3f, 0.3f, 1.0f));
button->setPressedColor(NUIColor(0.2f, 0.2f, 0.2f, 1.0f)); // Same as background

// Text colors
button->setTextColor(NUIColor::white());
```

## NUILabel

Simple yet powerful text display component.

### Alignment Options

```cpp
auto label = std::make_shared<NUILabel>();
label->setText("Centered Text");
label->setAlignment(NUILabel::Alignment::Center);

label->setAlignment(NUILabel::Alignment::Left);   // Left-aligned
label->setAlignment(NUILabel::Alignment::Right);  // Right-aligned
```

### Styling

```cpp
// Color customization
label->setTextColor(themeManager.getColor("textPrimary"));

// Font size (pixels)
label->setFontSize(14.0f);
```

## Event System Deep Dive

### Component Hierarchy

Events flow through the component tree:
1. **Root Component** receives platform events
2. **Child Components** get first chance to handle events
3. **Hover State** calculated after event processing
4. **Redraw** triggered only for components that changed

### Mouse Event Structure

```cpp
struct NUIMouseEvent {
    NUIPoint position;      // Screen coordinates
    bool pressed;           // Mouse button pressed
    bool released;          // Mouse button released
    float wheelDelta;       // Scroll wheel movement
    NUIMouseButton button;  // Which button
};
```

### Best Practices

#### ✅ Do
- Set proper bounds for all interactive components
- Handle events in child components before parent
- Update hover states after event processing
- Use consistent coordinate systems

#### ❌ Avoid
- Manual hover state management (let the system handle it)
- Event consumption without proper bounds checking
- Inconsistent color schemes across components

## Performance Considerations

### Efficient Rendering
- Only dirty components trigger redraws
- Batch rendering operations when possible
- Use appropriate component hierarchy depth

### Memory Management
- Components are reference-counted (std::shared_ptr)
- Proper cleanup on component destruction
- Minimal object allocation during runtime

---

## Testing

Run the component tests:
```bash
cmake --build build --config Release --target NomadUI_ComponentTests
```

## Examples

See complete implementations in the `Examples/` directory:
- `ButtonDemo.cpp` - Comprehensive button showcase
- `WidgetCatalogueDemo.cpp` - All components in action
