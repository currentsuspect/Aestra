# UI Consistency Audit — 2026-07

Branch: `feature/ui-consistency-foundation`
Base: `develop` at `9826d4bf`
Scope: application UI structure, theme readiness, recurring visual states, and shared controls.
Constraints: no redesign, no audio/DSP or persistence changes, no expensive paint effects, and no layout inflation.

## Method and evidence

This audit inspected the current theme implementations, base controls, application shell, primary workspaces, mixer,
Arsenal/pattern surfaces, plugin hosting/editors, dialogs, menus, browser/navigation, inspectors, overlays, tooltips,
scroll containers, and representative state logic.

Repository-wide evidence at the audit SHA:

- The two theme implementations are split: `NUITheme` loads and validates JSON, while the running application activates
  `NUIThemeManager`/`NUIThemeProperties`. No application startup path installs an `NUITheme` on the component tree.
- Of 667 C++ headers and implementations under `Source/` and `AestraUI/`, 69 reference `NUIThemeManager`; 65 contain
  direct/local color construction. The scan found 778 direct color constructions. This count includes justified DSP
  visualizations and editor-specific band colors, so it is evidence of bypass paths rather than a mechanical cleanup
  target.
- `TrackManagerUI.cpp` repeats the same 38 px header, 28 px ruler, 24 px horizontal scrollbar, and 15 px scrollbar
  dimensions in several layout and input paths. Those values are internally coherent but not centrally named.
- Shared controls disagree with the documented dense-instrument grammar. Examples include 11.5/11.75 px segmented
  labels, 13 px context-menu rows, local 8 px dropdown radii, and base checkbox colors from an older purple palette.
- Selection fills vary materially between equivalent list/row surfaces: approximately 0.075 in track headers, 0.10 in
  pattern rows, 0.16/0.22 in browser rows, and locally derived values in plugin lists.
- `PluginBrowserPanel.cpp` stores theme-derived colors in function-static values. Those values cannot follow a later
  theme switch or reload.
- The browser shell stores independent width preferences for Current Projects, Plugins, and Patterns. Changing navigation
  therefore replaces the content and the shell width together, even though all three views occupy the same panel.
- Global tooltips use hardcoded background, border, text, radius, padding, and 10 px type instead of the live theme.

## Severity inventory

### Critical — blocks predictable theming

1. **Split theme ownership.** JSON compatibility belongs to `NUITheme`, but most application components read the
   separate live manager. A JSON change can pass its tests while leaving the main UI unchanged.
2. **Base primitives can render without the live application theme.** `NUIButton`, parts of `NUICheckbox`, sliders,
   progress bars, and inherited-theme code use `NUIComponent::getTheme()`, which returns null at the root in the current
   startup path. Their fallback palettes then become an accidental third style source.
3. **Theme-derived static caches.** Plugin browser colors are initialized once from the active theme. Theme changes do
   not invalidate those values, contrary to theme-readiness expectations.
4. **Unknown live token names silently become accent purple.** Active call sites requested `backgroundTertiary`,
   `surfaceSecondary`, `borderPrimary`, `borderSecondary`, `textTertiary`, `textInfo`, `textOnAccent`,
   `inputBackground`, and `accentAmber`, but the manager did not map those names. Routing borders, muted text, input
   backgrounds, action foregrounds, and minimap audio tint therefore received the generic primary fallback.

### High — visible system drift

1. **Interactive state vocabulary is incomplete.** The manager has generic hover/pressed/focused/selected plus status
   colors, but no named armed, muted, soloed, bypassed, drag-target, meter-background, meter-active, grid-major, or
   grid-minor roles. Call sites derive these differently.
2. **Selected, active, and hover are visually conflated.** Browser, pattern, track, mixer, dropdown, and segmented-control
   surfaces use unrelated accent alpha values. Some hover states are accent-colored and can read as selection.
3. **Disabled behavior is fragmented.** The live theme defines disabled color/alpha, base controls have local disabled
   colors, and many components apply one-off opacity to text only. Some controls retain hover-looking fills when disabled.
4. **Equivalent controls lack shared metrics.** Common 20, 22, 24, 26, 28, 30, 32, and 36 px heights appear without a
   compact/standard/dialog-action rule. Icon sizes similarly vary around 10–18 px. This is most visible between transport,
   piano-roll tools, mixer actions, browser actions, and settings dialogs.
5. **Focus feedback is incomplete.** Text fields and dropdowns have local focus treatment; text/icon buttons, segmented
   controls, menu rows, and several custom clickable rows do not share a visible keyboard-focus rule.
6. **Menus and floating surfaces diverge.** Dropdowns, context menus, mixer plugin menus, export selectors, and settings
   navigation use different row heights, type sizes, borders, radii, hover fills, and disabled treatment.
7. **Export dialog draw/input geometry diverges.** Popup rows render at the 28 px standard-menu metric but hit testing
   still used 26 px. Its progress panel also rendered an inert Close button, and modal actions fired on press rather than
   a matching release.
8. **Lazy Export dialog had no window coordinate space.** It is created after the root's initial resize pass and was
   added with default 0 x 0 bounds. Centering its 520 x 330 panel therefore placed most of it at negative coordinates,
   leaving only a clipped corner at the desktop's top-left.
