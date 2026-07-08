# UI Pass — 2026-05

Branch: `feature/ui-pass-2026-05`
Base: `develop` @ `17f46681`
Scope: Layout + visual refinement across **Timeline, Arsenal, Audition + window chrome**.
Constraints: No DSP, no serialization, no CI changes. Visual + layout only. One slice per commit.
Build cmd: `cmake --build build -j2` (low parallelism per user RAM constraint).

## Audit Findings

- `NUIThemeManager` token system in `@/home/currentsuspect/Dev/Aestra/AestraUI/Core/NUIThemeSystem.h` and `@/home/currentsuspect/Dev/Aestra/AestraUI/Core/NUIThemeSystem.cpp` is already comprehensive: spacing (XS..XXL), radii, font sizes, animation durations, z-index, shadows, glass tokens, mixer tokens.
- Aestra Dark palette (`createAestraDark`) matches the screenshot: primary `#7c3aed` purple, surfaces `#0a0a0f → #20202c`, text white-alpha tiers.
- Header comments in `NUIThemeSystem.h:47-52` are stale (claim cyan accent `#00bcd4`; actual is `#00e5cc` and primary is purple). Cosmetic, low-value.
- `on*` text-on-color fields (`onBackground`, `onPrimary`, etc.) declared in struct but never assigned in `createAestraDark`. Unused at call sites outside the header. Per AGENTS.md "do not fix dead code unless targeted" — leave alone.
- `LayoutDimensions` has unused fields (`trackControlsWidth=236`, `timelineAreaWidth=800`, `controlButtonStartX=100`). Not removing.
- The "view toggle" segmented control (`m_viewToggle`, Arsenal/Timeline/Audition) is positioned with a hardcoded `toggleWidth=310.0f, toggleHeight=24.0f, yPos=4.0f` in `AestraContent::onResize` — not driven by tokens. Same for transport height (`56.0f` hardcoded alongside `layout.transportBarHeight=56`).

Conclusion: skip a standalone "tokens" commit. Tokens are healthy. Reuse existing tokens and only add new ones where genuinely needed inside each slice.

## Implementation Checklist

Each box becomes one commit on this branch. I stop after each for your review/build.

- [ ] **Slice A — Chrome & menu bar polish**
  - `@/home/currentsuspect/Dev/Aestra/AestraUI/Base/NUIMenuBar.h`: replace hardcoded `fontSize=12`, `paddingX=11`, hover alpha with theme tokens (`fontSizeS`, `spacingS`, `accentPrimary`); slightly increase hover contrast; cleaner active/pressed feedback.
  - `@/home/currentsuspect/Dev/Aestra/Source/App/AestraApp.cpp:534`: menu bar bounds — tighten height to match typography, align with title bar baseline.
  - Title bar / window control buttons (min/max/close): audit and tighten in `@/home/currentsuspect/Dev/Aestra/Source/Core/AestraWindowManager.*` if hardcoded.
  - Status badges ("Verified", "Founder"): make styling consistent — pill radius, accent treatment, typography token.

- [ ] **Slice B — Transport bar**
  - `@/home/currentsuspect/Dev/Aestra/Source/...` `Aestra::TransportBar` (Source-side, not the AestraUI widget stub): tighten button group spacing, BPM/TimeSig/Clock typography to `fontSizeM`/`fontSizeL`, ensure consistent vertical centering.
  - Replace hardcoded `56.0f` with `layout.transportBarHeight` in `AestraContent::onResize:1280` and the duplicate at line 1295 area.
  - Master VU + waveform alignment: keep current 60px meter + 150px waveform but recalc with token spacing.

- [ ] **Slice C — Tab switcher (Arsenal / Timeline / Audition)**
  - `@/home/currentsuspect/Dev/Aestra/Source/Core/AestraContent.cpp:1299-1320`: drive `toggleWidth/Height/yPos` from layout tokens or named constants; vertically center within transport.
  - `@/home/currentsuspect/Dev/Aestra/AestraUI/Base/NUISegmentedControl.h`: minor — keep Pill style but unify radius with `radiusL`, padding from `spacingXS`, font from `fontSizeS`.
  - Scope label (`m_scopeLabel`) positioning: tokenize.

- [ ] **Slice D — Library / browser panel**
  - File browser search bar height + typography.
  - Collections / Categories / Places section headers: consistent weight, uppercase tracking, color from `textSecondary`.
  - List row density, icon size, selected/hover states using `selected`/`hover` tokens.
  - Divider/resize rail in `AestraContent::onRender` `drawDividerRail` — already decent, just sync colors with tokens.

- [ ] **Slice E — Timeline view (track headers + ruler + lanes)**
  - Track header buttons (M / S / R / arm) — consistent sizing, hover/pressed feedback, color-coded states (R = `error`, M = `warning`, S = `info` or `accentSecondary`).
  - Track row alternating background using `backgroundPrimary` / `backgroundSecondary` with low alpha.
  - Ruler typography + bar number color from `textSecondary`.
  - Playhead color = `primary`, 1px crisp line.

- [ ] **Slice F — Arsenal view**
  - `ArsenalPanel` — pass over with new tokens. Keep behavior identical.

- [ ] **Slice G — Audition view**
  - `AuditionPanel` — pass over with new tokens. Keep behavior identical.

- [ ] **Slice H — Cross-tab consistency sweep + screenshots**
  - Walk all three views; fix any remaining hardcoded colors/spacings discovered in use.
  - Capture before/after screenshots (user-driven on their machine).

## Validation Per Slice

After each slice:

```bash
cmake --build build -j2
```

Targets that matter for UI compile:
- `Aestra` (main app)
- Or, for faster check: `AestraUI_Core`, `AestraUI_Platform`, `AestraUI_OpenGL`

If the slice touches only headers used widely, expect longer rebuilds.

## Out of Scope

- DSP, audio engine, real-time paths.
- Serialization / project format.
- Plugin host, scanner, OOP host.
- CI workflows.
- Public docs / pricing / roadmap claims.
- Light theme (`createAestraLight`) — not visible in screenshot, not in current scope.
