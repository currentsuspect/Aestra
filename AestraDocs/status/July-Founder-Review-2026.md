# Aestra Founder Review — July 2026

**Review date:** 2026-07-10  
**Repository snapshot:** `feature/hardware-midi-in` at `3b5c714c`  
**Scope:** Product vision, architecture, UI/design language, audio engine, performance, testing, workflow, business direction, and long-term risk.

> **Post-review addendum (2026-07-11):** [PR #454](https://github.com/currentsuspect/Aestra/pull/454) ships the hardware-MIDI **backend infrastructure** (vendored RtMidi, `MidiInputService`, engine hardware queue, headless coverage). The user-facing feature is **not yet end-to-end**: nothing in `Source/` instantiates the service, so Aestra does not listen to hardware controllers yet. Application lifecycle wiring is tracked in [#455](https://github.com/currentsuspect/Aestra/issues/455) (with [#456](https://github.com/currentsuspect/Aestra/issues/456) port hot-plug and [#457](https://github.com/currentsuspect/Aestra/issues/457) physical-controller smoke testing). The "Finish MIDI/computer-keyboard input" item in §12 therefore remains open. The `NUITextInputLayoutTest` failure recorded in the Review Validation Notes is now tracked as [#458](https://github.com/currentsuspect/Aestra/issues/458).

## 1. Executive Judgment

Aestra is coherent at the level of belief and incoherent at the level of scope.

It is not a toy DAW. Toy DAWs do not have golden-audio regressions, realtime/export parity tests, allocation traps, sample-rate truth tests, immutable project fixtures, true-peak measurement, background prefiltering, visible realtime scheduling status, or an explicit policy that session loss is a betrayal. Those are serious foundations.

It is not yet a trustworthy production DAW either. A short one-shot—the basic material of hip-hop production—can currently render silence under the default release behavior ([issue #452](https://github.com/currentsuspect/Aestra/issues/452), [SamplerPlugin.cpp](/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/SamplerPlugin.cpp:359)). Loaded project objects can receive new runtime identities instead of preserving serialized ones ([ProjectSerializer.cpp](/home/currentsuspect/Dev/Aestra/Source/Core/ProjectSerializer.cpp:1423)). Third-party plugin parameters are no-ops in the isolation path ([OutOfProcessPluginInstance.cpp](/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/OutOfProcessPluginInstance.cpp:466)). The UI has only a handful of automated contracts, and one currently fails.

The strongest honest description today is:

> Aestra is a serious pre-beta producer workstation with an unusually rigorous audio-integrity program, a distinctive native UI shell, and an incomplete end-to-end session contract.

It is more interesting than a DAW clone because it is converging on a specific promise: professional dignity on weak hardware. The best product hiding inside it is not “free Ableton with AI” or “FL Studio with better routing.” It is a low-resource, pattern-and-sample-first workstation where making, arranging, listening, exporting, and reopening are one trustworthy loop.

The July beta gameplan is the most mature product document in the repository. Its finish line—play, record, arrange, mix, export, reopen identically on 4GB hardware—is correct, and its refusal to fund the “moat stack” first is exactly right ([beta-gameplan-2026-07.md](/home/currentsuspect/Dev/Aestra/AestraDocs/product/beta-gameplan-2026-07.md:11)).

The danger is that the older vision documents still romanticize metaphors, cards, AI, routing animation, and ecosystem breadth more than the actual producer loop. If that older vision regains control, Aestra can spend two years becoming a sophisticated collection of promising subsystems without becoming a DAW people trust with songs.

## 2. Extracted Vision

### Explicit vision

The repository says Aestra is:

- A free, full-featured, pattern-first DAW for hip-hop and electronic producers.
- Built for price-sensitive creators and low-resource hardware.
- Free of essential feature gates.
- Monetized through first-party plugins, supporter benefits, packs, and eventually Muse.
- Designed around borrowed concepts such as Arsenal, Audition, Takes, routing maps, and C|E|A.

That is stated directly in the product pitch ([Product-Strategy.md](/home/currentsuspect/Dev/Aestra/AestraDocs/product/Product-Strategy.md:9)), free-core contract ([Product-Strategy.md](/home/currentsuspect/Dev/Aestra/AestraDocs/product/Product-Strategy.md:54)), and target-audience definition ([Product-Strategy.md](/home/currentsuspect/Dev/Aestra/AestraDocs/product/Product-Strategy.md:101)).

### Implicit vision

The recent engineering work tells a better story than the old positioning deck.

PRs [#427](https://github.com/currentsuspect/Aestra/pull/427), [#429](https://github.com/currentsuspect/Aestra/pull/429), [#431](https://github.com/currentsuspect/Aestra/pull/431), [#433](https://github.com/currentsuspect/Aestra/pull/433), and [#438–441](https://github.com/currentsuspect/Aestra/pull/441) are building a culture of numerical honesty:

- Does realtime equal export?
- Does the callback allocate?
- Is the session sample rate really what every path uses?
- What is the measured alias rejection?
- Is the limiter or preview policy changing the output?
- Can an old project still be loaded exactly?

Meanwhile, PRs [#399–418](https://github.com/currentsuspect/Aestra/pull/418), [#424](https://github.com/currentsuspect/Aestra/pull/424), and [#426](https://github.com/currentsuspect/Aestra/pull/426) are moving the UI away from neon spectacle toward dense, neutral, producer-focused instrumentation.

The implicit product is therefore not “design metaphors plus AI.” It is:

> A trust-first producer environment whose speed, sound, and project behavior are demonstrable rather than marketed.

That is a much stronger foundation.

### Emotional/product vision

The emotional promise is dignity.

The repository’s best sentence is not about features: it is about the producer with real ideas and no margin for software that fights them ([philosophy.md](/home/currentsuspect/Dev/Aestra/philosophy.md:17)).

The product should make that person feel:

- “This machine is enough.”
- “I can get from a sound to a beat immediately.”
- “The interface respects my attention.”
- “Nothing I make here will disappear.”
- “I am not being punished because I cannot pay.”
- “This feels like a musical place, not a software demo.”

That is what “home for producers” should mean. It should not mean badges, Discord status, or a decorative community layer.

### Technical vision

The technical vision is unusually clear:

- Native C++17 stack.
- Custom OpenGL UI.
- Preallocated realtime processing.
- Lock-free commands and immutable snapshots.
- Offline export using the same main render authority.
- Measured DSP and permanent compatibility fixtures.
- Graceful behavior on weak CPUs and low memory.
- Isolation of third-party plugins when they cannot be trusted.

The philosophy makes realtime safety and performance obligations rather than optimization projects ([philosophy.md](/home/currentsuspect/Dev/Aestra/philosophy.md:69)).

### Business/ecosystem vision

The business thesis is that the DAW remains complete while people pay for additional creative craft:

- High-quality Aestra-native instruments and effects.
- Optional sound/preset content.
- Supporter membership.
- Education and community.
- Eventually local-first Muse and cloud conveniences.

The principle is good. The current financial model is not yet credible. It assumes conversion, retention, plugin cadence, monthly packs, cloud, Muse, and physical-card logistics without accounting for the cost of sustaining them.

### What Aestra should become

If Aestra commits to its best instincts, it should become:

> The fastest trustworthy place to turn samples, notes, and rough patterns into a finished stereo record on an ordinary laptop.

Arsenal, Timeline, and Audition should be three phases of that one loop:

- Arsenal: create and shape musical material.
- Timeline: turn it into a song.
- Audition: listen critically and make release decisions.

Takes should provide fearless experimentation through complete, recoverable snapshots. Routing should make signal ownership visible. Muse should eventually make existing actions easier to find and execute—not introduce another creative universe.

## 3. Strongest Design Choices

### Custom native stack

Why it is strong:

The absence of JUCE gives Aestra control over scheduling, rendering, memory, interaction latency, and visual identity. It avoids inheriting a generic plugin-app look and a large framework’s assumptions. The local application binary is roughly 9MB and the asset tree roughly 38MB in the current build—not an installer measurement, but a healthy early sign.

What it unlocks:

- Precise idle rendering and dirty-state behavior.
- UI designed around Aestra rather than generic widgets.
- Deep audio/UI telemetry.
- Platform-specific low-latency work where it matters.
- A genuinely recognizable interaction language.

What would ruin it:

Treating “custom” as intrinsically virtuous. The stack already carries over 21,000 lines across eight central engine/UI/persistence files. If every text field, modal, plugin editor, accessibility feature, DPI rule, and platform bug becomes founder-maintained infrastructure, custom control turns into permanent opportunity cost.

Protect the custom stack, but demand that each custom subsystem earn its maintenance cost.

### The low-resource target

Why it is strong:

This is the product thesis with the greatest strategic and moral force. It is not merely “runs efficiently.” It defines who Aestra respects. The target appears explicitly in the philosophy and in the roadmap’s i5-3337U/4GB hardening requirement ([Roadmap-Product.md](/home/currentsuspect/Dev/Aestra/AestraDocs/product/Roadmap-Product.md:92)).

What it unlocks:

- A clear performance budget.
- A global audience neglected by incumbent assumptions.
- Better battery life and responsiveness for everyone.
- Strong discipline against feature and framework bloat.
- A credible reason to exist beyond price.

What would ruin it:

Calling the Folio “spiritual” while testing only small sessions at 512 frames. A spiritual target without release gates becomes branding. The machine must become a lab instrument with fixed reference sessions, memory ceilings, load-time limits, and dropout criteria.

### Realtime discipline

Why it is strong:

The actual callback enters a realtime guard, consumes lock-free commands, uses preallocated state, and sanitizes non-finite output ([AudioEngine.cpp](/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:624)). The current out-of-process plugin path also refuses to block the callback: it consumes ready output, publishes pending input, and passes through when the worker is late ([OutOfProcessPluginInstance.cpp](/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/OutOfProcessPluginInstance.cpp:397)).

What it unlocks:

- Professional trust.
- Meaningful low-buffer operation.
- Diagnosable performance.
- A host that can remain stable when plugins are not.
- A defensible engineering culture.

What would ruin it:

Allowing “small” exceptions, or relying on comments rather than traps. There are still two realtime-thread guard concepts, and the Source-layer one is effectively detached from the real callback ([ARCHITECTURE_AUDIT_2026Q2.md](/home/currentsuspect/Dev/Aestra/AestraDocs/architecture/ARCHITECTURE_AUDIT_2026Q2.md:42)). Consolidate them.

### Audio integrity tests

Why they are strong:

This is the best engineering decision in the repository.

The selected golden, purity, sample-rate, allocation, resampling, export-parity, project-fixture, and value-fidelity tests all passed locally in this review. The checked-in v1 fixture is explicitly treated as immutable user-history evidence rather than a generated convenience ([ProjectFixtureCorpusTest.cpp](/home/currentsuspect/Dev/Aestra/Tests/Integration/ProjectFixtureCorpusTest.cpp:3)).

What it unlocks:

- Refactoring without audio mythology.
- Honest quality claims.
- Permanent regression protection.
- Clear review criteria.
- A culture that distinguishes measured differences from placebo.

What would ruin it:

Grading isolated paths “A” while the producer-level path remains broken. A silent hat sample outweighs an excellent mainline alias-rejection number. Tests must increasingly exercise user journeys, not only kernels.

### OpenGL UI and render elision

Why it is strong:

The measured idle-frame work reduced idle CPU from roughly 30% of one core to about 2% on the target-class box ([UI render baseline](/home/currentsuspect/Dev/Aestra/labs/perf/2026-07-04-ui-render-baseline.md:58)). That is not cosmetic optimization; it is the thesis made real.

The timeline FBO cache, browser cache, batched waveforms, minimap work, dirty-state wakeup, and small-text rasterization are structural advantages.

What would ruin it:

Continuing to optimize isolated draw passes without measuring full-session input latency, RSS, active-frame pacing, and audio WCET together. A 2% idle number is good, but producers judge the machine while dragging, scrolling, playing, recording, and opening plugins.

### Free core

Why it is strong:

A complete free core aligns the mission, distribution, and product story. The commitment that free users can make a professional album without watermarks is clear and worth protecting ([Pricing.md](/home/currentsuspect/Dev/Aestra/AestraDocs/product/Pricing.md:9)).

What it unlocks:

- Trust in regions where subscriptions and exchange rates are hostile.
- Education and community adoption.
- A product story stronger than “cheaper DAW.”
- Clear pressure to make paid work additive rather than coercive.

What would ruin it:

Subtle degradation instead of explicit gates: inferior export, missing recovery, crippled routing, reduced track counts, essential plugins removed from Core, cloud lock-in, or paid performance improvements. The optional Campus watermark contradicts the stated policy and should be deleted from the plan ([Pricing.md](/home/currentsuspect/Dev/Aestra/AestraDocs/product/Pricing.md:93)).

### Aestra-native plugins

Why they are strong:

A first-party plugin ecosystem lets Aestra offer dependable CPU use, state compatibility, visual consistency, useful defaults, and deep workflow integration. It also creates monetizable value without disabling the DAW.

What it unlocks:

- A reliable native-only beta.
- Better low-resource guarantees than arbitrary third-party plugins.
- Presets and sound design aligned with the target producer.
- Paid products with understandable ownership and support boundaries.

What would ruin it:

Turning plugins into a quarterly content treadmill before the host and DAW are stable. One excellent sampler, synth, EQ, compressor, reverb, and delay are more valuable than a catalog of uneven “premium” releases.

### Arsenal, Timeline, Audition, Takes, and routing

The strongest concepts are the ones that clarify ownership:

- Arsenal distinguishes active sound-making units from timeline arrangement.
- Routing makes signal flow inspectable.
- Audition creates a deliberate listening context.
- Takes make experimentation reversible.
- Timeline modes can alter detail without moving the user to another product.

Arsenal already has explicit route semantics and tested live/export behavior ([Arsenal-Architecture.md](/home/currentsuspect/Dev/Aestra/AestraDocs/architecture/Arsenal-Architecture.md:19)). Takes are currently whole-project snapshots with a bounded, validated manifest—simple and appropriate ([TakeManager.h](/home/currentsuspect/Dev/Aestra/Source/Core/TakeManager.h:8)).

What would ruin them:

Making the metaphors more important than the actions. `DraftOnly`, `LinkedRack`, `LocalCopy`, `RenderedAudio`, branches, blends, C|E|A, pipes, and Spotify-like Audition are too much vocabulary for a producer who simply wants to know what is sounding, where it goes, and whether a change is reversible.

## 4. Design Choices That May Haunt You

| Risk | Evidence and failure mode | Early decision | Action |
|---|---|---|---|
| The core loop loses to the “moat stack” | The old strategy calls Muse the monetization engine, cards the social moat, and borrowed metaphors the UX moat ([Product-Strategy.md](/home/currentsuspect/Dev/Aestra/AestraDocs/product/Product-Strategy.md:28)). Meanwhile a basic short one-shot can be silent. This is how a visionary product becomes an elaborate demo. | Define Aestra’s moat as low-resource trust plus producer flow. Metaphors are implementation tools, not strategy. | **Fix now.** The July gameplan already makes the right correction. |
| AudioEngine remains a permanent god object | `AudioEngine.cpp` is 3,802 lines; its header is 1,158. Renderer and exporter have friend access “during hybrid engine transition” ([AudioEngine.h](/home/currentsuspect/Dev/Aestra/AestraAudio/include/Core/AudioEngine.h:56)). Every new transport, metering, preview, export, recording, and routing feature is attracted here. | Establish a narrow render context and explicit subsystem owners. Migrate incrementally; do not rewrite. | **Start seam now; migrate later.** |
| Two graph authorities become impossible to reason about | The engine keeps `m_rtGraphState`, `m_rtRenderer`, and two `m_graphStates` ([AudioEngine.h](/home/currentsuspect/Dev/Aestra/AestraAudio/include/Core/AudioEngine.h:842)). PDC writes both double-buffered state and a separate “golden” `m_trackState` because the realtime path reads a different authority ([AudioEngine.cpp](/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:3533)). | Declare one authoritative compiled render snapshot. Any legacy mirror must have a deletion milestone and parity assertion. | **Fix architecture now, migration over several PRs.** |
| Preview, Audition, sampler, timeline, and export become different audio products | Audition exits the main callback early, bypassing the normal master/meter path ([AudioEngine.cpp](/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:743)). The clip prefilter explicitly excludes Preview, Sampler, and Audition ([clip-prefilter-lifecycle.md](/home/currentsuspect/Dev/Aestra/AestraDocs/clip-prefilter-lifecycle.md:127)). Export at a different rate does not rebuild the graph’s prefilter selection ([clip-prefilter-lifecycle.md](/home/currentsuspect/Dev/Aestra/AestraDocs/clip-prefilter-lifecycle.md:94)). | Write one source-rate/render-rate policy and a required parity matrix for every audible path. Differences must be named product behavior, not accidents. | **Fix now.** |
| Plugin hosting is advertised before it is a host | The OOP path preserves RT safety, but parameter access is empty and editors return false. CLAP restart, callback, rescan, clear, and flush requests remain TODOs ([CLAPHost.cpp](/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/CLAPHost.cpp:31)). Synchronous creation can block UI consumers ([PluginManager.cpp](/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/PluginManager.cpp:161)). | Native plugins define beta. Third-party hosting is experimental until parameters, state, latency, callbacks, GUI policy, crash recovery, and a compatibility corpus pass. | **Defer headline support.** |
| Project identity is not stable enough for Takes or future collaboration | Versioning, checksums, fixtures, and value fidelity are now strong. But patterns are recreated with new IDs during load ([ProjectSerializer.cpp](/home/currentsuspect/Dev/Aestra/Source/Core/ProjectSerializer.cpp:1429)). Assets store path and name, not content identity ([ProjectSerializer.cpp](/home/currentsuspect/Dev/Aestra/Source/Core/ProjectSerializer.cpp:697)). History has a 50-file count but no byte budget ([ProjectSerializer.cpp](/home/currentsuspect/Dev/Aestra/Source/Core/ProjectSerializer.cpp:618)). | Preserve stable object IDs, hash assets, define managed-versus-referenced asset policy, and cap recovery storage by bytes. | **Fix now.** |
| The custom UI becomes an untestable monolith | `TrackManagerUI.cpp` is 5,427 lines, `FileBrowser.cpp` 4,497, and `AestraContent.cpp` 3,878. They mix presentation, input, dispatch, filesystem work, and cross-panel coordination. | Extract view models and interaction controllers at proven seams. Require behavior tests before moves. | **Monitor, with targeted extraction now.** |
| Design-system forks become permanent | The older system calls for neon, glass, gradients, glow, “no-line” surfaces, Space Grotesk and Inter ([DESIGN_SYSTEM.md](/home/currentsuspect/Dev/Aestra/AestraDocs/design/DESIGN_SYSTEM.md:1)). Current code uses neutral chrome, Geist/Manrope, visible structural separators, flat states, but still contains legacy cyan/magenta/lime and glass/glow tokens ([NUIThemeSystem.cpp](/home/currentsuspect/Dev/Aestra/AestraUI/Core/NUIThemeSystem.cpp:521)). | Delete one of the two visual directions. Current flat-neutral direction should win. | **Fix grammar now; migrate opportunistically.** |
| “Dense” becomes “tiny” | The new type grammar begins at 9px and uses 10px as a workhorse ([ui-type-space-grammar.md](/home/currentsuspect/Dev/Aestra/AestraDocs/ui-type-space-grammar.md:17)). On an old 1366×768 TN panel, low contrast and 9px labels can become illegibility, not professionalism. | Treat 9px as nonessential annotation only. Test at 100%, 125%, and 150% DPI on the actual Folio-class display. | **Fix now.** |
| Prefilter quality spends the RAM budget | The current design keeps an additional filtered copy per source. Its own estimate gives roughly 184MB extra for one four-minute 96k stereo file and has no eviction policy. | Introduce a global cache budget, LRU/priority eviction, telemetry, and deterministic fallback. | **Fix before long-session beta.** |
| Graph/routing cost scales multiplicatively | Arsenal’s per-track rendering resolves units for each timeline track ([Arsenal-Architecture.md](/home/currentsuspect/Dev/Aestra/AestraDocs/architecture/Arsenal-Architecture.md:61)). At small scale this is fine; at dozens of tracks and units it risks repeated scans, refcount churn, and complex invalidation. | Pre-bucket routed units in the immutable graph snapshot and make graph-build cost visible. | **Measure now; optimize when the scale test proves it.** |
| Paired atomics hide lifetime bugs | The input callback and callback-data pointer are loaded independently with relaxed ordering ([AudioEngine.cpp](/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:628)). The concurrency audit correctly flags that a new callback can observe stale context on weaker memory models ([AUDIT_Threading_Concurrency_2026Q2.md](/home/currentsuspect/Dev/Aestra/AestraDocs/architecture/AUDIT_Threading_Concurrency_2026Q2.md:190)). | Publish correlated callback state as one immutable object or with a defined release/acquire protocol. | **Fix now; small and high leverage.** |
| Platform indecision doubles the release surface | The product roadmap says Linux primary ([Roadmap-Product.md](/home/currentsuspect/Dev/Aestra/AestraDocs/product/Roadmap-Product.md:110)); the technical roadmap says Windows-only for beta ([roadmap.md](/home/currentsuspect/Dev/Aestra/docs/technical/roadmap.md:112)); README advertises both. | Choose one public quality bar and describe the other honestly. | **Founder decision now.** |
| The economic model is aspirational arithmetic | The pricing document says `$129 > $50 × 12 months = $600`, which is mathematically false ([Pricing.md](/home/currentsuspect/Dev/Aestra/AestraDocs/product/Pricing.md:85)). It also assumes monthly packs, quarterly plugins, Muse, cloud, physical fulfillment, and 5–8% conversion. | Rebuild economics from production cost, support load, payment fees, regional pricing, churn, and realistic release cadence. | **Fix business model before public promises.** |
| Cards undermine belonging | Grey, silver, gold, founder scarcity, header badges, Discord roles, levels, and glows turn financial tier into visible social hierarchy, despite claims otherwise ([Pricing.md](/home/currentsuspect/Dev/Aestra/AestraDocs/product/Pricing.md:113)). For a product aimed at economically constrained producers, this is especially dangerous. | Make identity about creative contribution, not payment tier. Keep supporter recognition opt-in and peripheral. | **Kill or radically reduce.** |
| Repository truth continues to drift | The beta plan itself says issue labels are stale ([beta-gameplan-2026-07.md](/home/currentsuspect/Dev/Aestra/AestraDocs/product/beta-gameplan-2026-07.md:24)). Live issues still describe work already merged, while docs describe an old CI profile. | Add a weekly verify/close pass and assign one canonical beta-status document. | **Fix operating discipline now.** |

## 5. Product Identity Review

### Where not to compete

Do not compete with:

- FL Studio on plugin count, piano-roll maturity, lifetime accumulated workflow, or content breadth.
- Ableton on warping, live performance, and device ecosystem.
- Reaper on configurability, scripting, format breadth, and twenty years of stability edge cases.
- Logic on bundled content, orchestration, and macOS integration.
- Bitwig on universal modulation and modular sound design.

Those are decade-scale traps. The technical roadmap already recognizes most of them ([roadmap.md](/home/currentsuspect/Dev/Aestra/docs/technical/roadmap.md:257)).

Also do not compete by saying Aestra has “better UX” in the abstract. That is not credible until ordinary producers finish songs faster in it.

### Where Aestra can be meaningfully different

Aestra can own the intersection of:

- Pattern/sample-first creation.
- Serious low-resource performance.
- Transparent signal and routing ownership.
- A complete free core.
- Measured audio trust.
- A deliberate create → arrange → listen workflow.
- Native instruments built to a known CPU and state contract.

No major incumbent organizes all of those around hardware accessibility as the primary design constraint.

### First producer to serve

The first producer should be narrowly defined:

> A sample-and-pattern-driven hip-hop or electronic producer working on a modest Windows or Linux laptop, primarily using one-shots, loops, MIDI notes, audio clips, and a small set of native instruments/effects to finish stereo releases.

Not yet:

- A film composer.
- A live-performance artist.
- A commercial tracking engineer.
- A modular sound designer.
- A producer dependent on a large third-party plugin collection.
- A collaborative cloud team.

### “Aestra taste”

The taste emerging from the best recent work is:

- Darkroom, not spaceship.
- Dense, not cramped.
- Neutral chrome; color belongs to music and state.
- Flat, precise controls.
- Track identity through restrained color.
- Visual signal flow without node-editor theater.
- Measured sound, not analog mythology.
- Fast direct manipulation.
- Calm confidence.

That taste is real. The older “neon-soaked Synthetic Frontier” language is no longer the strongest direction.

### First ten minutes

Within ten minutes the user must feel:

1. Audio setup is obvious and successful.
2. A hat, kick, or keyboard note makes sound instantly.
3. Dropping or recording material creates a clear musical object.
4. Arsenal-to-Timeline ownership makes sense without reading documentation.
5. The interface remains smooth and quiet on the CPU.
6. Export is one understandable operation.
7. Reopening proves that nothing moved, vanished, or changed tone.

No account prompt. No card. No Muse teaser. No routing animation presented as the reason to stay.

## 6. UI / Visual Design Review

The available checked-in screenshot is from May ([interface snapshot](/home/currentsuspect/Dev/Aestra/AestraDocs/images/aestra_daw_interface.png)) and predates major July work, so it should not be treated as a fully current visual record. The current code and recent PRs show a substantial move toward neutral chrome, flatter controls, filled waveforms, improved track identity, and clearer routing.

### Density

The direction is appropriate for a DAW. Aestra should feel like an instrument with persistent context, not a responsive web dashboard.

The problem is confusing density with minimum size. Nine-pixel type, 20px controls, 46px lanes, a 300px browser, 236px track controls, and a 56px transport can fight each other on 1366×768 ([NUIThemeSystem.h](/home/currentsuspect/Dev/Aestra/AestraUI/Core/NUIThemeSystem.h:205)). The shell must be measured at the actual target resolution, not designed on a larger developer display and scaled down.

### Typography

The current Geist-first rendering direction is better than the stale Space Grotesk/Inter plan ([NUIRendererGL.cpp](/home/currentsuspect/Dev/Aestra/AestraUI/Graphics/OpenGL/NUIRendererGL.cpp:394)). Geist is restrained and technical without becoming sci-fi.

Recommendations:

- Use Geist consistently for UI.
- Use tabular figures or a restrained mono face for time, BPM, sample rate, and meter values.
- Reserve 9px for nonessential ticks.
- Use 10–12px for dense operational labels, with contrast validated on poor panels.
- Avoid uppercase for long control labels; it reduces scan speed at small sizes.
- Keep weight differences subtle but real; do not simulate hierarchy only with opacity.

### Spacing

The typography scale is DAW-specific; the spacing scale is still generic-app spacing: 4, 8, 16, 24, 32, 48.

A dense workstation needs a finer operational scale—roughly 2, 4, 6, 8, 12, 16, 24—so components do not resort to raw literals or become padded like cards. The existing 109 files with direct color construction and relatively sparse theme-scale usage indicate that the token system is not yet the real authority.

### Contrast

The neutral surface ladder is good. It allows tracks, clips, meters, selection, and state to carry the color.

Risks:

- Secondary white at 50% and disabled text at 25% may disappear on old low-contrast displays.
- Adjacent near-black surfaces can merge if the “no-line” doctrine is followed.
- Glow and translucent purple are weaker state indicators than a solid edge, icon change, or tonal fill.
- Pure white is acceptable for small critical text; forbidding it categorically is aesthetic dogma.

### Panels

Use four neutral depth levels and one panel grammar:

- Workspace/canvas.
- Docked panel.
- Raised inspector/editor.
- Temporary overlay/modal.

Panel identity should come from title placement, tonal level, and consistent handles—not unique decoration per panel.

### Track lanes

The recent filled-waveform and per-track color direction is worth doubling down on.

Track color should identify musical ownership, not tint every surface. Use it for:

- A 3–4px identity strip.
- Clip/waveform body.
- Selected routing edge.
- Small channel marker.

Keep grids, labels, controls, and backgrounds neutral. Lane separators should remain visible at every zoom level. “No lines” is the wrong rule for a timeline; precise alignment is part of the instrument.

### Transport

Transport should be the calmest, most invariant part of the application.

- Stable geometry.
- Large enough primary play/record controls.
- No gradients or glow.
- Recording uses red and shape change, not color alone.
- BPM and time use tabular numerals.
- Secondary controls progressively disclose rather than colonize the bar.
- The current 56px default height should be tested against the Folio display; it may be too expensive.

### Browser

The browser is strategically important because sample discovery is the front door for the target producer. Its FBO caching work is real—the pre-cache baseline showed up to 11.46ms spent in browser rendering during active input ([UI render baseline](/home/currentsuspect/Dev/Aestra/labs/perf/2026-07-04-ui-render-baseline.md:46)).

Product rules:

- Search must be immediate.
- Preview state must be unmistakable.
- Dragging must never crackle.
- Selected, previewing, favorite, missing, and loading states must not be encoded only by color.
- Keep metadata secondary; filename, type, and audition are primary.
- Do not add content-store or cloud affordances before the local browser is excellent.

### Mixer

The recent channel identity and inspector work is directionally strong. The mixer should feel like the same product as Timeline—not a separate “console skin.”

Avoid:

- Faux hardware.
- Glossy knobs.
- Heavy shadows.
- Too many color-coded plugin families.
- Narrow faders that become precision-hostile.

Prioritize:

- Obvious gain ownership.
- Clear pre/post and send state.
- Stable meters.
- Routing visibility.
- Readable numerical values.
- Keyboard traversal.
- Common controls in identical positions.

### Plugin/editor language

Aestra-native plugins should share:

- Window shell.
- Header and preset grammar.
- Bypass and reset placement.
- Parameter typography.
- Meter semantics.
- Automation/state indicators.
- Resize behavior.

Inside that shell, the DSP visualization can carry restrained personality. Do not give every plugin an unrelated visual brand.

### Ableton-like restraint

Restraint is helping. Copying Ableton’s flat greyness would hurt.

Aestra needs warmer musical identity through track colors, waveforms, arrangement structure, and focused accent—not through chrome. The UI should be quieter than FL Studio and more emotionally alive than Reaper, without becoming neon Bitwig.

### Recommended design grammar

- **Surfaces:** four neutral luminance tiers; hue-free chrome.
- **Text:** Geist; 10/11/12/14 as the operational core; tabular numerals.
- **Controls:** 24–28px interaction targets even when the visible glyph is 16–20px.
- **Color:** purple for focus/selection/action; track colors for musical identity; green/yellow/red only for status and meters.
- **Separators:** 1px tonal edges where alignment matters; spacing where grouping matters.
- **States:** color plus geometry/icon/weight; never glow alone.
- **Radii:** 3/5/7 for controls; 10–14 only for floating surfaces.
- **Motion:** 80–140ms transitions for state disclosure; no persistent breathing, trailing glow, or decorative movement.
- **Depth:** structural surfaces are flat; only transient floating layers receive shadow.
- **Routing:** reveal detail on selection/hover, but keep current signal ownership legible without animation.

## 7. Audio Engine / DSP Trust Review

### What is genuinely strong

The main full-mix export now calls the live `processBlock`, explicitly disables monitoring-only preview ducking, and preserves master processing ([AudioExporter.cpp](/home/currentsuspect/Dev/Aestra/AestraAudio/src/IO/AudioExporter.cpp:201)). That is the correct parity architecture.

The output path:

- Sanitizes NaN/Inf.
- Supports an optional safety limiter.
- Measures peaks and LUFS.
- Maintains unclamped float output when the limiter is disabled.
- Measures true peak during export.
- Applies deterministic TPDF dither for PCM16/24 export ([AudioExporter.cpp](/home/currentsuspect/Dev/Aestra/AestraAudio/src/IO/AudioExporter.cpp:449)).

The resampling work is evidence-driven and now gives the main timeline path an off-RT Kaiser prefilter with strong measured alias rejection. The targeted trust tests all passed in this review.

### Where integrity can silently diverge

#### Audition

Audition replaces the main callback path and returns early. It gets simple peak metering, but not the normal master path, LUFS pipeline, preview ducking, or other downstream semantics.

That may be a valid listening-product decision. It is not valid as an undocumented implementation accident. The UI must say whether Audition is:

- Raw reference playback.
- Through monitor calibration.
- Through master processing.
- Loudness-normalized.
- Included in project export.

#### Preview

Browser preview correctly stays outside project export and can duck transport monitoring. But its source-rate conversion remains a separate implementation from the timeline clip prefilter.

A producer deciding whether to use a sample should not hear a materially different timbre after dropping it into the timeline.

#### Sampler

The sampler uses Sinc64Turbo and velocity-squared gain ([SamplerPlugin.cpp](/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/SamplerPlugin.cpp:399)). Its short-one-shot release bug is a P0 product failure because hats, clicks, rims, and short kicks are central to the first user.

The velocity curve also needs an explicit musical policy and regression—not an incidental square.

#### Full export versus isolated bounce

Full export uses the complete live path. Isolated-track bounce deliberately uses `AudioRenderer` without the master stage ([AudioExporter.h](/home/currentsuspect/Dev/Aestra/AestraAudio/include/IO/AudioExporter.h:16)).

That is acceptable only if the product calls it “stems/pre-master track bounce.” It is not acceptable if users interpret it as “what I heard when soloing this track.”

#### Export sample rate

The exporter changes engine sample rate, but the current clip graph may retain prefilter selection built for the session rate. Exporting at a different rate is explicitly uncovered. Either rebuild the graph for export or restrict/document beta export to session rate.

#### Device clipping

Limiter-disabled float output remains unclamped, while the physical device boundary policy is unresolved. Decide where conversion/clamping occurs, what the meter reports, and what “clip” means. Issue [#422](https://github.com/currentsuspect/Aestra/issues/422) should be a policy decision, not just a patch.

#### Pan law

The current equal-power center reference has parity across several paths, but the mono-versus-stereo balance policy remains open ([issue #405](https://github.com/currentsuspect/Aestra/issues/405)). Set the policy before projects depend on it.

#### Plugins

The out-of-process path can produce a delayed ready block or pass through when output is unavailable. That protects the callback, but the added latency and temporary bypass must be reported to PDC and surfaced to users. Otherwise plugin audio can be late while appearing healthy.

### What must be true before “Aestra sounds correct”

You can currently say that important mainline paths are measured. You cannot yet say the entire DAW sounds correct.

The honest bar is:

1. Every basic producer source—short one-shot, long sample, loop, MIDI instrument, recorded audio—produces expected non-silent output.
2. Preview, sampler, Audition, timeline, live monitoring, export, and isolated bounce have explicit sample-rate and gain policies.
3. Full-mix realtime and export null within a defined tolerance across buffer sizes and sample rates.
4. Any intentional difference is named in the UI and tested.
5. Pan law, master limiter, device clamp, headroom, dither, and true-peak policies are documented and persisted where necessary.
6. Plugin latency, bypass, crash, state, and parameter behavior are reliable.
7. Meters report the same signal point the user thinks they report.
8. Seeking, looping, stopping, tails, fades, and transport restarts cannot click or drop notes.
9. The reference session passes both numerical tests and a documented manual listening script on the target machine.
10. No callback locks, allocations, logging, waits, file I/O, or correlated-lifetime races remain.

## 8. Architecture Review

### Durable parts

The top-level module shape is correct: Core → platform/audio/UI → application. The architecture audit reached the same conclusion ([ARCHITECTURE_AUDIT_2026Q2.md](/home/currentsuspect/Dev/Aestra/AestraDocs/architecture/ARCHITECTURE_AUDIT_2026Q2.md:11)).

Other durable choices:

- Immutable audio snapshots.
- Lock-free command queues.
- Off-RT graph compilation.
- Preallocated render state.
- Source ownership through shared immutable audio buffers.
- Worker-thread prefiltering with controlled graph publication.
- Explicit project-size bounds.
- Versioned schema and migrations.
- Whole-project Takes rather than premature CRDT/version-control machinery.
- OOP plugin isolation as a long-term host boundary.
- Command-based undo architecture.

The July project work is especially meaningful: current files are schema v2 with v1 support, a migration path, checksum verification, and an immutable v1 fixture corpus ([ProjectSerializer.cpp](/home/currentsuspect/Dev/Aestra/Source/Core/ProjectSerializer.cpp:35), [ProjectMigrations.h](/home/currentsuspect/Dev/Aestra/Source/Core/ProjectMigrations.h:39)).

### Fragile parts

#### Audio/control ownership

`processBlock` currently owns input callbacks, transport transitions, command consumption, Audition switching, timeline rendering, Arsenal, preview ducking, master gain, limiting, metering, true peak, LUFS, and waveform publication. That is too much policy in the one function that must remain easiest to reason about.

The solution is not a rewrite. Establish explicit owners with a small render context and move one proven responsibility at a time.

#### Hybrid graph transition

This is the most dangerous architecture seam. There are multiple state containers and mirrored PDC values. Every future feature—automation, sidechain, nested buses, freeze, stems—will multiply the chance that one path reads the wrong state.

Make “one authoritative compiled graph” an architectural invariant.

#### UI/application monoliths

`AestraContent`, `TrackManagerUI`, and `FileBrowser` are approaching the scale at which small changes become nonlocal. They will collapse under:

- Multiple selection modes.
- Rich drag/drop.
- Automation.
- Comping.
- Plugin editors.
- Accessibility.
- Context-sensitive shortcuts.
- Project load/rebind behavior.

Carve interaction controllers and view models around current behavior. Avoid abstract widget frameworks.

#### Serializer ownership

`ProjectSerializer.cpp` is compiled separately into the app, headless targets, and numerous tests. It belongs in an audio/project-domain module, not the UI-facing Source tier. This is important but not a reason to delay beta trust work.

#### UI module boundary

AestraUI’s CMake target includes Source and AestraAudio application paths privately ([AestraUI/CMakeLists.txt](/home/currentsuspect/Dev/Aestra/AestraUI/CMakeLists.txt:191)). Even where unused, this weakens the boundary. UI-core components should depend on small data/view interfaces rather than broad application include visibility.

#### Takes

Current Takes are snapshots with parent IDs, not a merge system. That is good. Do not build branch merging or “blend conflict resolution.” Reliable create, name, switch, recover, and delete are sufficient.

#### Cache invalidation

The clip-prefilter worker and graph-generation approach are sensible, but caching policy is fragmented:

- Timeline render cache.
- Browser FBO cache.
- Waveform cache.
- Minimap state.
- Prefilter cache.
- Plugin snapshots.
- Project/runtime snapshots.

Before adding more caches, establish a common telemetry vocabulary: bytes, generation, hit/miss, stale reason, rebuild time, and owner.

## 9. Performance Review

### Real advantages

#### Idle frame elision

Going from roughly 30% of a core to 2% idle is a thesis-level win. It directly improves weak laptops, battery life, fan noise, and audio scheduling headroom.

#### Cached timeline/browser work

The timeline FBO and browser-cache work address measured hotspots rather than speculative micro-optimization.

#### Realtime scheduling visibility

On the target box, active realtime scheduling reduced UI-stress callback WCET to 2.55ms against a 10.67ms budget, with zero observed xruns ([rt-audio-scheduling-spec.md](/home/currentsuspect/Dev/Aestra/labs/perf/rt-audio-scheduling-spec.md:43)). More importantly, degraded scheduling is visible rather than silently presented as success.

#### Worker-thread sample preparation

Moving prefiltering and decode preparation away from the callback is correct. The real-time path gets stable buffers and bounded work.

#### Small current footprint

The current local build produced approximately:

- 9.1MB `Aestra` executable.
- 14MB total `build-linux/bin`.
- 38MB asset tree.

That is encouraging, but not yet an install-footprint claim. Package dependencies, debug symbols, SDKs, caches, templates, and first-party content must be measured separately.

### Optimizations that are still local

The strongest published UI/audio baseline uses:

- 11 tracks.
- 512 frames.
- One current machine.
- A particular view state.
- No recorded RSS.
- No startup measurement.
- No 32/64-track session.
- No plugin-heavy workload.
- No long-session memory-growth result.

That proves specific optimizations, not product scalability.

### Largest performance risks

1. **Prefilter memory:** potentially hundreds of megabytes per long high-rate source, no eviction.
2. **Graph scaling:** repeated track/unit/routing work as sessions grow.
3. **Plugin isolation transport:** string/hex IPC and worker polling are safe for RT but inefficient and latency-heavy.
4. **UI monolith invalidation:** one interaction can dirty far more surface than intended.
5. **Waveform/cache multiplicity:** every representation can retain another copy.
6. **Project load:** decoding and rebuilding a real session may dominate startup despite a small executable.
7. **Low-buffer operation:** 512-frame success is not enough for live playing. The target needs explicit 128/256-frame expectations.
8. **Build/developer accessibility:** the codebase is already large enough that 4GB build constraints require `-j2` discipline and low-memory CI awareness.

### Required reference-hardware matrix

At minimum:

- 16, 32, and 64 tracks.
- 128, 256, and 512 frames at 44.1/48kHz.
- Samples at same, higher, and lower source rates.
- Native instrument/effect mix.
- Recording plus playback.
- Active scrolling/dragging/metering.
- Save, close, reopen.
- Five-, thirty-, and 120-minute runs.

Record:

- Callback mean, p99.9, and WCET.
- Xruns/underruns.
- UI frame median/p99 and input-to-paint latency.
- RSS at load and over time.
- Cache bytes.
- Project-open time.
- Cold startup time.
- Export speed and peak memory.
- Audio scheduling state.

If these are not release gates, the low-end promise is marketing.

## 10. Testing / CI / Quality Discipline

### Overall judgment

The testing culture is one of Aestra’s strongest assets, but it is lopsided.

After the current CMake regeneration, the build registered 144 tests:

- 61 selected by the audio label.
- Six serialization-labeled tests.
- No default performance or soak tests.
- Four genuinely UI-specific labeled tests, plus an unrelated label match in `ctest -L ui`.

The selected eight audio/serialization trust tests passed. The UI-labeled run selected five tests and failed `NUITextInputLayoutTest`: nine assertions passed, one caret-Y layout assertion failed.

That is useful evidence. It shows audio and persistence are acquiring durable contracts while UI correctness remains thin.

### Highest-value current tests

Protect these as permanent gates:

- `GoldenAudioRegressionTest`
- `RealtimeExportParityTest`
- `RTAllocationTrapTest`
- `AudioPurityAuditTest`
- `SampleRateBufferTruthTest`
- `SessionResamplingTruthTest`
- `ProjectFixtureCorpusTest`
- `ProjectValueFidelityTest`
- Project recovery/roundtrip tests
- Arsenal export/routing parity tests
- PDC and graph invalidation tests
- Realtime path stress tests

### Tests that are too narrow

- Quantization helpers without full file-format/export combinations.
- UI tests limited to theme JSON, text layout, piano-roll interaction, and renderer spacing.
- Recording state tests without the actual device/app workflow.
- Plugin-host tests without a real compatibility matrix.
- Mainline resampling tests that do not include preview, sampler, and Audition.
- Project roundtrips that tolerate identity reminting.
- Small-project performance tests without session-scale resource budgets.

### Missing tests that should scare you

1. A 100ms one-shot at default sampler settings produces audible output.
2. Save → load preserves every persistent object ID.
3. Save → load → save is semantically and canonically stable.
4. Asset content changes are detected even when the path is unchanged.
5. Missing assets can be relinked without losing object identity.
6. Preview/sampler/Audition/timeline resampling quality and gain parity.
7. Export at a rate different from the project rate.
8. Isolated-bounce contract versus soloed live playback.
9. Input callback/context lifetime under TSan and pointer replacement.
10. 64-track target-machine resource regression.
11. Repeated project open/close and plugin create/destroy memory growth.
12. UI layout at 1366×768 and multiple DPIs.
13. Full producer-loop automation through save/reopen.
14. Plugin crash during save, render, and project load.

### CI review

The main CI runs headless build/tests on Linux, Windows, and macOS and now has a separate Linux application compile lane ([ci.yml](/home/currentsuspect/Dev/Aestra/.github/workflows/ci.yml:18), [ci.yml](/home/currentsuspect/Dev/Aestra/.github/workflows/ci.yml:73)). That is strong.

Problems:

- Formatting is advisory.
- clang-tidy is advisory.
- ASan/UBSan, TSan, and LSan are advisory.
- Nightly is sanitizer-oriented but not performance-oriented ([nightly.yml](/home/currentsuspect/Dev/Aestra/.github/workflows/nightly.yml:12)).
- The testing documentation still claims a narrow six-test default CI gate, contradicting the actual workflow ([testing_ci.md](/home/currentsuspect/Dev/Aestra/docs/technical/testing_ci.md:23)).
- UI behavior is not a CI contract beyond a few isolated tests.

### What to measure every PR

- Required platform builds.
- Full deterministic headless suite.
- Audio allocation/lock/log trap.
- Golden and realtime/export parity.
- Project fixtures/value fidelity/stable IDs.
- Quick callback budget on a fixed reference scene.
- Core UI interaction/layout tests.
- Screenshot evidence for visual changes.
- Binary and asset size deltas when dependencies/assets change.

### What to measure nightly

- ASan/UBSan and TSan.
- Thirty-minute and two-hour audio soak.
- 16/32/64-track performance matrix.
- RSS growth.
- Cold startup and project-open time.
- Project corpus across all supported schema versions.
- Export at multiple sample rates/bit depths.
- Plugin lifecycle and compatibility corpus.
- Repeated save/load/recovery cycles.

### What must be manually listened to

- Short hats, rims, kicks, and long one-shots.
- Sample drag/preview/drop transition.
- Monitoring versus recorded playback.
- Loop boundaries and seeks.
- Stop/restart tails.
- Pan-center behavior for mono and stereo.
- Bypass parity.
- Master limiter on/off.
- Realtime versus export null plus human check.
- Sample-rate mismatch sources.
- Project reopen after crash/recovery.

### “Done” definitions

- **Audio change:** numerical invariant, RT-safety check, sample-rate/buffer matrix, and listening script.
- **UI change:** interaction test, target-resolution check, performance comparison, keyboard/focus behavior, and screenshot evidence.
- **Project change:** old fixture load, current roundtrip, stable IDs, corrupt/missing-field behavior, recovery path, and migration.

## 11. Business / Ecosystem Risk

### What can be monetized without poisoning trust

- High-quality Aestra-native instruments and effects.
- Optional curated presets and sample packs.
- One-time plugin purchases.
- A supporter bundle that includes those products.
- Education, workshops, templates, and professional support.
- Marketplace revenue share later.
- Optional cloud backup/collaboration after the local system is complete.
- Muse after it is useful and affordable to maintain.

### What should never be gated

- Recording.
- Editing.
- Routing.
- Track count.
- Project save/load.
- Autosave and recovery.
- Export quality and bit depth.
- Plugin hosting.
- Core accessibility.
- Performance modes.
- Offline use.
- Essential EQ, dynamics, time effects, metering, and sampling.
- Project compatibility or asset recovery.

### Are premium plugins enough?

Premium plugins can support early revenue, but not under the current assumed cadence.

“Quarterly plugins plus monthly packs” is a content company. It requires sound design, QA, documentation, video, support, compatibility, and continual marketing. A small team will either neglect the DAW or ship shallow products.

A more credible early model:

- A complete free native set.
- One genuinely excellent paid instrument or effect family at a time.
- One-time ownership, included in Supporter.
- Regional pricing.
- Supporter framed as sustaining development, not buying status.
- Packs released when excellent, not monthly by obligation.

Premium plugins may be enough for the first stage. They are not automatically a durable company by themselves.

### Muse

The Muse spec contains good constraints: local inference, no automatic application, undoable actions, dismissible suggestions, and no cloud dependency ([Muse-AI-Spec.md](/home/currentsuspect/Dev/Aestra/AestraDocs/product/Muse-AI-Spec.md:133)).

Muse should be:

- A command and control layer.
- Project-aware search.
- Explanations of routing, clipping, latency, and missing assets.
- Reversible suggestions.
- Pattern continuation only when explicitly invoked or strongly contextual.
- Offline-first.
- Small enough for the target machine.
- Able to stay silent.

Muse should not be:

- The reason the subscription exists.
- A continuous observer consuming 200MB on a 4GB laptop.
- A genre-policing “best practices” engine.
- A cloud requirement.
- A generator in euphemistic clothing.
- Trained on user work by default.
- A modal chatbot that competes with the DAW.

The current spec calls Muse the primary subscription justification ([Muse-AI-Spec.md](/home/currentsuspect/Dev/Aestra/AestraDocs/product/Muse-AI-Spec.md:190)). The beta gameplan correctly reverses that. Muse must be upside.

### Community as a technical feature

Useful community infrastructure would include:

- Public low-end hardware benchmark submissions.
- Anonymized crash signatures.
- Plugin compatibility reports with exact versions.
- Reproducible project fixtures.
- Shared routing/preset templates.
- Translation and accessibility testing.
- Localization.
- Sample/project relink recipes.
- Producer workflow studies.
- Publicly visible performance and reliability goals.

That creates collective product knowledge. Discord roles and collectible cards do not.

### Avoiding overpromise

Do not publicly headline:

- Muse.
- Cards.
- Cloud Takes.
- Third-party plugin compatibility.
- Mac/mobile.
- “All-A audio.”
- Album-level Audition simulations.
- Collaboration.
- A December date detached from evidence.

Headline one claim only after it is proven:

> Make and finish a serious beat on the laptop you already own.

## 12. The Next 90 Days

### Must-fix now

#### 1. Establish the reference producer loop

Build one canonical project and one documented flow:

> Start Aestra → load/play short one-shots → play MIDI/computer keyboard → record → arrange → route → mix with native plugins → export → close → reopen → compare state and sound.

Automate as much of it as possible. Run the actual UI/device path manually every release candidate.

First fix: [issue #452](https://github.com/currentsuspect/Aestra/issues/452). A hip-hop DAW cannot ship with short one-shots silently disappearing.

#### 2. Finish the project-trust contract

- Preserve IDs during load.
- Add canonical save/load/save stability.
- Define asset ownership.
- Store asset content hashes.
- Add relink and mismatch reporting.
- Add a history byte quota.
- Exercise Takes against missing/corrupt snapshots.
- Keep the immutable v1 fixture forever.

#### 3. Write and enforce the audio policy

Decide:

- Mono versus stereo pan law.
- Limiter default.
- Float and device-boundary clipping.
- Dither defaults.
- True-peak warning/enforcement.
- Audition signal point.
- Isolated-bounce meaning.
- Export-rate graph rebuild.
- Preview/sampler/Audition SRC policy.

Then build tests around the policy.

#### 4. Make the Folio a release gate

Create fixed 16/32/64-track sessions and record CPU, WCET, xruns, UI latency, RSS, startup, project load, and export.

Implement a bounded prefilter/cache memory policy before testing large sessions.

#### 5. Freeze plugin scope

Beta should be native-plugin-first.

Do not advertise third-party plugin support until:

- OOP parameters work.
- Host callbacks work.
- Latency is reported and compensated.
- UI creation is nonblocking.
- State survives reopen.
- Crash/bypass behavior is visible.
- A small real plugin matrix passes.

### Important, but not now

- Incrementally consolidate graph authority.
- Move serialization into a project/audio-domain module.
- Extract controllers/view models from the largest UI files.
- PDC improvements beyond the common flat case.
- Additional first-party plugin depth.
- Packaging, diagnostics, and crash reporting after the loop is stable.
- More comprehensive UI accessibility/focus work.
- Audition seeking and richer reference organization.

### Consciously ignore for now

- Muse implementation.
- Cloud.
- Collaboration.
- Mobile.
- macOS unless one contributor owns it end-to-end.
- Theme picker.
- Surround/video/notation.
- Deep time stretching.
- Large third-party plugin breadth.
- Further SRC number-chasing beyond the current measured quality.
- Advanced Takes merging.

### Kill or defer because it distracts

- Payment-tier cards in the application header.
- Founder scarcity mechanics as a product pillar.
- Card levels, glows, and engagement progression.
- Optional watermarked Campus exports.
- Spotify/AirPods “artifact simulation” claims.
- Git vocabulary exposed to ordinary producers.
- C|E|A as unexplained primary terminology.
- Animated routing as the launch message.
- “Muse is the subscription” economics.
- Fixed monthly pack and quarterly plugin promises.

### Suggested 90-day sequence

#### Days 1–30: Trust loop

- Fix short one-shots.
- Finish MIDI/computer-keyboard input.
- Prove recording through the actual application.
- Preserve project IDs and assets.
- Lock audio policies.
- Add end-to-end reference session.

#### Days 31–60: Hardware and grammar

- Target-machine performance matrix.
- Cache memory budget.
- Target-resolution/DPI UI audit.
- Freeze the flat-neutral design grammar.
- Fix current UI regression and add core interaction tests.
- Resolve public beta platform.

#### Days 61–90: Producer proof

- Five to ten real producers.
- Twenty real songs/projects.
- Reopen projects repeatedly over several weeks.
- Collect crashes, missing assets, unexpected sound differences, and friction.
- No new subsystems.
- Publish beta only when the loop is boringly reliable.

## 13. Founder Decision Log

| Decision | Tradeoff | Recommendation | Evidence that would change my mind |
|---|---|---|---|
| What is beta’s only headline? | Vision breadth versus credibility. | “Finish a serious beat on ordinary hardware.” | A second workflow demonstrates equal reliability with real users and no schedule cost. |
| Windows or Linux primary? | Windows has the producer/device ecosystem; Linux matches current engineering strength and underserved users. Supporting both equally doubles QA. | Public beta: Windows primary, Linux preview/dev-supported. | A target cohort that is materially Linux-heavy and a representative Linux device/packaging matrix passing end-to-end. |
| Third-party plugins in beta? | Ecosystem familiarity versus host instability and scope. | Post-beta headline; native plugins only for the official workflow. | Parameters, state, latency, callbacks, async UI, crash recovery, and 10 representative plugins all pass. |
| What does free mean? | Revenue flexibility versus lifelong trust. | Never gate the complete stereo music-production loop. | Nothing should change this recommendation. |
| How are project assets owned? | Portable projects cost disk; external references save space but break portability. | Managed project media by default, optional reference-in-place advanced mode, hashes in both. | Target users consistently work from immutable centralized sample libraries and prefer reference mode. |
| What are Takes? | Simple snapshots are reliable; branching/merging is complex. | Named whole-project snapshots with parent history. No merge/blend semantics. | Producers demonstrate repeated need for non-destructive branch recombination that snapshots cannot satisfy. |
| What is Audition? | Rich translation tools sound distinctive but can mislead. | A deliberate A/B listening and release-check space with explicit signal policy. | Controlled tests prove device/platform simulation is accurate and users understand its limits. |
| What is Muse? | Helpful control layer versus resource cost, distraction, and creative mistrust. | Local-first, reversible assistance and command/search layer; post-core. | It passes target-hardware budgets and real users repeatedly accept its suggestions without prompting. |
| What is the visual identity? | Neon spectacle is distinctive; neutral precision ages better and performs better. | Flat-neutral chrome, musical color in content, one interaction accent. | Target producers consistently perceive it as sterile and demonstrate better comprehension with a measured alternate treatment. |
| How should Aestra monetize? | Subscription stability versus production/support treadmill. | Supporter plus one-time first-party plugins; no tier-status hierarchy. | Real cost and conversion data supports another model without violating Core. |
| When to refactor AudioEngine? | Immediate cleanup delays beta; indefinite transition compounds risk. | Add one authoritative render-context seam now, migrate incrementally after trust blockers. | A feature requires touching multiple mirrored graph states and cannot be safely implemented; then pull the consolidation forward. |
| Is December 2026 the release gate? | Calendar focus versus false confidence. | No. The reference loop and target-hardware evidence are the gate. | Nothing should change this recommendation. |

## 14. Final Verdict

Aestra’s strongest bet is professional dignity on weak hardware: a pattern-and-sample-first DAW that is fast, trustworthy, free at the core, and serious about proving its sound.

The biggest thing that could kill it is not Ableton, FL Studio, or lack of features. It is allowing custom-framework ambition, metaphor systems, plugins, cards, Muse, and routing spectacle to grow faster than the one complete producer loop.

Protect at all costs:

- The 4GB target.
- The complete free core.
- Realtime discipline.
- Project compatibility and recovery.
- Numerical audio honesty.
- The emerging calm, dense visual taste.

Stop romanticizing:

- Borrowed metaphors as a moat.
- Cards as community.
- Muse as the business model.
- Third-party hosting before it is dependable.
- Custom code merely because it is custom.
- Audio grades that do not cover the real user path.
- A release date unsupported by projects made and reopened.

Build next:

> Fix the short-one-shot path, then create the automated and manual reference producer session that proves input, recording, arrangement, routing, export, persistence, and target-machine performance as one contract.

If Aestra can make that loop boringly reliable on the Folio-class machine, it becomes a real DAW with a defensible reason to exist. If it cannot, nothing in the moat stack matters.

## Review Validation Notes

### Git state reviewed

- Branch/SHA: `feature/hardware-midi-in` / `3b5c714c`
- Review was read-only; no production code was changed.

### Validation commands

- Repository/document/code inspection with `rg`, `git`, `wc`, `du`, and numbered source reads.
- Live GitHub issue/PR inspection with `gh`.
- `cmake --build build-linux -j2 --target GoldenAudioRegressionTest RealtimeExportParityTest RTAllocationTrapTest SampleRateBufferTruthTest AudioPurityAuditTest SessionResamplingTruthTest`
- `cmake --build build-linux -j2 --target ProjectFixtureCorpusTest ProjectValueFidelityTest`
- `ctest --test-dir build-linux -j2 --output-on-failure -R '^(GoldenAudioRegressionTest|RealtimeExportParityTest|RTAllocationTrapTest|SampleRateBufferTruthTest|AudioPurityAuditTest|SessionResamplingTruthTest|ProjectFixtureCorpusTest|ProjectValueFidelityTest)$'`
- `ctest --test-dir build-linux -j2 --output-on-failure -L ui`

### Results

- Eight of eight targeted audio/serialization tests passed.
- Four of five tests selected by the UI label query passed.
- `NUITextInputLayoutTest` failed its caret character-zero Y-position assertion (tracked post-review as [#458](https://github.com/currentsuspect/Aestra/issues/458); confirmed pre-existing on a clean tree).

### Checks not performed

- Full 144-test suite.
- Runtime/device tests.
- Full desktop app build.
- Controlled listening session.
- Fresh target-hardware benchmark run.

Those omissions are why this report distinguishes verified local contracts from product-level readiness.