9. **Popup creation mutated the root during event dispatch.** Menu callbacks added a context menu while the root was
   still iterating its child vector for the release event. Removal was deferred, but addition was not; vector
   reallocation produced missing menus and transient black frames.

### Medium — recurring local drift

1. **Typography drift.** The documented grammar is 9/10/11/12/14/16 plus display sizes, but active surfaces still use
   7, 8, 8.5, 9.5, 10.5, 11.5, 11.75, 13, and 13.5 px. Tiny graph labels and specialized meters are intentional;
   ordinary controls and rows are not.
2. **Radius drift.** The intended 3/5/7/10/14 px scale coexists with generic 2, 4, 6, 8, 12, and 15 px component radii.
   Some are geometric necessities (circles, waveform clips); standard controls should use the scale.
3. **Spacing drift.** The shell and primary layouts mostly use 4/8/16 px rhythm, while controls frequently add 2, 3, 5,
   6, 10, 11, 12, or 14 px without a role. Optical offsets of 1–3 px remain valid for glyph centering.
4. **Border drift.** Most borders are 1 px, but selected rows and custom cards sometimes jump to 2 px and therefore
   change visual weight. Focus, selection, and structural borders lack explicit separate roles.
5. **Text truncation/alignment is inconsistent.** File browser and routing surfaces have local fit helpers; other list and
   menu labels can clip or use different baseline offsets. Numeric readouts do not consistently use a stable alignment.
6. **Scrollbars are not one system.** The base scrollbar has its own historical palette and dimensions while timeline,
   browser, piano roll, mixer, and plugin editors add local scrollbar widths and thumb treatments.
7. **Hit-area policy is implicit.** Visible 16–22 px actions sometimes use the same-size hit box, while transport controls
   are larger. No shared minimum hit-area metric expresses the compact desktop target.
8. **Old enhanced-control styling remains on live primitives.** The active checkbox/toggle/radio path still draws pulse
   rings, simulated gradients, and shadows; the mixer item selector uses an outer accent glow and 16 px arrow hit boxes.
9. **Out-of-range color construction exists in an active cursor path.** The timeline trim cursor used float-channel
   `NUIColor(255, 255, 255, 200)` rather than normalized values or a semantic text role.

### Low — documentation and maintenance drift

1. Theme comments still describe old cyan values in places where the live dark theme is purple-led.
2. `getColor()` silently returns primary accent for unknown names, making token typos look intentional.
3. Component-specific dimensions are hardcoded inside a string lookup function and do not form a documented scale.
4. Theme switching exposes a single callback slot and themed-component registration is still a stub. A broad observer
   rewrite is not justified in this pass, but reload behavior must be documented honestly.
5. Several live preset fields relied on `NUIColor`'s opaque-black default. In particular, `overlay` and
   `textOnPrimary` made a migrated confirmation dialog hide the workspace and render low-contrast primary text until the
   preset values were made explicit and covered by contrast/translucency checks.

## Surface inventory

| Surface | Evidence and inconsistency | Decision |
| --- | --- | --- |
| Application shell/chrome | Menu/title controls use local padding, radius, and accent hover; global tooltip is hardcoded. | Move shared chrome states to semantic tokens; preserve current 32 px title intent. |
| Toolbar/transport | Core 28 px transport buttons are coherent, but state colors and icon sizing are partly local. | Adopt shared compact metrics and semantic recording/focus roles; preserve grouping and meter widths. |
| Browser/navigation | Several selected alpha values, local row states, static plugin-browser colors, and per-view width preferences that make the shared shell jump. | Normalize list rows, remove theme-derived statics, and keep one shell width; preserve file-type colors. |
| Timeline/arrangement | Repeated layout constants and local grid/selection derivation. | Name repeated metrics and use grid/selection roles; preserve fractal-grid language and clip colors. |
| Mixer | Shared mixer widgets already centralize much styling, but mute/solo/arm/bypass states use local derivations. | Map states semantically; preserve meters, track colors, and dense strip geometry. |
| Arsenal/pattern | Step-grid tokens exist, but row selection, cards, play actions, and drag target use different state recipes. | Reuse shared state roles; preserve alternating hierarchy and step semantics. |
| Plugin rack/effect slots | Plugin browser, routing map, inspector, and rack slots disagree on bypass/selected/hover. | Normalize shell/slot states; preserve plugin type and routing-path colors. |
| Plugin editors | Editors intentionally use neutral surfaces and parameter/band identity colors; common labels/cards vary. | Normalize only shared neutral text, border, focus, bypass, and control metrics. Do not flatten band or DSP visualization colors. |
| Dialogs/settings | Confirmation, recovery, export, membership, and settings navigation have different action heights and row states. | Apply standard dialog padding/action metrics and shared menu/list states without resizing dialogs broadly. |
| Menus/popovers/context menus | Dropdown, context, export, and mixer plugin menus use different 10–13 px type and row geometry. | Establish compact/standard menu-row metrics and shared focus/disabled states. |
| Inspectors/property panels | Mixer and routing inspectors use local header/card typography and action styling. | Align headers/rows with semantic text and control metrics; preserve information density. |
| Empty/loading/error states | Implemented per surface with unrelated text alpha and status color use. | Centralize status/text roles; avoid a new empty-state framework. |
| Scroll containers | Multiple scrollbar widths and local colors. | Theme the base scrollbar and use semantic track/thumb roles where practical. |

