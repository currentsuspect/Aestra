# Aestra UI Type & Space Grammar

The rules the interface follows so every margin, size, and radius reads as
one decision. Aestra is a **dense instrument**, not a generic app — its
vocabulary starts at 9px, not 12px.

Source of truth: `NUIThemeProperties` in `AestraUI/Core/NUIThemeSystem.h`.
Access at call sites via the theme, never a raw literal:

```cpp
auto& tm = AestraUI::NUIThemeManager::getInstance();
tm.getFontSize("s");   // or theme.fontSizeS
tm.getRadius("m");     // or theme.radiusM
tm.getSpacing("m");    // or theme.spacingM
```

## Type scale

| token       | px | role                                             |
| ----------- | -- | ------------------------------------------------ |
| `micro`     | 9  | meter ticks, tiny inline values, ruler subticks  |
| `xs`        | 10 | dense / secondary labels — the DAW workhorse     |
| `s`         | 11 | control labels, list rows                        |
| `m`         | 12 | primary body, emphasized rows                    |
| `l`         | 14 | section titles                                   |
| `xl`        | 16 | panel headers                                    |
| `display-s` | 24 | secondary numeric readouts                       |
| `display-l` | 32 | primary numeric readouts (BPM, transport time)   |

Steps are perceptually distinct on purpose — no 1px neighbours. When a legacy
literal falls between steps, round to the nearer token; historical fractional
sizes (8.2, 10.5, 11.5…) were drift, not intent, and collapse into the scale.

`fontSizeXXL/H1/H2/H3` remain only until their surfaces migrate — do not use
them in new code.

## Radius scale

| token | px | role                          |
| ----- | -- | ----------------------------- |
| `xs`  | 3  | chips, tiny toggles, ticks    |
| `s`   | 5  | small controls, pills, cells  |
| `m`   | 7  | buttons, cards, group shells  |
| `l`   | 10 | panels, large cards           |
| `xl`  | 14 | modals, floating surfaces     |

Recentered onto the 5–7px mass the UI already lives in. Sub-2px corners are
hairline strokes, not component radii — leave those literal.

## Spacing scale (unchanged)

`xs` 4 · `s` 8 · `m` 16 · `l` 24 · `xl` 32 · `xxl` 48

## Semantic color roles

Use semantic roles for shared application structure and state. Component-specific colors are reserved for information
that has domain meaning, such as track identity, EQ bands, meters, and analyzer curves.

| role | purpose |
| --- | --- |
| `appBackground`, `workspaceBackground` | root and workspace canvas |
| `elevatedPanel`, `recessedPanel` | floating and inset panel hierarchy |
| `controlBackground`, `controlHover`, `controlPressed`, `controlDisabled` | neutral control states |
| `borderSubtle`, `borderStrong`, `focusRing` | structural, interactive, and keyboard-focus edges |
| `textPrimary`, `textSecondary`, `textMuted`, `textDisabled` | text hierarchy |
| `selection` | persistent selected state; hover must not reuse it |
| `armed`, `muted`, `soloed`, `bypassed` | transport/mixer/plugin status |
| `dragTarget` | valid drag destination feedback |
| `meterBackground`, `meterActive` | shared meter structure |
| `gridMajor`, `gridMinor` | timeline and editor grids |
| `overlay`, `backdrop`, `shadow` | modal scrim, secondary scrim, and floating-surface depth |

State priority for shared controls is deterministic: disabled, pressed, selected, hovered, then idle. Focus changes the
border treatment without changing geometry. `resolveControlColors()` implements this ordering.

The JSON-backed `NUITheme` accepts the same semantic color names in its `colors` object. New keys are optional: loading
starts from defaults, so older theme files remain valid and inherit missing roles.

Preset scrims must remain translucent. An opaque `overlay` hides application context behind dialogs; primary action
foregrounds must use `textOnPrimary` so their contrast follows the accent rather than a local text constant.

Live application code should use the canonical names above. The manager retains compatibility aliases for historical
call sites and extensions: `backgroundTertiary` -> `elevatedPanel`, `surfaceSecondary` -> `surfaceRaised`,
`borderPrimary`/`borderSecondary` -> `borderStrong`/`borderSubtle`, `textTertiary` -> `textMuted`, `textInfo` -> `info`,
`textOnAccent` -> `textOnPrimary`, `inputBackground` -> `inputBgDefault`, and `accentAmber` -> `warning`. Unknown names
still use the legacy primary fallback, so new call sites must not depend on fallback behavior.

## Compact control metrics

| token | px | role |
| --- | ---: | --- |
| `compactControlHeight` | 24 | dense tool and inline controls |
| `standardControlHeight` | 28 | ordinary controls |
| `dialogActionHeight` | 36 | modal actions |
| `standardRowHeight` | 28 | list/property rows |
| `compactMenuRowHeight` | 24 | dense menus |
| `standardMenuRowHeight` | 28 | ordinary menus |
| `panelHeaderHeight` | 32 | panel title bars |
| `sectionHeaderHeight` | 24 | in-panel section headers |
| `standardIconSize` | 16 | ordinary control icons |
| `minimumHitArea` | 24 | smallest compact desktop hit target |
| `dividerWidth` | 1 | structural separators |
| `panelPadding` | 8 | dense panel inset |
| `dialogPadding` | 16 | modal inset |

These are defaults, not forced geometry. Large transport controls, knobs, artwork, waveforms, graphs, and optical glyph
offsets remain surface-specific.

## Transient surface lifecycle

Dialogs that cover the application use full root-window bounds and center their visible panel within that coordinate
space. Lazily created dialogs must receive the root's current bounds before their first layout; they cannot rely on an
earlier resize callback.

Menus and popovers are inserted frontmost only after the mouse/key dispatch that opened them has unwound. Both child
addition and removal are deferred during dispatch so callbacks cannot invalidate a parent hierarchy mid-iteration.
Visual states may invalidate the UI, but opening an external Linux file chooser must not block the Aestra event loop.

## Rollout

1. **PR 1** — define this grammar + pilot on the shell (timeline, browser,
   transport, track headers) to prove the taste in known surfaces.
2. **PR 2** — mixer.
3. **PR 3** — plugin editors.

Not a blind mechanical sweep: each surface is migrated by intent (a 22px BPM
readout becomes `display-s`, a 10px label becomes `xs`) with before/after
screenshots, not a global find-replace.
