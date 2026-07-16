# Aestra Theme System Contract

## Live ownership

`NUIThemeManager` is the single live theme owner. Components should read `NUIThemeProperties` or the manager's semantic accessors. `NUITheme` remains the backward-compatible JSON loader and per-component adapter; loading a file does not parse during paint or automatically mutate the application.

Built-in product themes are:

- `Aestra-dark`
- `Aestra-light`
- `high-contrast-dark`

The manager retains lower-case and older internal preset names for compatibility. Product settings should expose only supported Aestra choices.

## Activation and updates

- `setActiveTheme(name)` returns `false` for an unknown name and leaves the current theme unchanged.
- `switchTheme(name, duration)` currently performs the same atomic activation. A partial transition is deliberately not emitted because every semantic property would need a defined interpolation rule.
- Use `subscribeToThemeChanges()` and retain the returned ID. Unsubscribe before the owner is destroyed.
- `setOnThemeChanged()` is retained as one compatibility callback slot. New owners must use subscriptions so listeners cannot replace one another.
- `AestraWindowManager` owns the application subscription and propagates `onThemeChanged()` through the component hierarchy. Theme-aware caches should refresh in their override and then call `NUIComponent::onThemeChanged(theme)`.
- Explicit custom widget colors are preserved. Widgets using default colors refresh from the new active theme.

Theme updates are UI-thread operations. Subscribers must not call audio-thread code.

## JSON registration

Use:

```cpp
auto& themes = AestraUI::NUIThemeManager::getInstance();
if (themes.loadThemeFromFile("studio-custom", path, "Aestra-dark")) {
    themes.setActiveTheme("studio-custom");
}
```

The file is limited to 1 MiB. Missing fields inherit the requested base theme. Invalid entries are skipped according to the existing loader policy; malformed, incomplete, non-object, missing, or oversized files fail live registration and do not install a misleading fallback theme.

Colors use `#RRGGBB` or `#RRGGBBAA`. Unknown token names remain available through `NUITheme` for backward and forward compatibility but do not affect the live manager until a semantic mapping is documented.

### Live color roles

Preferred JSON names are:

- Surfaces: `appBackground`, `workspaceBackground`, `recessedPanel`, `elevatedPanel`, `surfaceRaised`
- Controls: `controlBackground`, `controlHover`, `controlPressed`, `controlDisabled`
- Structure: `borderSubtle`, `borderStrong`, `borderActive`, `divider`
- Text: `textPrimary`, `textSecondary`, `textMuted`, `textDisabled`, `textLink`, `textCritical`, `textOnPrimary`
- Accent and selection: `accent`, `accentHover`, `accentPressed`, `selection`, `focusRing`, `dragTarget`
- Status: `success`, `warning`, `error`, `info`, `armed`, `muted`, `soloed`, `bypassed`
- Input: `toggleDefault`, `toggleHover`, `toggleActive`, `inputBackground`, `inputBgHover`, `inputBorderFocus`, `sliderTrack`, `sliderHandle`, `sliderHandleHover`, `sliderHandlePressed`
- Meter and grid: `meterSafe`, `meterWarn`, `meterCrit`, `meterBackground`, `meterActive`, `gridMajor`, `gridMinor`
- Overlays: `shadow`, `overlay`, `backdrop`

Legacy names such as `background`, `surface`, `primary`, `buttonBgDefault`, `buttonBgHover`, and `normal` font size remain supported.

When a JSON file overrides the primary accent but omits dependent roles, hover, pressed, selection, focus, drag target, link, toggle, input-focus, and slider roles are derived deterministically. Warning, error, and info similarly feed muted, armed, meter, critical-text, and solo roles when those roles are not explicitly overridden.

### Live dimensions and typography

Supported dimensions include the shared spacing scale, three legacy radius keys, common control/row/header heights, icon and hit-area metrics, divider width, panel/dialog padding, file-browser width, track-controls width, track height, and transport height.

Supported font keys are `micro`, `xs`, `s`, `m`, `l`, `xl`, `display-s`, and `display-l`; legacy `small`, `normal`, and `large` map to the compact live scale.

Positive layout and typography values are validated. Missing values retain the base preset.

## Compatibility

- Existing theme files still load through `NUITheme::loadFromFile()` and still receive defaults for missing or invalid entries.
- Preference key and serialization format are unchanged. Unknown saved theme names safely fall back to `Aestra-dark`.
- The component-level `setTheme()` API remains available for compatibility, but application styling should use the live manager.
- No project or plugin state format changes are involved.

## Performance

- JSON parsing and registration happen only on explicit load/reload paths.
- Activation performs one bounded UI-thread notification and hierarchy invalidation.
- Cached timeline layers are invalidated once; they are not rebuilt every frame.
- Theme switching does not add audio-thread allocation, locking, logging, file I/O, or callbacks.

## Intentional visual domains

Track colors, waveform rendering, plugin response graphs, meter thresholds, recording, mute, solo, bypass, warning/error status, and major/minor timeline grids remain separate roles. The arrangement grid may retain its dark fractal-workspace language inside a light chrome theme; it is not an ordinary panel background.

## Muse harness handoff

Muse should automate stable public actions: open Settings, select a theme by identity, apply or cancel, wait for root invalidation, navigate major surfaces, and capture screenshots. It should observe active theme identity and visible UI state instead of reaching into component color fields. Building that harness after this contract lands avoids coupling Muse to transitional theme internals.
