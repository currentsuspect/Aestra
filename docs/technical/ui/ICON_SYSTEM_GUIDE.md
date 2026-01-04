# NomadUI Icon System Guide

## Overview

NomadUI features a powerful SVG-based icon system that integrates seamlessly with the Nomad theme. Icons are scalable, theme-aware, and easy to use.

## Features

- ✅ **SVG-based** - Crisp at any size
- ✅ **Theme-integrated** - Automatically use theme colors
- ✅ **Lightweight** - No external dependencies
- ✅ **10 built-in icons** - Common UI icons included
- ✅ **Custom SVG support** - Load your own icons
- ✅ **Multiple sizes** - 16px, 24px, 32px, 48px presets

## Quick Start

### Using Built-in Icons

```cpp
#include "Core/NUIIcon.h"

// Create an icon
auto cutIcon = NUIIcon::createCutIcon();
cutIcon->setIconSize(NUIIconSize::Medium); // 24px
cutIcon->setPosition(100, 100);

// Render it
cutIcon->onRender(renderer);
```

### Available Built-in Icons

| Icon | Function | Default Color |
|------|----------|---------------|
| Cut | `createCutIcon()` | textPrimary |
| Copy | `createCopyIcon()` | textPrimary |
| Paste | `createPasteIcon()` | textPrimary |
| Settings | `createSettingsIcon()` | textPrimary |
| Close | `createCloseIcon()` | textPrimary |
| Minimize | `createMinimizeIcon()` | textPrimary |
| Maximize | `createMaximizeIcon()` | textPrimary |
| Check | `createCheckIcon()` | success |
| Chevron Right | `createChevronRightIcon()` | textSecondary |
| Chevron Down | `createChevronDownIcon()` | textSecondary |

## Icon Sizes

```cpp
// Predefined sizes
icon->setIconSize(NUIIconSize::Small);   // 16px
icon->setIconSize(NUIIconSize::Medium);  // 24px (default)
icon->setIconSize(NUIIconSize::Large);   // 32px
icon->setIconSize(NUIIconSize::XLarge);  // 48px

// Custom size
icon->setIconSize(20.0f, 20.0f);
```

## Theme Integration

### Using Theme Colors

```cpp
// Use a theme color
icon->setColorFromTheme("primary");      // Nomad purple
icon->setColorFromTheme("success");      // Green
icon->setColorFromTheme("error");        // Red
icon->setColorFromTheme("textPrimary");  // Main text color

// Or set a custom color
icon->setColor(NUIColor(1.0f, 0.0f, 0.0f, 1.0f)); // Red

// Clear custom color (use original SVG colors)
icon->clearColor();
```

### Available Theme Colors

