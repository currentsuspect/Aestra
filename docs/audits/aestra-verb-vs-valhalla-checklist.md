# AestraVerb → "Valhalla-quality" Checklist

Status: SCOPING DOCUMENT (no code changes)
Reviewed from: `develop` @ `3dda17a1` (post-freeze at `26c7f680`)
Upstream constraint: [`aestra-verb-beta-freeze-note.md`](aestra-verb-beta-freeze-note.md) — DSP topology, mode system, and public parameter identity are locked.

This document inventories AestraVerb against the publicly documented feature surface of Valhalla's reverb family (VintageVerb, Plate, Room, Shimmer) and routes every gap through the freeze policy. It is intentionally not a plan to do everything; it is a checklist of what we have, what we partly have, and what we don't — so priorities can be set deliberately.

Sources are listed at the end. The Valhalla "feature surface" used here is what's publicly described on valhalladsp.com and in the Sean Costello blog — not any internal implementation.

Legend:

- ✅ Have — present and matches or exceeds the described Valhalla behavior.
- 🟡 Partial — present in some form, but does not match the Valhalla behavior or coverage.
- ❌ Missing — not present in AestraVerb.
- ⛔ Locked — change would reopen the beta freeze (see freeze note).

---

## 1. DSP / Algorithm

AestraVerb is a modulated 8-line FDN with pre-diffusion, an early-reflection tap network, plate post-allpass, modulated delay reads, and per-mode wet voicing / box cut / mud HP. (See `AestraAudio/include/Plugin/AestraVerb.h:79-541` and the freeze note for the locked topology.)

