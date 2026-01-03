# 🎨 NOMAD COLOR SYSTEM — v1.0

Complete color palette and theming guide for NomadUI.

## 🧱 1. Core Structure

Layered backgrounds create visual hierarchy — everything feels "layered," not flat.

| Role | Color | Hex | Description |
|------|-------|-----|-------------|
| **Primary Background** | `backgroundPrimary` | `#181819` | Deep matte black with slight warmth — your main canvas |
| **Secondary Background** | `backgroundSecondary` | `#1e1e1f` | Panels, sidebars, inactive areas — subtle contrast |
| **Tertiary Surface** | `surfaceTertiary` | `#242428` | Slightly lifted layer — dialogs, popups, mixer sections |
| **Raised Surface / Card BG** | `surfaceRaised` | `#2c2c31` | Highlighted containers or grouped UI components |

### Usage Example
```cpp
// Get layered backgrounds
NUIColor canvas = themeManager.getColor("backgroundPrimary");
NUIColor panel = themeManager.getColor("backgroundSecondary");
NUIColor dialog = themeManager.getColor("surfaceTertiary");
NUIColor card = themeManager.getColor("surfaceRaised");
```

---

## 💡 2. Accent & Branding

Core Nomad purple — use sparingly for highlights, active states, and selection glow.

| Role | Color | Hex | Description |
|------|-------|-----|-------------|
| **Accent / Brand** | `primary` | `#8B7FFF` | Core Nomad purple |
| **Accent Hover / Light** | `primaryHover` | `#A79EFF` | Hover or glow variant |
| **Accent Deep / Pressed** | `primaryPressed` | `#665AD9` | Pressed or focused button state |

### Usage Example
```cpp
NUIColor accent = themeManager.getColor("primary");
NUIColor accentHover = themeManager.getColor("primaryHover");
NUIColor accentPressed = themeManager.getColor("primaryPressed");
```

---

## 🧭 3. Text & Typography

High contrast text colors for readability.

| Role | Color | Hex | Description |
|------|-------|-----|-------------|
| **Primary Text** | `textPrimary` | `#E5E5E8` | Main text (bright, high contrast) |
| **Secondary Text** | `textSecondary` | `#A6A6AA` | Subtext, labels, muted sections |
| **Disabled Text** | `textDisabled` | `#5A5A5D` | Inactive states |
| **Link / Action Text** | `textLink` | `#8B7FFF` | Matches brand purple for interaction clarity |
| **Critical Text** | `textCritical` | `#FF5E5E` | Red for alerts or invalid inputs |

### Usage Example
```cpp
NUIColor mainText = themeManager.getColor("textPrimary");
NUIColor label = themeManager.getColor("textSecondary");
NUIColor link = themeManager.getColor("textLink");
```

---

## 🖱️ 4. Interactive Elements

Pre-configured colors for buttons, toggles, inputs, and sliders.

### Buttons

| State | Color | Hex |
|-------|-------|-----|
| **Default BG** | `buttonBgDefault` | `#242428` |
| **Hover BG** | `buttonBgHover` | `#2e2e33` |
| **Active BG** | `buttonBgActive` | `#8B7FFF` |
| **Default Text** | `buttonTextDefault` | `#E5E5E8` |
| **Active Text** | `buttonTextActive` | `#ffffff` |

### Toggle / Switch

| State | Color | Hex |
|-------|-------|-----|
| **Default** | `toggleDefault` | `#3a3a3f` |
| **Hover** | `toggleHover` | `#4a4a50` |
| **Active** | `toggleActive` | `#8B7FFF` |

### Input Fields

| State | Color | Hex |
|-------|-------|-----|
| **Default BG** | `inputBgDefault` | `#1b1b1c` |
| **Hover BG** | `inputBgHover` | `#1f1f20` |
| **Focus Border** | `inputBorderFocus` | `#8B7FFF` |

### Sliders

| Element | Color | Hex |
|---------|-------|-----|
| **Track** | `sliderTrack` | `#2a2a2e` |
| **Handle** | `sliderHandle` | `#8B7FFF` |
| **Handle Hover** | `sliderHandleHover` | `#A79EFF` |
| **Handle Pressed** | `sliderHandlePressed` | `#665AD9` |

### Usage Example
```cpp
// Button states
renderer.fillRect(buttonBounds, themeManager.getColor("buttonBgDefault"));
if (isHovered) {
    renderer.fillRect(buttonBounds, themeManager.getColor("buttonBgHover"));
}
if (isPressed) {
    renderer.fillRect(buttonBounds, themeManager.getColor("buttonBgActive"));
}

// Slider
renderer.fillRect(trackBounds, themeManager.getColor("sliderTrack"));
renderer.fillCircle(handlePos, handleRadius, themeManager.getColor("sliderHandle"));
```