- `textPrimary` - Main text (#E5E5E8)
- `textSecondary` - Muted text (#A6A6AA)
- `primary` - Nomad purple (#8B7FFF)
- `success` - Green (#5BD896)
- `warning` - Amber (#FFD86B)
- `error` - Red (#FF5E5E)
- `info` - Cyan (#6BCBFF)

## Custom SVG Icons

### Loading from String

```cpp
const char* customSVG = R"(
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
        <circle cx="12" cy="12" r="10"/>
        <line x1="12" y1="8" x2="12" y2="12"/>
        <line x1="12" y1="16" x2="12.01" y2="16"/>
    </svg>
)";

auto icon = std::make_shared<NUIIcon>(customSVG);
icon->setColorFromTheme("info");
```

### Loading from File

```cpp
auto icon = std::make_shared<NUIIcon>();
icon->loadSVGFile("assets/icons/my-icon.svg");
icon->setColorFromTheme("primary");
```

## SVG Format Requirements

### Supported Elements

- `<path>` - Full support for M, L, H, V, C, Z commands
- `<line>` - Straight lines
- `<circle>` - Circles
- `<rect>` - Rectangles
- `<polyline>` - Multi-point lines

### Supported Attributes

- `viewBox` - Defines coordinate system
- `fill` - Fill color (or "none")
- `stroke` - Stroke color (or "none")
- `stroke-width` - Line width
- `d` - Path data

### Best Practices

1. **Use viewBox** - Always include `viewBox="0 0 24 24"` for proper scaling
2. **Use stroke** - Prefer stroke over fill for better theme integration
3. **Set stroke-width** - Use `stroke-width="2"` for consistent line weight
4. **Use currentColor** - Use `stroke="currentColor"` to enable theme colors
5. **Keep it simple** - Avoid complex gradients or filters

### Example SVG Template

```xml
<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
    <!-- Your icon paths here -->
    <path d="M..."/>
</svg>
```

## Integration Examples

### In Context Menu

```cpp
// Add icon to menu item
auto menuItem = std::make_shared<NUIContextMenuItem>("Settings");
auto icon = NUIIcon::createSettingsIcon();
icon->setIconSize(NUIIconSize::Small);
menuItem->setIcon(icon); // Future feature
```

### In Buttons

```cpp
// Create button with icon
auto button = std::make_shared<NUIButton>();
button->setText("Save");

auto icon = NUIIcon::createCheckIcon();
icon->setIconSize(NUIIconSize::Small);
button->setIcon(icon); // Future feature
```

### In Custom Components

```cpp
class MyComponent : public NUIComponent {
private:
    std::shared_ptr<NUIIcon> icon_;
    
public:
    MyComponent() {
        icon_ = NUIIcon::createSettingsIcon();
        icon_->setIconSize(NUIIconSize::Medium);
        icon_->setColorFromTheme("primary");
    }
    
    void onRender(NUIRenderer& renderer) override {
        // Position icon
        icon_->setPosition(10, 10);
        
        // Render icon
        icon_->onRender(renderer);
    }
};
```

## Performance Tips

1. **Reuse icons** - Create once, render many times
2. **Cache parsed SVGs** - Don't parse the same SVG repeatedly
3. **Use appropriate sizes** - Don't render 48px icons at 16px
4. **Batch rendering** - Render multiple icons in one pass when possible

## SVG Parser Limitations

Current implementation supports:
- ✅ Basic path commands (M, L, H, V, C, Z)
- ✅ Lines, circles, rectangles
- ✅ Fill and stroke colors
- ✅ ViewBox transformation

Not yet supported:
- ❌ Gradients
- ❌ Filters
- ❌ Text elements
- ❌ Animations
- ❌ Complex transforms

## Creating Custom Icons

### Design Guidelines

1. **24x24 grid** - Design on a 24x24 pixel grid
2. **2px stroke** - Use 2px stroke weight
3. **Rounded caps** - Use rounded line caps for friendlier look
4. **Consistent style** - Match the style of built-in icons
5. **Simple shapes** - Avoid overly complex paths

### Export Settings

When exporting from design tools:
- Format: SVG
- Decimal precision: 2
- Remove unnecessary attributes
- Optimize paths
- Use relative commands when possible

## Troubleshooting

### Icon not rendering
- Check if SVG is valid
- Verify viewBox is set
- Ensure icon has size set
- Check if color is visible against background

### Icon too small/large
- Use `setIconSize()` to adjust
- Check viewBox dimensions
- Verify bounds are set correctly

### Wrong color
- Use `setColorFromTheme()` for theme colors
- Check if SVG uses `currentColor`
- Verify theme is loaded

## Future Enhancements

Planned features:
- Icon font support
- Animated icons
- Icon library manager
- Automatic icon caching
- More built-in icons
- Icon search/browse UI

## See Also

- [NOMAD_COLOR_SYSTEM.md](NOMAD_COLOR_SYSTEM.md) - Theme colors
- [THEME_DEMO_GUIDE.md](THEME_DEMO_GUIDE.md) - Theme integration
- IconDemo.cpp - Complete example