| Capability | Valhalla description | AestraVerb | Status | Freeze impact |
|---|---|---|---|---|
| Reverb algorithms (modes) | VintageVerb: 22 (Concert Hall, Bright Hall, Plate, Room, Chamber, Random Space, Chorus Space, Ambience, Sanctuary, Dirty Hall, Dirty Plate, Smooth Plate, Smooth Room, Smooth Random, Nonlin, Chaotic Hall, Chaotic Chamber, Chaotic Neutral, Cathedral, Palace, Chamber1979, Hall1984). Room: 12. Plate: 12. | 9 (Room, Hall, Plate, Cathedral, Chamber, Bright Hall, Ambience, Scoring, Smooth Plate) — `AestraVerb.h:106-116` | 🟡 | 9 modes with per-mode constants bridges the single biggest feature gap. Valhalla still has 22–45 per-product. |
| Internal algorithm variety within a family | Plate emulates different "metals" (Chrome, Steel, Cobalt, Brass, Aluminum, Copper, Unobtanium, Full Plate, Quiet Plate, Dusty Plate, Bright Plate, Tube Plate). VVV adds Dirty/Smooth/Chaotic families on top. | Per-mode constants for FDN base lengths, damping, mod depth, diffusion, decay — `AestraVerb.h:780-792`. Tonal character shaped by per-mode box cut + plate peak biquads — `AestraVerb.h:962-964, 959`. | 🟡 Tonal variation per mode exists; explicit "metal flavors" or Dirty/Smooth sub-flavors do not. | ⛔ Adding sub-modes = mode system change. |
| Random / chorus modulation choice | VVV "Random Space" uses internal delay randomization (no pitch wobble); "Chorus Space" uses chorused modulation. Both on tap. | `kModCharacter` (Random / Chorus / Chaotic) selects the modulation character — `AestraVerb.h:132-136`. Random uses LFO + noise with smooth interpolation; Chorus uses sinusoidal pitched modulation; Chaotic uses wow-and-flutter. | ✅ | Implemented. |
| Echo density control (sparse ↔ dense) | VVV "the echo density that can be adjusted from very sparse to very dense" via Diffusion in many modes. Plate: Density. | `kDiffusion` (0–1) drives pre-diffuser feedback `g` up to ~0.78 — `AestraVerb.h:850-851`. | ✅ | New params ⛔. |
| Pre-delay | VVV, Room, Plate all have PreDelay. | `kPredelayMs` (0–500 ms) — `AestraVerb.h:109, 562, 846-848`. | ✅ | |
| Predelay tempo sync | Plate/Supermassive offer tempo-synced predelay (whole/half/quarter/…). Often requested on forums. | `kPredelaySync` (Off / 1/16 / 1/8 / 1/4 / 1/2 / 1 bar / 2 bars) — `AestraVerb.h:120-128`. | ✅ | Implemented. |
| Predelay (analog character: pre-echo / slap) | Plate review notes an analog "PEW" dispersion artifact on pre-echo in real plates. VVV does not promise a slap-back; Plate "Brass" is described as having a "sharper attack that sounds like there's a touch of dry in there". | Pre-delay is a clean delay; no modeled dispersion or slap-back character. | ❌ | ⛔ New DSP. |
| Decay time | VVV: 0.2 s to 70 s. Plate: ~0.1 s to ~20 s+. | `kDecay` → 0.3 s to 10 s — `AestraVerb.h:578, 831`. | 🟡 Max 10 s vs Valhalla's 70 s. | Decay range change touches display + save-state mapping → ⛔ as a public param. |
| Decay EQ (Damping) | VVV, Plate, Room all have a Damping knob. | `kDamping` — `AestraVerb.h:561, 832-834`. | ✅ | |
| Independent High Cut / Low Cut (post-reverb EQ) | VVV has HiCut + LowCut. Plate has LowFreq/HighFreq. Room v2.0.5 added Space and Lo Cut controls. | `kLowCut` + `kHighCut` user-facing one-pole filters — `AestraVerb.h:1141-1146`. Per-mode "box cut" biquad (`m_boxCutCoeff`) — `AestraVerb.h:962-964, 1153-1162`. Per-mode mud HP — `AestraVerb.h:447-459, 1289-1291`. Plate-only metal-ring cut biquad — `AestraVerb.h:466-477, 1274-1281`. | ✅ | Implemented. |
| Damping ↔ Decay coupling | VVV Damping only rolls off highs while keeping decay; it does not shorten RT60. | Damping controls the in-loop LP coefficient; the FDN feedback gain still uses decay time directly — `AestraVerb.h:833, 872`. Mostly correct. | ✅ | |
| Size (delay scaling) | VVV Size scales the entire FDN up/down. Room Size controls early/late density. Plate Size controls modal density. | `kSize` (0.1× to 2.0×) scales FDN base + diffuser lengths and early tap timings — `AestraVerb.h:830, 866-905`. | ✅ | |
| Attack / Shape / Early-Late balance | VVV Attack interpolates between truncated / gated / reverse envelopes, and balances early vs late in Ambience mode. Room has Space (v2.0.5) and pre-delay space control. | `kAttack` controls early-reflection blend (soft→hard); `kShape` controls attack shape (soft→sharp) — `AestraVerb.h:1152-1155`. Early reflections still added at 0.22 input / 0.12 output — `AestraVerb.h:340-341, 432-433`. | ✅ | Implemented. |
| Stereo width | VVV Width (0–200%). Room Width. Plate Width. | `kWidth` (0–1) drives a M/S rotation via `widthMain/widthCross` — `AestraVerb.h:494-497, 860-862`. 0 = mono fold-down, 1 = full Valhalla-style M/S. | ✅ | |
| Pre-stereo decorrelation | Valhalla relies on per-line modulation and tap decorrelation; Plate (1.5.0) explicitly preserves input panning with parallel mono plates. | Anti-correlated diff boost on Room (k=0.22) and Plate (k=0.26) — `AestraVerb.h:485-492`. Mode-coupled. | 🟡 Hard-coded per mode, not user-adjustable. | Making it a param = ⛔. |
| Modulation (Rate / Depth) | VVV, Plate, Room: Mod Rate + Mod Depth. Plate Mod Depth 0 % is the "most realistic". | `kModRate`, `kModDepth` — `AestraVerb.h:568-569, 853-857`. | ✅ | |
| Modulation "off / clean" mode (Depth=0) | Plate: Depth 0 yields the most realistic plate (no chorusing). | `control.modulationEnabled = depth > 0.0001 && rate > 0.0001` — `AestraVerb.h:858`. At zero, the LFO update block is skipped. | ✅ | |
| Plate dispersion / metallic artifact (modeled) | Plate modes have an inherent metallic sheen; "Dirty Plate" amplifies it. | Plate mode has a fixed peaking cut at ~620 Hz to tame ringing, not add character — `AestraVerb.h:466-477, 959`. | ❌ As a feature; the cut is present, the character is not. | ⛔. |
| Bit-reduced / fixed-point / converter colorization | VVV "1970s" / "1980s" color modes downsample internally, add fixed-point quantization, floating-point gain stepping. Plate does not, Room does not. | Not present. | ❌ | ⛔ DSP. |
| Tape / chaos modulation | VVV Chaotic modes use wow-and-flutter chorusing and pre-emphasis + nonlinearity + de-emphasis saturation. | None. | ❌ | ⛔ DSP. |
| Shimmer (pitch-shifted feedback) | Shimmer is its own plugin. VVV does not have it. VVV also does not have gated/reverse — that's `Nonlin` mode. | Not present. | ❌ | ⛔. Could be a separate sibling plugin later. |
| Nonlin (gated / reverse) | VVV "Nonlin" mode. | Not present. | ❌ | ⛔ DSP. |
| Freeze (infinite sustain) | Plate, Room, Shimmer, Supermassive have a Freeze button. | `kFreeze` param with input attenuation + infinite sustain — `AestraVerb.h:382`. UI freeze pill — `AestraVerbEditor::drawFreezePill`. | ✅ | Implemented. |
| T60 / decay monotonicity | n/a | Lab asserts all three modes are monotonic, zero rebound — `labs/reverb/quality/reverb_quality_baseline.md:9-11`. | ✅ | |
| Modulated FDN topology | Common modern approach. | 8-line FDN with Householder matrix, 2 LFOs + random — `AestraVerb.h:104, 398-400, 419-428`. | ✅ Topology is competitive. | ⛔. |
| Safety / NaN / Inf sanitization | n/a | `sanitize()` runs at every wet/output stage — `AestraVerb.h:1180-1186, 499-500, 509-510, 1169-1170, 476-477`. | ✅ | Freeze protects this. |
| SIMD acceleration (SSE / AVX2 / NEON) | n/a | `AestraAudio/src/DSP/ReverbSIMD.cpp` + `ReverbSIMD.h`. AVX2 + SSE paths in `AestraVerb.h:282-298, 324-336, 346-381, 419-428`. | ✅ | Freeze protects this. |
| Real-time safety (no alloc / no lock in process) | n/a | Post-freeze commit `3119452c` removed the AestraVerb mutex from the realtime path. `process()` allocates nothing. Smoothing uses `std::atomic<float>` + scalar ring buffers. | ✅ | |
| Cubic Hermite fractional delay read | n/a | `readDelayLine` uses cubic Hermite (linear fallback flag) — `AestraVerb.h:1032-1061, 1054`. | ✅ | |