---

## 🪞 5. Shadows, Borders & Highlights

| Role | Color / Style | Description |
|------|---------------|-------------|
| **Border Subtle** | `borderSubtle` `#2c2c2f` | Divider lines, subtle edges |
| **Border Active** | `borderActive` `#8B7FFF` | Selected or focused panels |
| **Shadow Low (ambient)** | `shadowM` | `rgba(0, 0, 0, 0.4)` for elevated surfaces |
| **Shadow High (floating UI)** | `shadowL` | `rgba(0, 0, 0, 0.6)` for modals, dropdowns, tooltips |
| **Highlight Edge Glow** | `highlightGlow` | `rgba(139, 127, 255, 0.3)` soft glow around active elements |

### Usage Example
```cpp
// Borders
NUIColor subtleBorder = themeManager.getColor("borderSubtle");
NUIColor activeBorder = themeManager.getColor("borderActive");

// Shadows
auto ambientShadow = themeManager.getShadow("m");
auto floatingShadow = themeManager.getShadow("l");

// Highlight glow
NUIColor glow = themeManager.getColor("highlightGlow");
```

---

## 🌈 6. Functional Colors (Status Feedback)

Make Nomad readable even under heavy interface layers like meters, automation graphs, and waveform views.

| State | Color | Hex | Description |
|-------|-------|-----|-------------|
| **Success / OK** | `success` | `#5BD896` | Green accent for confirmation / signal OK |
| **Warning** | `warning` | `#FFD86B` | Gentle amber for caution |
| **Error** | `error` | `#FF5E5E` | Vibrant red for danger |
| **Info** | `info` | `#6BCBFF` | Cyan-blue for system messages |

### Usage Example
```cpp
NUIColor successColor = themeManager.getColor("success");
NUIColor warningColor = themeManager.getColor("warning");
NUIColor errorColor = themeManager.getColor("error");
NUIColor infoColor = themeManager.getColor("info");
```

---

## 🧠 7. Environmental Lighting (Optional Shader Layer)

For extra polish, simulate subtle "light falloff":

- **Corners**: darken with a radial gradient (opacity 3–5%)
- **Center**: slightly lighter tone (`#1f1f20` overlay)

Makes the UI feel like it exists within a physical device.

### Implementation Suggestion
```cpp
// Apply subtle vignette effect
void applyEnvironmentalLighting(NUIRenderer& renderer, const NUIRect& bounds) {
    // Create radial gradient from center
    NUIColor centerOverlay(0.122f, 0.122f, 0.125f, 0.03f); // #1f1f20 at 3%
    NUIColor edgeOverlay(0.0f, 0.0f, 0.0f, 0.05f);         // Black at 5%
    
    renderer.fillRadialGradient(bounds, centerOverlay, edgeOverlay);
}
```

---

## 📋 Quick Reference

### Most Common Colors

```cpp
// Backgrounds (layered)
getColor("backgroundPrimary")    // #181819 - Main canvas
getColor("backgroundSecondary")  // #1e1e1f - Panels
getColor("surfaceTertiary")      // #242428 - Dialogs
getColor("surfaceRaised")        // #2c2c31 - Cards

// Accent
getColor("primary")              // #8B7FFF - Brand purple
getColor("primaryHover")         // #A79EFF - Hover state
getColor("primaryPressed")       // #665AD9 - Pressed state

// Text
getColor("textPrimary")          // #E5E5E8 - Main text
getColor("textSecondary")        // #A6A6AA - Labels
getColor("textLink")             // #8B7FFF - Links

// Status
getColor("success")              // #5BD896 - Green
getColor("warning")              // #FFD86B - Amber
getColor("error")                // #FF5E5E - Red
getColor("info")                 // #6BCBFF - Cyan
```

---

## 🎯 Design Principles

1. **Layered Hierarchy**: Use the 4-tier background system to create depth
2. **Sparing Accent Use**: Purple should highlight, not dominate
3. **High Contrast Text**: Always use `textPrimary` for important content
4. **Consistent Interactions**: Use pre-defined button/toggle/input colors
5. **Clear Status**: Functional colors should be immediately recognizable
6. **Subtle Shadows**: Ambient shadows for depth, floating shadows for modals
7. **Glow for Focus**: Use `highlightGlow` sparingly for active elements

---

## 🔄 Migration from Old Theme

If you're updating existing code:

```cpp
// Old
theme.background  // Was #1e1e1f

// New - Choose appropriate layer
theme.backgroundPrimary    // #181819 - Main canvas
theme.backgroundSecondary  // #1e1e1f - Panels (matches old)
theme.surfaceTertiary      // #242428 - Dialogs
theme.surfaceRaised        // #2c2c31 - Cards
```

The old `background` and `surface` properties still work for backward compatibility but map to the new layered system.