## Intentionally preserved differences

- Plugin-editor band colors, analyzer curves, compressor/limiter graphs, meters, waveform colors, and track colors carry
  domain information and are not accidental palette drift.
- Primary transport play/record affordances may remain more prominent than ordinary icon buttons.
- Destructive close/delete actions retain an error role rather than the generic accent.
- Compact timeline, mixer, and plugin-editor controls may use the compact height while settings actions use standard or
  dialog-action height.
- Optical glyph offsets, graph tick labels below the normal type scale, clip geometry, and panel-specific large controls
  (knobs, album artwork, waveform displays) remain layout-specific.
- Neutral, flat plugin active states and the existing fractal-grid visual language remain unchanged.

## Implementation plan

### Slice 1 — semantic foundation and compatibility

- Add only missing semantic colors and practical shared dimensions to `NUIThemeProperties`.
- Map semantic names through `NUIThemeManager::getColor()` and expose named metrics through the existing dimension path.
- Mirror the semantic defaults in JSON-backed `NUITheme`; generic JSON loading keeps old files compatible because missing
  keys continue to inherit defaults and unknown existing keys remain accepted.
- Add deterministic state-resolution helpers for recurring control states. Helpers are pure value selection with no
  allocations, registration, parsing, or paint-time caching.

### Slice 2 — base controls and overlays

- Move button, segmented control, tooltip, dropdown/context-menu, text-input, checkbox, and scrollbar defaults onto the
  live semantic path while preserving explicit per-component overrides.
- Standardize compact/standard dimensions, focus rings, disabled treatment, and layout-stable state transitions.

### Slice 3 — application surfaces

- Remove stale static theme caches from plugin browser.
- Keep one width preference for the shared Current Projects/Patterns/Plugins browser shell; retain the separate Arsenal
  pattern rail preference because it is a distinct simultaneous panel.
- Normalize browser/list selection, track/mixer status roles, Arsenal drag/selection feedback, dialog/menu rows, and common
  panel headers where the audit found equivalent behavior.
- Resolve every color role currently requested by live components, migrate application call sites to canonical semantic
  names, and retain aliases only for compatibility.
- Align export popup drawing and hit testing, remove inert progress actions, and use release-matched modal actions.
- Give lazily created overlays the current root bounds before first layout, and defer popup additions/removals until the
  active event dispatch fully unwinds.
- Keep Linux's external save chooser off the UI thread so the Wayland event loop remains responsive while the chooser is
  open; poll and apply its result on the normal UI update path.
- Flatten the active checkbox/toggle/radio and mixer-selector state paths while preserving their geometry and callbacks.
- Name repeated timeline geometry without changing its values.

### Slice 4 — validation and evidence

- Extend JSON compatibility tests for missing/invalid semantic tokens.
- Add a theme-consistency test for semantic resolution, metric equality, contrast guardrails, and layout-stable states.
- Build the real `Aestra` UI target with `-j2`, run focused UI/theme tests, then broader relevant tests.
- Capture baseline and final major-surface screenshots from the running application when the local display session permits.

## Compatibility and performance strategy

- Existing JSON theme files remain valid: new keys are optional and defaults are installed before parsing.
- No project/session serialization, plugin IDs, parameter IDs, or audio behavior are touched.
- Theme resolution remains constant-time value lookup/selection. No JSON parsing occurs in render/layout paths.
- No new shadows, blur, per-frame allocations, observers, or animation loops are introduced.
- Geometry values used by interaction and render paths remain identical unless a shared metric replaces an equivalent
  literal in both places.

## Runtime visual evidence

The final desktop build was reviewed at both 671 x 714 and 1354 x 714 client sizes. The browser's right edge remained at
x=260 in Current Project, Plugins, and Patterns; the track header and timeline origin remained at the same x coordinate
while the embedded content changed. The modal evidence confirms that the workspace remains visible through the scrim and
that primary/destructive/focus states remain distinct. The Export path was exercised through File -> Export Audio ->
Browse: the in-app dialog remained centered, the native chooser opened at a usable size, Aestra continued rendering with
a layout-stable Waiting state, and no compositor hung-client dialog appeared after 11 seconds.

- `labs/ui-consistency/2026-07/final-current-projects.png`
- `labs/ui-consistency/2026-07/final-plugins.png`
- `labs/ui-consistency/2026-07/final-patterns.png`
- `labs/ui-consistency/2026-07/final-shell-timeline.png`
- `labs/ui-consistency/2026-07/final-confirmation-dialog.png`
- `labs/ui-consistency/2026-07/final-settings.png`
- `labs/ui-consistency/2026-07/final-membership.png`
- `labs/ui-consistency/2026-07/final-export.png`
- `labs/ui-consistency/2026-07/final-export-save-picker.png`
