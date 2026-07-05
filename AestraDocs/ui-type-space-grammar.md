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

## Rollout

1. **PR 1** — define this grammar + pilot on the shell (timeline, browser,
   transport, track headers) to prove the taste in known surfaces.
2. **PR 2** — mixer.
3. **PR 3** — plugin editors.

Not a blind mechanical sweep: each surface is migrated by intent (a 22px BPM
readout becomes `display-s`, a 10px label becomes `xs`) with before/after
screenshots, not a global find-replace.
