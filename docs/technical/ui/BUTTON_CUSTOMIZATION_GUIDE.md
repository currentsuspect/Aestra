# NomadUI Button Customization Guide

## Overview
The NomadUI button system provides extensive customization options for creating visually appealing and functional buttons. This guide demonstrates the available styles, colors, and behaviors.

## Button Styles

### 1. Primary Style
- **Purpose**: Main action buttons
- **Appearance**: Solid background with rounded corners and subtle border
- **Use Case**: Primary actions like "Save", "Submit", "Play"

```cpp
auto button = std::make_shared<NUIButton>("Primary");
button->setStyle(NUIButton::Style::Primary);
button->setBackgroundColor(NUIColor::fromHex(0xff4CAF50)); // Green
button->setTextColor(NUIColor::fromHex(0xffffffff));      // White
```

### 2. Secondary Style
- **Purpose**: Secondary actions
- **Appearance**: Outlined background with rounded corners
- **Use Case**: Secondary actions like "Cancel", "Back", "Settings"

```cpp
auto button = std::make_shared<NUIButton>("Secondary");
button->setStyle(NUIButton::Style::Secondary);
button->setBackgroundColor(NUIColor::fromHex(0xff2196F3)); // Blue
button->setTextColor(NUIColor::fromHex(0xffffffff));       // White
```

### 3. Text Style
- **Purpose**: Minimal text-only buttons
- **Appearance**: No background, just text
- **Use Case**: Links, subtle actions like "Learn More", "Details"

```cpp
auto button = std::make_shared<NUIButton>("Text Only");
button->setStyle(NUIButton::Style::Text);
button->setTextColor(NUIColor::fromHex(0xffFF9800)); // Orange
```

### 4. Icon Style
- **Purpose**: Icon-only buttons
- **Appearance**: Circular background with centered icon
- **Use Case**: Toolbar buttons, close buttons, action icons

```cpp
auto button = std::make_shared<NUIButton>("●");
button->setStyle(NUIButton::Style::Icon);
button->setBackgroundColor(NUIColor::fromHex(0xffE91E63)); // Pink
button->setTextColor(NUIColor::fromHex(0xffffffff));       // White
```

## Color Customization

### Background Colors
You can set any background color using hex values:

```cpp
// Material Design Colors
button->setBackgroundColor(NUIColor::fromHex(0xff4CAF50)); // Green
button->setBackgroundColor(NUIColor::fromHex(0xff2196F3)); // Blue
button->setBackgroundColor(NUIColor::fromHex(0xffFF9800)); // Orange
button->setBackgroundColor(NUIColor::fromHex(0xffE91E63)); // Pink
button->setBackgroundColor(NUIColor::fromHex(0xff9C27B0)); // Purple
button->setBackgroundColor(NUIColor::fromHex(0xffFF5722)); // Deep Orange
button->setBackgroundColor(NUIColor::fromHex(0xff607D8B)); // Blue Grey
```

### Text Colors
Customize text color for better contrast:

```cpp
button->setTextColor(NUIColor::fromHex(0xffffffff)); // White
button->setTextColor(NUIColor::fromHex(0xff000000)); // Black
button->setTextColor(NUIColor::fromHex(0xff333333)); // Dark Grey
```

## Button Behaviors

### Toggle Buttons
Create buttons that can be toggled on/off:

```cpp
auto toggleButton = std::make_shared<NUIButton>("Toggle");
toggleButton->setToggleable(true);
toggleButton->setOnToggle([this](bool toggled) {
    std::cout << "Toggle state: " << (toggled ? "ON" : "OFF") << std::endl;
});
```

### Disabled Buttons
Create non-interactive buttons:

```cpp
auto disabledButton = std::make_shared<NUIButton>("Disabled");
disabledButton->setEnabled(false);
disabledButton->setBackgroundColor(NUIColor::fromHex(0xff757575)); // Grey
disabledButton->setTextColor(NUIColor::fromHex(0xffBDBDBD));        // Light Grey
```

### Click Handlers
Set up click event handlers:

```cpp
button->setOnClick([this]() {
    std::cout << "Button clicked!" << std::endl;
    // Your action here
});
```

## Visual Examples

Here's what the different button styles look like in our demo:

```
┌─────────────────────────────────────────────────────────────┐
│                    NomadUI Button Styles Demo              │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────┐              │
│  │ Primary │ │Secondary│ │Text Only│ │  ●  │              │
│  └─────────┘ └─────────┘ └─────────┘ └─────┘              │
│                                                             │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐          │
│  │ Purple  │ │ Orange  │ │ Toggle  │ │Disabled │          │
│  └─────────┘ └─────────┘ └─────────┘ └─────────┘          │
│                                                             │
│  Click any button to see status                             │
│  Toggle: OFF                                                │
│  Different button styles and customizations                 │
└─────────────────────────────────────────────────────────────┘
```

## Integration with NOMAD

These buttons are designed to replace JUCE components in the main NOMAD application:

- **Replace `juce::TextButton`** with `NUIButton` (Primary/Secondary styles)
- **Replace `juce::Button`** with `NUIButton` (Icon style for custom buttons)
- **Replace `juce::Label`** with `NUILabel` for text display

## Next Steps

1. **Phase 1 Integration**: Start replacing simple JUCE buttons in MainComponent
2. **Add More Components**: Sliders, text editors, combo boxes
3. **Theme System**: Implement consistent theming across all components
4. **Event System**: Complete mouse/keyboard event handling
5. **Layout System**: Add automatic layout management

## Technical Notes

- All buttons use OpenGL for rendering
- Colors are specified in RGBA format (0xAARRGGBB)
- Buttons support hover and pressed states
- Event handling is callback-based for flexibility
- Components use smart pointers for memory management
