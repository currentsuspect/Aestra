# Full Theme System Audit — July 2026

## Scope and evidence

This audit follows the UI consistency foundation in PR #517. It inspects the active desktop theme path, JSON loading, preference persistence, settings UI, invalidation, cached styling, and shared controls. The findings come from the current `feature/full-theme-system` tree and the runtime evidence captured by the preceding consistency pass.

## Severity 1 — theme changes are not a live application contract

- Aestra has two independent theme representations. `NUITheme` loads map-based JSON, while rendered application code primarily reads `NUIThemeManager` and `NUIThemeProperties`. No startup path installs a loaded JSON theme into the live manager.
- `AestraWindowManager::initialize()` always activates `Aestra-dark`, despite `Preferences::theme` being loaded before window creation and serialized on shutdown.
- Appearance settings always select index zero, expose a nonexistent `Midnight Blue` option, and have no selection handler. Apply and Cancel only clear a local dirty flag.
- `NUIThemeManager` exposes one replaceable callback. A second interested surface silently disconnects the first, so application-wide invalidation and independent tooling cannot coexist.
- `NUIThemedComponent` registration is stubbed and unused. No reliable root-to-leaf notification exists.
- Animated `switchTheme()` interpolates only three legacy colors into a partially default-initialized theme, does not activate the target name, and completes by notifying listeners with the old active theme. This path is unsafe to expose as a theme feature.

Impact: theme files can pass parser tests without affecting the real UI, saved selection is ignored, and a live switch can leave cached and uncached components showing different themes.

## Severity 2 — cached theme values do not reliably refresh

- Mixer widgets, routing views, panel windows, the timeline minimap, and other surfaces cache colors during construction.
- `NUIIcon::setColorFromTheme()` resolves a token once and forgets the token name, so toolbar and transport icons cannot follow a later theme change.
- Some shared controls refresh theme colors during render while other controls resolve once. The resulting behavior depends on component implementation rather than a common lifecycle.
- Theme activation does not explicitly invalidate the root hierarchy. Idle frame elision can therefore leave a correctly changed manager with stale pixels until unrelated interaction occurs.

Impact: switching themes is nondeterministic across major surfaces and can require mouse movement or reconstruction to become visible.

## Severity 2 — JSON defaults and live defaults describe different products

- `NUITheme::createDefault()` retains an older palette, 12 px default radius, 15 px body text, and 28 px title text.
- `NUIThemeProperties` uses the compact desktop grammar established by the consistency pass: 3–7 px common radii and 9–14 px working typography.
- Missing JSON fields correctly receive parser defaults, but those defaults are not the defaults used by live controls.
- JSON accepts arbitrary token names for forward compatibility, but there is no documented mapping of accepted live semantic roles.

Impact: a partial theme file may be internally valid yet produce unexpected geometry if connected directly to the current UI.

## Severity 3 — remaining bypass paths

- A repository scan finds raw color construction in both intentional domain rendering and accidental UI styling. Recording, meters, waveforms, track colors, and plugin response plots are intentional domains; generic panels, labels, borders, and interaction states should use semantic roles.
- A few legacy widgets still query the per-component `NUITheme` object for font sizes, while the rest of the application uses `NUIThemeManager`.
- Built-in preset families beyond Aestra and high-contrast are registered in the manager but are not product-facing choices. Exposing every internal compatibility preset would broaden the visual identity rather than complete the Aestra theme system.

## Intentionally preserved distinctions

- Track colors, waveform colors, meter thresholds, recording/armed state, mute, solo, bypass, warning, and error remain separate domain roles.
- Plugin editors retain their neutral, flat direction and may use domain-specific graph colors where these communicate signal behavior.
- Major/minor timeline grid contrast and the fractal-grid language remain distinct from ordinary dividers.
- Dense DAW layouts keep compact metrics; this pass does not inflate rows, headers, or hit areas beyond the established minimums.
- Existing arbitrary JSON keys remain loadable through `NUITheme` for compatibility, although only documented semantic keys affect the live manager.

## Smallest reliable implementation plan

1. Make `NUIThemeManager` the sole live owner while keeping `NUITheme` as the backward-compatible JSON and per-component adapter.
2. Add deterministic theme registration/loading with a documented mapping from JSON semantic tokens to `NUIThemeProperties`; missing tokens inherit the selected base preset.
3. Replace the single-listener limitation with explicit subscriptions while retaining the old callback API as a compatibility slot.
4. Make theme activation atomic. Until every property has a safe interpolation rule, requested animated switches complete as one atomic change rather than emitting partial themes.
5. Subscribe the application window once, propagate `onThemeChanged()` through the component tree, refresh cached components/icons, invalidate affected surfaces, and preserve idle rendering behavior.
6. Wire startup and Appearance settings to `Preferences::theme`, with safe fallback for stale or unknown values and real Apply/Cancel behavior.
7. Migrate confirmed-live generic styling bypasses, then validate dark, light, and high-contrast themes across the same main-surface runtime loop used for PR #517.

## Performance and safety constraints

- Parsing and registration occur only on user-driven load/reload paths, never during paint, layout, or audio processing.
- Theme activation performs bounded UI-thread notification and one hierarchy invalidation.
- Paint paths continue to read cached structs or manager references; no per-frame JSON parsing is introduced.
- The theme system remains outside real-time audio paths and does not add locks, allocation, logging, or I/O to audio callbacks.

## Muse handoff requirements

The later Muse harness should consume public UI actions and stable observable state rather than component internals. The finalized theme contract should expose deterministic theme selection, active-theme identity, and screenshot-ready invalidation. Muse work remains a separate branch after this theming pass so automation does not become coupled to transitional UI implementation details.
