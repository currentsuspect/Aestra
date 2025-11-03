# NOMAD UI Configuration System

This directory contains YAML configuration files that control all visual aspects of the NOMAD DAW interface.

## Files

- `nomad_ui_config.yaml` - Main configuration file controlling colors, layout, spacing, and typography

## How to Customize

### 1. Edit Configuration
Open `nomad_ui_config.yaml` and modify any values:

```yaml
# Change track height
layout:
  trackHeight: 100.0  # Default: 80.0

# Change accent color
colors:
  primary: "#ff6b35"  # Orange instead of cyan

# Adjust spacing
spacing:
  panelMargin: 15.0   # More space around panels
```

### 2. Rebuild Application
After making changes, rebuild the application:

```bash
cmake --build build --config Release --target NOMAD_DAW
```

### 3. Run Application
The new settings will be applied automatically.

## Configuration Sections

### Colors
Control all UI colors including backgrounds, accents, text, and interactive states.

```yaml
colors:
  backgroundPrimary: "#121214"     # Main canvas
  primary: "#00bcd4"              # Accent color
  textPrimary: "#e6e6eb"          # Main text
  accentCyan: "#00bcd4"           # Transport controls
```

### Layout
Configure all panel sizes, track dimensions, and spacing:

```yaml
layout:
  fileBrowserWidth: 200.0         # Left sidebar width
  trackControlsWidth: 150.0       # Track controls panel
  trackHeight: 80.0               # Individual track height
  transportBarHeight: 60.0        # Transport controls height
```

### Spacing
Control margins, padding, and gaps throughout the interface:

```yaml
spacing:
  panelMargin: 10.0               # Space around panels
  componentPadding: 8.0           # Internal component padding
  trackSpacing: 5.0               # Space between tracks
```

### Typography
Adjust font sizes for different UI elements:

```yaml
typography:
  fontSizeM: 14.0                 # Standard text
  fontSizeL: 16.0                 # Large text
  fontSizeH1: 32.0                # Main headings
```

## Advanced Features

### Component-Specific Settings
Fine-tune individual UI components:

```yaml
components:
  trackControls:
    muteButtonSize: [25.0, 20.0]   # M button dimensions
    buttonSpacing: 5.0              # Space between buttons

  transportBar:
    playButtonSize: [40.0, 40.0]    # Play button size
    buttonSpacing: 8.0              # Transport button spacing
```

### Theme Variants
Create multiple themes:

```yaml
themes:
  light:
    extends: "default"
    colors:
      backgroundPrimary: "#ffffff"
      textPrimary: "#000000"

  highContrast:
    extends: "default"
    colors:
      primary: "#ffff00"              # High contrast yellow
```

## Tips for Pixel-Perfect Design

1. **Start Small**: Make incremental changes and rebuild often
2. **Test Responsiveness**: Check how changes affect window resizing
3. **Maintain Proportions**: Keep related dimensions proportional
4. **Consider Accessibility**: Ensure good contrast ratios
5. **Document Changes**: Comment your customizations in the YAML file

## Troubleshooting

### Configuration Not Loading
- Ensure YAML syntax is valid
- Check file permissions
- Verify the file path in code

### UI Not Updating
- Make sure to rebuild after changes
- Check console for error messages
- Verify all required keys are present

### Performance Issues
- Avoid extremely large dimensions
- Test on target hardware
- Consider the impact on layout calculations

## Examples

See the included `nomad_ui_config.yaml` for a complete example with all available options and detailed comments.