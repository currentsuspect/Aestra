# Nomad Theme Demo Guide

## Overview

The CustomWindowDemo now showcases the complete Nomad color system with an interactive context menu and visual color palette display.

## What's New

### 1. Complete Color Palette Integration

The demo now displays all major color categories from the Nomad color system:

- **Core Structure** (4 layers):
  - Background Primary (#181819)
  - Background Secondary (#1e1e1f)
  - Surface Tertiary (#242428)
  - Surface Raised (#2c2c31)

- **Accent Colors**:
  - Primary (#8B7FFF)
  - Primary Hover (#A79EFF)
  - Primary Pressed (#665AD9)

- **Status Colors**:
  - Success (#5BD896)
  - Warning (#FFD86B)
  - Error (#FF5E5E)
  - Info (#6BCBFF)

### 2. Interactive Context Menu

Right-click anywhere in the window to open a context menu that demonstrates:

- **Themed Colors**: Uses `surfaceTertiary` for background, `borderActive` for borders
- **Hover Effect**: Purple highlight (#8B7FFF at 15% alpha) matching the theme system
- **Text Colors**: Primary text (#E5E5E8) and secondary text (#A6A6AA) for shortcuts
- **Menu Items**: Cut, Copy, Paste with keyboard shortcuts
- **Submenus**: Theme switcher (Nomad Dark/Light)
- **Checkboxes**: Show Grid, Snap to Grid
- **Separators**: Using subtle border color (#2c2c2f)

### 3. Visual Color Swatches

At the bottom of the window, you'll see color swatches displaying:
- 4 background layers (BG1, BG2, Surf, Card)
- 2 accent colors (Accent, Hover)
- 4 status colors (OK, Warn, Error, Info)

Each swatch is labeled and bordered to show the color clearly.

## Running the Demo

```bash
# Build the demo
cmake --build build --target CustomWindowDemo

# Run it
./build/NomadUI/Examples/CustomWindowDemo
```

## Controls

- **F11**: Toggle full screen
- **Escape**: Exit full screen
- **Right-click**: Open context menu
- **Left-click**: Close context menu
- **Drag title bar**: Move window
- **Window controls**: Minimize, maximize, close

## Theme Features Demonstrated

### Layered Backgrounds
The window uses `backgroundPrimary` as the main canvas, while the context menu uses `surfaceTertiary` to appear "lifted" above the surface.

### Interactive States
- **Hover**: Purple highlight with 15% alpha
- **Pressed**: Darker purple (#665AD9)
- **Selected**: Purple with 15% alpha (matching context window highlight)

### Typography
- **Primary Text**: #E5E5E8 (bright, high contrast)
- **Secondary Text**: #A6A6AA (labels, shortcuts)
- **Link Text**: #8B7FFF (purple for actions)

### Borders
- **Subtle**: #2c2c2f (dividers, separators)
- **Active**: #8B7FFF (focused/selected elements)

## Code Examples

### Using Theme Colors in Your Components

```cpp
auto& themeManager = NUIThemeManager::getInstance();

// Get layered backgrounds
NUIColor canvas = themeManager.getColor("backgroundPrimary");
NUIColor panel = themeManager.getColor("backgroundSecondary");
NUIColor dialog = themeManager.getColor("surfaceTertiary");

// Get accent colors
NUIColor accent = themeManager.getColor("primary");
NUIColor accentHover = themeManager.getColor("primaryHover");

// Get status colors
NUIColor success = themeManager.getColor("success");
NUIColor error = themeManager.getColor("error");

// Get text colors
NUIColor text = themeManager.getColor("textPrimary");
NUIColor label = themeManager.getColor("textSecondary");
```

### Creating a Themed Context Menu

```cpp
auto contextMenu = std::make_shared<NUIContextMenu>();

// Apply Nomad theme
auto& themeManager = NUIThemeManager::getInstance();
contextMenu->setBackgroundColor(themeManager.getColor("surfaceTertiary"));
contextMenu->setBorderColor(themeManager.getColor("borderActive"));
contextMenu->setTextColor(themeManager.getColor("textPrimary"));
contextMenu->setHoverColor(themeManager.getColor("primary"));
contextMenu->setSeparatorColor(themeManager.getColor("borderSubtle"));
contextMenu->setShortcutColor(themeManager.getColor("textSecondary"));

// Add items
contextMenu->addItem("Cut", []() { /* ... */ });
contextMenu->addItem("Copy", []() { /* ... */ });
contextMenu->addSeparator();
contextMenu->addCheckbox("Option", false, [](bool checked) { /* ... */ });
```

## Next Steps

Try modifying the demo to:
1. Add more interactive elements (buttons, sliders, inputs)
2. Create custom components using the theme colors
3. Implement theme switching animation
4. Add more context menu items with icons
5. Create a settings panel using the layered background system

## See Also

- [NOMAD_COLOR_SYSTEM.md](NOMAD_COLOR_SYSTEM.md) - Complete color palette reference
- [CUSTOM_WINDOW_INTEGRATION.md](CUSTOM_WINDOW_INTEGRATION.md) - Custom window guide
- NUIThemeSystem.h - Theme system API reference