---

## 2. Parameters / Controls (public-facing)

AestraVerb exposes 18 parameters — `AestraVerb.h:84-103`. Save state is versioned (`kStateMagic = 'RVB' v5`).

| Public param | ID | Range (displayed) | Status | Notes |
|---|---|---|---|---|---|
| Decay | `kDecay` | 0.3 s – 10 s | ✅ | Valhalla plate goes to ~20 s, VVV to 70 s. Shorter than Valhalla by design. |
| Damping | `kDamping` | 0 – 100 % | ✅ | |
| Predelay | `kPredelayMs` | 0 – 500 ms | ✅ | |
| Width | `kWidth` | 0 – 100 % | ✅ | Valhalla is 0 – 200 %. AestraVerb is 0 – 1 in the same style. |
| Mix | `kMix` | 0 – 100 % | ✅ | Equal-power crossfade `cos/sin` — `AestraVerb.h:864-867`. |
| Bypass | `kBypass` | OFF / ON | ✅ | |
| Size | `kSize` | 0.10× – 2.00× | ✅ | |
| Diffusion | `kDiffusion` | 0 – 100 % | ✅ | |
| Mod Rate | `kModRate` | 0 – 200 % | ✅ | Multiplies base LFO rates. |
| Mod Depth | `kModDepth` | 0 – 8 smp | ✅ | |
| Mode | `kMode` | Room / Hall / Plate / Cathedral / Chamber / Bright Hall / Ambience / Scoring / Smooth Plate | ✅ | 9 modes with per-mode DSP coefficients. |
| Low Cut | `kLowCut` | 20 – 20000 Hz | ✅ | Post-reverb one-pole HPF. |
| High Cut | `kHighCut` | 200 – 20000 Hz | ✅ | Post-reverb one-pole LPF. |
| Freeze | `kFreeze` | OFF / ON | ✅ | Input-attenuated infinite sustain. |
| Attack | `kAttack` | 0 – 100 % | ✅ | Controls early-reflection envelope (soft→hard). |
| Shape | `kShape` | 0 – 100 % | ✅ | Controls attack shape (soft→sharp). |
| Pre Sync | `kPredelaySync` | Off / 1/16 / 1/8 / 1/4 / 1/2 / 1 bar / 2 bars | ✅ | Tempo-synced predelay division. |
| Mod Char | `kModCharacter` | Random / Chorus / Chaotic | ✅ | Modulation character selector. |

