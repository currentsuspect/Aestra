# Changelog — 2026 Q3 (June–August)

All notable changes for Aestra in Q3 2026 are documented here.

## v0.7.0-alpha — Routing, Automation & Hosting Milestone (2026-08-16)

331 PRs merged (#624–#771). The story of this milestone is coherence: routing and gain staging are correct end to end, automation targets the instance it was drawn for, the Master channel is a real plugin host whose latency sits in the compensation graph, and the piano roll grew from an editor into a workflow.

> **Versions, clarified.** `v0.7.0-alpha` is the *application release version*. It is unrelated to `ProjectSerializer::PROJECT_VERSION_CURRENT` (the project/file format version, currently 3): the format version changes only when the `.aes` format itself changes, never on a release bump.

### Routing & Mixing Correctness

- Unity gain law end to end: channel strips use the stereo-balance law, so routing a signal through any mixer channel no longer attenuates it — and the same law governs live playback, offline export, isolated bounce, audition preview, and input monitoring (parity pinned by tests).
- Fader and pan applied once: one UI gesture no longer writes two stores that the engine multiplied (−6 dB on the fader used to play at −12 dB).
- Routing command seam with mutation-time cycle rejection, stable send IDs, and master-legality enforcement; render-path semantics match the routing contract (V1/V2/V3) with live/offline parity.
- Master channel is a valid plugin host: insert chains process on the master bus before the fader and safety limiter, persist in the project file, and their latency feeds the plugin-delay-compensation graph (reported project latency is now the real end-to-end delay).
- Master-routed clips obey the live solo gate; isolated-track bounce renders only the selected track's stage and excludes the master stage by design — both halves of the isolation contract are pinned by tests.

### Automation

- Automation targets the instance it was drawn for, never the slot it occupied — chain reordering keeps curves bound to their plugin.
- Empty lanes are automatable: the first click creates a neutral Volume curve bound to the lane's channel; edits rebuild the audio graph and mark the project dirty.
- Leftover demo automation removed from the default project and from previously saved projects.

### Piano Roll & Sequencing

- Selection subdivision by snap, proportional selection stretching, chord-aware stroke input, harmony context persistence, contextual editor workspace, playhead alignment in timeline mode, and bidirectional sampler ping-pong loops.

### UI & Editing

- Design constitution pass across mixer, browser, timeline, and arsenal.
- Audio Clip editor: musical pitching, trim persistence, live waveform updates, resize-safe layout.
- Timeline geometry has a single authority for grid and x→beat conversion.

### Reliability & Hardening

- Security, durability, and realtime contract lanes are CI-authoritative; test contract coverage and founder decision citations are enforced gates.
- Plugin hosting compiles in CI; Windows plugins run sandboxed out-of-process; CLAP SDK vendored.
- The routing/automation/master/UI triage branch went through four review rounds (human + CodeRabbit) with 20 findings addressed before merge.
- License-signing worker dependencies cleaned (undici CVEs).
