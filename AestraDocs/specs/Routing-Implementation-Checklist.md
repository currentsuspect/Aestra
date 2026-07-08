# Routing Implementation Checklist
Date: 2026-04-11
Status: Active — routing is the current workstream

Cross-ref: EPIC G items in `docs/technical/v1_beta_task_list.md`

---

## Release Criteria (from EPIC G Routing Release Bar)
These are the user-facing workflows that must work before routing is "done":
- [ ] Route multiple drum tracks into a drum bus, then bus to master
- [ ] Create a reverb return and send several tracks to it
- [ ] Feed a compressor sidechain without making that send audible
- [ ] Disable a track's master send and route it only to a subgroup
- [ ] Solo a track and still get the intended return/sidechain behavior
- [ ] Add a high-latency plugin anywhere and keep playback/record/render aligned (PDC)
- [ ] Print a bus to audio
- [ ] Reopen the project and get the exact same graph back

---

## Phase 1: Fix Known Bugs (Do First — unblocks everything else)

These bugs were found 2026-04-11 in the routing render loop.

- [ ] **BUG-1 (P0):** Send gains not smoothed — apply `LinearSmoothedValue` to send gains like main volume
- [ ] **BUG-2 (P0):** No cycle detection in routing graph — detect and warn, prevent silent tracks
- [ ] **BUG-3:** Send pan uses different pan law than main pan — unify or document
- [ ] **BUG-4:** `std::unordered_map` allocation every callback — pre-allocate or embed in graph snapshot
- [ ] **BUG-5:** Per-sample send iteration — batch per-send across the block

---

## Phase 2: Group Bus Tracks

A group bus is a track that receives audio from other tracks, processes it (insert FX), and outputs to master or another group. No clips, no timeline content.

- [ ] **G-BUS-1:** Define `TrackType` enum: `Audio`, `Group`, `Return`, `MIDI` (extend existing track model)
- [ ] **G-BUS-2:** Group tracks skip clip playback in render loop (no `ClipRenderState`)
- [ ] **G-BUS-3:** Group tracks accumulate audio from routed sources before running inserts/fader
- [ ] **G-BUS-4:** UI: add "Create Group Bus" action (from track context menu or one-click from selection)
- [ ] **G-BUS-5:** UI: group tracks visually distinct (different strip style or icon)
- [ ] **G-BUS-6:** Serialization: group bus type + routing survives save/load
- [ ] **G-BUS-7:** Test: 3 drum tracks → drum bus → master, bus has compressor, sounds correct

---

## Phase 3: Return / Aux Tracks

A return track has no clips. It only receives audio via sends. It has its own insert chain and fader. Used for shared FX like reverb/delay.

- [ ] **RET-1:** Return track type — no clip playback, no timeline representation
- [ ] **RET-2:** Return tracks accumulate send inputs before running their own inserts/fader
- [ ] **RET-3:** Return tracks output to master (or to a group bus) via mainOutputId
- [ ] **RET-4:** UI: add "Create FX Return" action
- [ ] **RET-5:** UI: return tracks in mixer but not on timeline (or timeline shows placeholder)
- [ ] **RET-6:** Return track solo/mute behavior — soloing a return mutes sends from non-soloed tracks
- [ ] **RET-7:** Serialization: return track type survives save/load
- [ ] **RET-8:** Test: create reverb return, send 3 tracks to it at different levels, verify wet/dry mix

---

## Phase 4: Solo/Mute/Cue Semantics Through Routing

This is where the most subtle bugs will live. Get this right and the DAW feels trustworthy.

- [ ] **SOLO-1:** Define and document solo-in-place behavior (current implementation in BFS propagation)
- [ ] **SOLO-2:** Solo a track in a group — group stays audible, non-sibling tracks mute
- [ ] **SOLO-3:** Solo a return track — only sends from soloed source tracks feed it
- [ ] **SOLO-4:** Solo-safe on groups/returns — solo-safe groups always audible when any solo is active
- [ ] **SOLO-5:** Mute behavior: muting a track silences its sends (pre-fader sends still work if pre-fader)
- [ ] **SOLO-6:** G-011: Define cue vs solo-in-place — pick one for Beta, document it
- [ ] **SOLO-7:** Test matrix: every solo/mute combination through groups, returns, sends, sidechain

---

## Phase 5: PDC (Plugin Delay Compensation) Through Routing

G-012 from EPIC G. Deferred to after basic routing works, but required for Beta.

- [ ] **PDC-1:** Measure plugin latency on load, store on effect chain
- [ ] **PDC-2:** Compute total latency per track (sum of insert chain latencies)
- [ ] **PDC-3:** Delay compensation on sends — align source output to destination's latency
- [ ] **PDC-4:** Delay compensation on group buses — compensate for upstream track latencies
- [ ] **PDC-5:** Sidechain alignment — sidechain feed delayed to match plugin's expected timing
- [ ] **PDC-6:** Export/offline render respects PDC
- [ ] **PDC-7:** Test: high-latency plugin on one track, send to return, verify alignment

---

## Phase 6: Routing UX Polish

- [ ] **UX-1:** G-009: Routing visibility — inspector/mini-matrix showing all routes
- [ ] **UX-2:** G-015: One-click actions — create bus from selection, create FX return, route to bus
- [ ] **UX-3:** G-016: Routing templates — drum bus, vocal FX return, parallel comp bus
- [ ] **UX-4:** Destination picker blocks self-routes and cycles (G-010a partially done)
- [ ] **UX-5:** Visual indicators on mixer strips: bus/send/sidechain badges

---

## Phase 7: Serialization & Export Trust

- [ ] **SER-1:** G-017: Route-state round-trip test — save, reopen, verify identical graph
- [ ] **SER-2:** Group bus + return track state survives save/load
- [ ] **SER-3:** G-013: Bus print / stem export respects routing
- [ ] **SER-4:** Export through group buses and returns produces correct output
- [ ] **SER-5:** Reopen on different machine (different sample rate) — routing still correct

---

## Non-Goals for v1 Beta (Do NOT Do)
- Full arbitrary graph-editing UI
- Surround / Atmos / ambisonics
- Per-send FX chains
- REAPER-style high-channel-count arbitrary internal routing
- Complex hardware insert calibration wizards