| Missing control (per public Valhalla behavior) | Status | Freeze impact |
|---|---|---|
| Dedicated `Low Cut` / `High Cut` EQ knobs (post-reverb) | ✅ | Implemented. |
| Dedicated `Attack` / `Shape` (early/late balance, reverse/gate envelope) | ✅ | Implemented. |
| `PreDelay` tempo-sync (1/4, 1/8, dotted…) | ✅ | Implemented as `kPredelaySync`. |
| `Freeze` button | ✅ | Implemented as `kFreeze`. |
| `Modulation` on/off switch (independent of Depth) | 🟡 | Achievable by setting Depth=0 today; no dedicated switch. |
| `Mix Lock` (Valhalla's "lock mix across preset changes") | ✅ | Implemented in editor. |
| `Stereo` mode selector (Mono / Mono→Stereo / Stereo) like Shimmer | 🟡 | AestraVerb is already true stereo. Achievable implicitly via `Width=0`. |
| `Density` (Plate's Density knob) | 🟡 | Partially covered by `kDiffusion`; not 1:1. |
| `Pre-delay Space` / `Space` (Room v2.0.5) | ❌ | ⛔ New param. |
| `Mod Character` (chorus vs random, like Random Space vs Chorus Space) | ✅ | Implemented as `kModCharacter` (Random / Chorus / Chaotic). |
| `Color` (VVV's 1970s / 1980s / Now) | ❌ | ⛔ New param + new DSP. |
| `Output` gain / trim | 🟡 | AestraVerb has `kWetMakeupGain = 4.2` baked in — `AestraVerb.h:146, 435`. No user trim. Could be a new public param (⛔) or hidden. |
| `Solo` / `Mute` | ❌ | 🟡 Could be added as a `kMute` param (⛔) or a UI-only control that toggles `kBypass` for a different visual. |
| `Predelay Tap` (pre-echo character) | ❌ | ⛔. |

---

## 3. UI / Editor / Visualization

The editor lives at `AestraUI/Widgets/AestraVerbEditor.cpp` (838 lines, including `.h`). Window size fixed at `kWinW × kWinH`.

| Affordance | Valhalla | AestraVerb | Status |
|---|---|---|---|
| Resizable GUI (drag-to-resize) | VVV/Room/Plate are resizable from a corner. | `enforceBoundsInParent` + `onResize` exist (`AestraVerbEditor.cpp:276-281, 283-336`) but `resizeEditor()` on the plugin returns false — `AestraVerb.h:629`. | 🟡 Panel window resizes, but host cannot resize the plugin's own editor frame. |
| Mode picker | VVV has a 1-of-N popup button (lower-left). Plate has a 1-of-N popup (Mode menu). | 4 category pills (Room / Hall / Plate / Special) with mode dropdown for 9 modes — `AestraVerbEditor.cpp:87-103`. | ✅ |
| Big macro Decay knob | VVV places Decay as a large central radial knob. | Yes, 174 px macro knob with tick marks and a gold arc indicator — `AestraVerbEditor.cpp:466-505`. | ✅ |
| Knob layout (sections) | VVV uses bordered sections (Mix/PreDelay → Decay → Damping → Shape → Diffusion → Modulation → EQ). | 3 labeled sections: "PREDELAY / SIZE", "TONE", "MODULATION" — `AestraVerbEditor.cpp:665-679`. | ✅ |
| Tooltip on hover | VVV self-documents: hover shows a description. | `formatParameterValue` is wired (`AestraVerbEditor.cpp:154-189`); no live text bubble. | ❌ |
| Tooltips (tutorial text below the GUI) | VVV shows parameter explanation text below the controls. | Not present. | ❌ |
| EQ knobs (post-reverb tone) | VVV HiCut, LowCut, both on the panel. | `kLowCut` + `kHighCut` knobs in TONE section — `AestraVerbEditor.cpp:237`. | ✅ |
| Reverb Response / decay visualization | VVV has none as a built-in panel. Room/Plate have none. VVV shows a small bar above some modes. | "REVERB RESPONSE" animated envelope — `AestraVerbEditor.cpp:564-597`. Procedural / cosmetic — does not measure actual output. | 🟡 Visual only. |
| Stereo Field visualization | n/a | Animated ellipse + swirl — `AestraVerbEditor.cpp:601-620`. Procedural / cosmetic. | 🟡 Visual only. |
| Built-in presets | VVV: ~150 presets across categories (Ambiences, Chambers, Gated, Halls, Huge, Plates, Rooms, …). Plate: 4 size banks × multiple, plus Don Gunn designer set. Room: many. | 45 factory presets across 9 modes (5 per mode) — `AestraVerbEditor.cpp:117-217`. | 🟡 Coverage improved; Valhalla still has 3–10× more. |
| Preset artwork | Plate/Room/Supermassive use text + category icons. | 20 PNGs in `AestraAssets/plugins/AestraVerb/presets/`, loaded per-preset — `AestraVerbEditor.cpp:119-217`. | ✅ |
| User preset save/load | All Valhalla plugins do. | Save (`saveUserPreset`) / load (`loadUserPresets`) / delete (`deleteUserPreset`) via `.aeverb` files — `AestraVerbEditor.cpp:1142-1198`. | ✅ |
| Preset navigation (◀ ▶ arrows, ⌘-arrow) | All Valhalla plugins. | ◀ ▶ arrows + preset strip with scroll — `drawPresetNav`, `drawPresetStrip`. | ✅ |
| A/B compare, undo | All Valhalla plugins. | A/B compare with `ABState` snapshots — `drawABButtons`, `hitTestAB`. | ✅ |
| Mix Lock | Plate/Room/Supermassive. | Mix lock toggle — `drawMixLock`, `m_mixLocked`. | ✅ |
| Bypass button on the panel | All Valhalla plugins (top right). | Bypass pill in the editor — `drawBypassPill`, `hitTestBypass`. | ✅ |
| Editor inside the DAW vs floating window | Floating. | AestraPanelWindow (floats above the host). | ✅ |
| Right-click → "Show in Finder" or value edit | Common in modern plugins. | Not present. | ❌ |
| Keyboard shortcuts (fine / coarse drag, double-click reset) | All Valhalla plugins. | Not implemented for verb knobs. | ❌ |

---

## 4. Presets / Content

Freeze note explicitly allows "More factory presets" and "Preset artwork and tooltip copy" as the primary safe expansion path.

| Content area | Have | Need |
|---|---|---|
| Factory presets | 45 across 9 modes (5 per mode) — `AestraVerbEditor.cpp:117-217` | ≥ 60+ across additional categories to match user expectation. Categories: Ambiences, Chambers, Gated, Halls, Huge Spaces, Plates, Rooms, Cathedral, Reverse, Scoring. Each preset should target a known source (vocal, drums, guitar, synth, snare, piano). |
| Preset artwork (PNG) | 20 PNGs in `AestraAssets/plugins/AestraVerb/presets/` | One per remaining preset, matching existing style. |
| Preset tooltip copy | Tooltips embedded in `m_presets` array — `AestraVerbEditor.cpp:119-217` | Ensure every preset has a one-sentence description. |
| Designer preset set | None | One guest designer pack is Valhalla's signature move (Don Gunn sets on Plate/Room). |
| User preset save/load | Save (`saveUserPreset`) / load (`loadUserPresets`) / delete (`deleteUserPreset`) via `.aeverb` files — `AestraVerbEditor.cpp:1142-1198` | Done. |
| Preset gain staging | Existing mix is fixed. | AestraVerb already has per-mode `kWetCompGain` — `AestraVerb.h:149-159`. Per-preset gain trim would be polish. |
| Listening examples | None | Common ask; "Valhalla comes with 50+ examples" perception. |

---

## 5. Hosting / Lifecycle / Safety

| Area | Have | Reference |
|---|---|---|
| Plugin format | Internal (`PluginFormat::Internal`), 2-in/2-out, no MIDI, has editor flag. | `AestraAudio/src/Plugin/BuiltInPlugins.cpp:79-97` |
| Stable plugin ID | `com.Aestrastudios.verb` at v0.1.0 | `BuiltInPlugins.cpp:82-85` |
| Parameter state magic | `kStateMagic = 0x52564205` ("RVB" v5). `loadState` accepts v1–v5 with migration. | `AestraVerb.h:82, 755-780` |
| Real-time safety | Confirmed in freeze note: no lock in audio path after `3119452c`. | freeze note + `AestraVerb.h:185-541` |
| Safety clamp / sanitization | `sanitize()` at every stage; pre/post clamp diagnostics gated behind `AESTRA_REVERB_DIAGNOSTICS`. | `AestraVerb.h:1180-1186, 499-500, 509-510` |
| Lab regression coverage | `ReverbSIMDParityTest`, `ReverbSafetyRegressionTest`, `AestraReverbMaterialLab`, `AestraReverbQualityLab`, `AestraReverbBenchmark`. | `Tests/AestraAudio/*` |
| Preset state roundtrip | Not present | Required for "save preset to project" / "load preset on next launch" workflows. |
| Watchdog / crash handling | `getWatchdogStats / isBypassedByWatchdog / isCrashed` exist but are stubs. | `AestraVerb.h:634-637` |
| Tail handling | `getTailSamples()` returns 20 s × `m_sampleRate`. | `AestraVerb.h:633` |
| Latency | `getLatencySamples()` returns 0. | `AestraVerb.h:632` |
| Freeze (host-driven transport freeze) | `kFreeze` param + freeze pill UI — `AestraVerbEditor:drawFreezePill`. | ✅ |
| Headless / CI coverage | All tests run headless under `AESTRA_HEADLESS_ONLY=ON`. | freeze note Test Evidence. |

---

## 6. Documentation / Public-Facing

| Doc | Have | Need |
|---|---|---|
| Public plugin page | None. | Per AGENTS.md §17, only if implemented and validated. |
| Internal architecture note | `aestra-verb-beta-freeze-note.md` ✅. Quality baseline + material lab reports. | Continuation of these for new presets. |
| User manual (controls, signal flow) | None for end users. | Short one-pager with sections, mode guide, recommended starting points per source. |
| Tooltips on each knob | Engine returns display strings (`AestraVerb.h:574-599`); UI does not surface them as hover text. | Add hover tooltips. |
| Preset copy | None. | Per-preset short description. |

---

## 7. Where Valhalla's "magic" actually comes from (evidence)

From the research, the things that make Valhalla feel "Valhalla-quality" are not the FDN math — modern 8-line FDNs are table stakes. The differentiators are:

1. **Mode breadth with distinct personalities.** 22 VVV modes, 12 Plate modes, 12 Room modes. AestraVerb has 9.
2. **Per-mode tonal sculpting baked in.** Plate has 12 "metals" that don't change the algorithm but change the EQ and density profile. AestraVerb's tonal sculpting exists per-mode (mud HP, box cut, wet comp) — `AestraVerb.h:176-183, 276-362`.
3. **Modulation is treated as a first-class character**, not a knob. Random vs chorus vs chaotic is a switch, not a depth value. AestraVerb now has `kModCharacter` (Random / Chorus / Chaotic).
4. **Articulation controls beyond Decay.** Attack / Shape / Early-Late balance. AestraVerb now has `kAttack` and `kShape`.
5. **Post-reverb EQ as a panel control**, not an internal one. AestraVerb now has `kLowCut` and `kHighCut` knobs.
6. **Freeze, Mix Lock, presets, preset categories, listening examples, designer packs.** AestraVerb now has Freeze, Mix Lock, A/B compare, preset navigation, user presets, 45 factory presets.
7. **UX polish.** Tooltips, A/B, undo, host tempo sync, "show in Finder", keyboard fine/coarse drag. AestraVerb has tooltips, A/B, preset nav, tempo sync (`kPredelaySync`). Missing: undo, "show in Finder", keyboard fine/coarse drag.
8. **Optional color models** (1970s/1980s/Now) that don't add params but switch entire sonic flavor.

Items 1–2 and 8 remain gated by the beta freeze (⛔).
Items 3–6 are now mostly implemented (✅) with some gaps.
Item 7 is split: tooltips, A/B, preset nav, tempo sync are implemented; undo, Finder, keyboard drag remain.

---

## 8. Recommended next moves (in order of leverage vs freeze cost)

Group A — work allowed by the current freeze, highest leverage:

- **A1. Further factory preset expansion.** 45 presets exist (5 per mode). Aim for 60+ with category diversity (Ambiences, Chambers, Gated, Halls, Huge Spaces, Plates, Rooms, Scoring, Cathedral, Reverse, Vocal Plate, Snare Plate, Drum Room, Vocal Hall, Synth Pad, etc.). Pure content, no DSP.
- **A2. Preset artwork expansion.** 20 PNGs exist; cover remaining 25 presets.
- **A3. Hover tooltips on every knob.** Engine returns display strings; wire live text bubble.
- **A4. Listening examples** (short audio clips per preset) — only if the repo actually ships audio.
- **A5. Public one-pager manual** (modes, sections, recommended starting points).
- **A6. Double-click knob reset.** UI-only.
- **A7. Keyboard shortcuts (fine / coarse drag).** UI-only.
- **A8. Undo support for preset and knob changes.** UI-only.

Group B — opens the freeze, requires explicit sign-off:

- **B1. Expand beyond 9 modes** (Random Space, Dirty Hall, Dirty Plate, Nonlin, etc.). Each new mode = new `Mode` enum entry + new `constantsForMode` branch + new `ModeConstants`.
- **B2. Add `kColor` (1970s / 1980s / Now).** New param + downsampler + fixed-point quantizer.
- **B3. Add `kPlateCharacter` (Chrome/Steel/Cobalt/Brass/etc.).** New param + per-mode biquad set.
- **B4. Shimmer as a sibling plugin** (not a mode). Separate ID, separate UI.
- **B5. Add `kDensity` (Plate-style density knob).** New param.
- **B6. Add `kPreDelaySpace` (Room-style space control).** New param.
- **B7. Add `kOutputTrim` user gain knob.** New param.

Group C — research only, no code yet:

- **C1. Listening validation with real material** (vocals, snare, drums, piano, synth pad, guitar). The freeze note's #1 final limitation.
- **C2. Pro Tools / Cubase / Live / Reaper plugin-host lifecycle test** beyond internal built-in path. The freeze note's #3 final limitation.
- **C3. Decide if AestraVerb is "the" Aestra reverb** (one algo, many modes) or if Aestra will get sibling reverb plugins (AestraRoom, AestraPlate, AestraShimmer) à la Valhalla. The architecture decision shapes every other roadmap item.

---

## 9. What this document is NOT

- Not a release plan.
- Not a commitment to ship any specific feature.
- Not a benchmark or A/B test result. A/B vs Valhalla is out of scope; AestraVerb's quality is evaluated against its own lab baseline (`labs/reverb/quality/reverb_quality_baseline.md`).
- Not a public-facing claim. Per AGENTS.md §17, no public docs should describe features that are not implemented and validated.

---

## 10. Sources (public information only)

- Valhalla VintageVerb product page — https://valhalladsp.com/shop/reverb/valhalla-vintage-verb/
- "Valhalla VintageVerb: The MODES" — Sean Costello blog, 2023-02-10 — https://valhalladsp.com/2023/02/10/valhallavintageverb-the-modes/
- "ValhallaVintageVerb 4.0.0 Update. New Reverb Modes: Chamber1979 and Hall1984" — 2023-12-13 — https://valhalladsp.com/2023/12/13/valhallavintageverb-4-0-0-new-reverb-modes-chamber1979-and-hall1984/
- Valhalla Plate product page — https://valhalladsp.com/shop/reverb/valhalla-plate
- "ValhallaPlate: The Reverb Modes" — 2015-11-08 — https://valhalladsp.com/2015/11/08/valhallaplate-the-reverb-modes/
- "ValhallaPlate: The Controls" — 2015-11-08 — https://valhalladsp.com/2015/11/08/valhallaplate-the-controls/
- Valhalla Room product page — https://valhalladsp.com/shop/reverb/valhalla-room
- "ValhallaRoom Updated to 2.0.0. New Space & Lo Cut Controls" — 2023-11-20 — https://valhalladsp.com/2023/11/20/valhallaroom-updated-to-2-0-0-new-space-lo-cut-controls/
- Valhalla Shimmer product page — https://valhalladsp.com/shop/reverb/valhalla-shimmer
- ValhallaSupermassive modes — https://valhalladsp.com/2023/11/21/valhallasupermassive-updated-to-version-3-0-0-two-new-modes-leo-and-virgo/
- "Reverb Types — Effect-O-Pedia" — https://valhalladsp.com/2018/05/14/effect-o-pedia-reverb-types
- Wikipedia: ValhallaDSP — https://en.wikipedia.org/wiki/ValhallaDSP
- ValhallaPlate review (Audiofanzine) — https://en.audiofanzine.com/algorithmic-reverb/Valhalla-DSP/valhallaplate/editorial/reviews/what-s-on-your-plate.html
- "ValhallaShimmer: a bit of history" — 2010-11-23 — https://valhalladsp.com/2010/11/23/valhallashimmer-a-bit-of-history

In-repo evidence files referenced above:

- `AestraAudio/include/Plugin/AestraVerb.h` (full plugin, 1580 lines)
- `AestraUI/Widgets/AestraVerbEditor.cpp` (UI, 1527 lines)
- `AestraAudio/src/Plugin/BuiltInPlugins.cpp` (registry)
- `docs/audits/aestra-verb-beta-freeze-note.md` (freeze policy)
- `labs/reverb/quality/reverb_quality_baseline.{md,json}` (current quality baseline)
