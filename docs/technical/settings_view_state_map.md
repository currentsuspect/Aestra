# Settings and view state: the flow map

**Status:** audit, 2026-07-27
**Scope:** U3 of the week — establish what state is authoritative and how it
flows, *before* reorganising any of it
**Companion to:** `docs/technical/muse_state_ownership.md` (which established
*ownership*; this establishes *flow*)

---

## Why a flow map and not a fix list

The ownership audit answered "who owns this". It did not answer "does a change
made here arrive there", and that is where the defects in this family live. So
every row below is traced through five stages:

```text
displayed value → editing state → committed model state → persistence source → runtime consumer
```

A defect in this family is a **boundary where one side updates and the next does
not**. Naming the boundary is the deliverable; which widget or serializer to
change is downstream of it.

Read at `develop`, 2026-07-27.

---

## Summary of boundary failures

| # | Boundary | Consequence |
|---|---|---|
| **B1** | committed → displayed, for views | `view.current` reports a view open while it is off screen |
| **B2** | committed → persistence, for panel geometry | a complete, validated, bounded round-trip that transports nothing |
| **B3** | editing → committed, for General settings | the page applies nothing at all |
| **B4** | persistence → runtime consumer, for 7 `Preferences` fields | values persist and govern nothing |
| **B5** | two independent `UIState` types | "UI state" means two different things depending on the file |

---

## View state

### The flow

| stage | where |
|---|---|
| displayed | panel `isVisible()` |
| editing | none — no transient edit state for views |
| committed | `AestraContent::m_viewState.*Open` |
| persistence | `Source/Core/UIState.h` — `browserVisible`, `mixerVisible` only |
| runtime consumer | `setViewOpen` → `panel->setVisible()` |

### B1 — committed and displayed disagree, and the query reads whichever is handy

`AestraContent::isViewOpen` (`AestraContent.cpp:1919`) mixes its sources:

```cpp
case ViewType::Mixer:     return m_viewState.mixerOpen;              // intent
case ViewType::PianoRoll: return m_viewState.pianoRollOpen;          // intent
case ViewType::Sequencer: return m_viewState.sequencerOpen;          // intent
case ViewType::Playlist:  return m_viewState.playlistActive;         // intent
case ViewType::History:   return m_historyPanel->isVisible();        // actual
case ViewType::Takes:     return m_takesPanel->isVisible();          // actual
```

Four report **intent**, two report **actual**. They normally agree, because
`setViewOpen` writes both.

They stop agreeing in Audition mode: `setViewFocus` (`:2141`) hides the mixer,
piano roll and Arsenal panels **without touching `m_viewState`** — deliberately,
because `m_viewState` is the *restore* state that leaving Audition re-applies.

> So in Audition mode `view.current` reports `mixer: open` while the mixer is off
> screen, and `view.open {view: mixer}` returns `changed: false` without making
> anything appear.

**Observed, not inferred** (driven on the running app, 2026-07-27):

| step | screen |
|---|---|
| F3 — open mixer, Timeline focus | mixer panel visible |
| click **Audition** | mixer panel **gone** |
| click **Timeline** | mixer panel **back**, unchanged |

The third step is the decisive one. The mixer returning without being reopened
proves `m_viewState.mixerOpen` stayed `true` for the whole Audition period — so
`isViewOpen(Mixer)`, which reads exactly that field, was answering `true` while
the mixer was off screen. The divergence is behavioural, not theoretical.

**Both concepts are load-bearing.** The restore state must keep saying "the mixer
was open" or leaving Audition cannot put it back. This is not a bug in
`m_viewState`; it is a query that collapses two real properties into one boolean.

**Resolution:** expose both — `requestedOpen` (from `m_viewState`) and `visible`
(from the panel). All six views answer from the same two sources rather than four
from one and two from the other. `view.current` gains a field; nothing loses the
restore semantics it needs.

Also unexposed today: `m_viewFocus` (Arsenal / Timeline / Audition / RoutingMap /
PianoRoll), which is the state that *causes* the divergence. An agent cannot
currently see it.

**Update (phase-3, workspace-panel ownership):** the active workspace and the
remembered-open overlay flags ARE now persisted in the `.aes` project file as
optional `ui.viewFocus` / `ui.pianoRollOpen` / `ui.sequencerOpen` keys
(`ProjectSerializer::UIState`), and reapplied through `AestraApp::applyUIState`
→ `AestraContent::restoreWorkspaceState`. Muse `view.current` reports the active
workspace through its `focus` field, whose protocol string now lives in
`WorkspaceFocusModel::workspaceFocusName` (adds `pianoRoll`); the B1 `status`
field resolution can also consume that dedicated verb without relying on a
`viewFocus` key.

---

## Panel geometry

### B2 — a fully-implemented round-trip that carries nothing

`ProjectSerializer::PanelState` (`ProjectSerializer.h:48`) holds
`title`, `x`, `y`, `width`, `height`, `expandedHeight`, `minimized`,
`maximized`, `userPositioned`. The serializer implements **both** directions:

- write — `ProjectSerializer.cpp:1031-1045`
- read — `ProjectSerializer.cpp:1453-1470`, fully bounds-validated, capped by
  `PROJECT_MAX_UI_PANELS`

And the application layer implements **neither**:

```cpp
ProjectSerializer::UIState AestraApp::captureUIState() const {   // :1764
    ProjectSerializer::UIState state;
    // ... settingsDialogVisible + settingsDialogActivePage only
    return state;                        // state.panels is never populated
}

void AestraApp::applyUIState(const UIState& state) {             // :1777
    // ... settings dialog only; state.panels is never read
}
```

So every save writes an empty `panels` array, and every load parses, validates
and bounds-checks panel geometry into a vector that is then discarded.

This is the most complete example of the boundary failure in the codebase: the
hard part (validated, bounded, versioned serialization) is done and correct, and
the two four-line ends are missing. Note it is **not** dead code to delete —
panel geometry is worth persisting. It is an unfinished feature that looks
finished from either end.

---

## Settings

Three unrelated authorities, as the ownership audit found. Traced here as flows.

### Audio device configuration — the one healthy path

| stage | where |
|---|---|
| displayed | `AudioSettingsPage` widgets |
| editing | none — controls call through immediately |
| committed | `AudioDeviceManager` |
| persistence | **none** (`Preferences::audioDeviceId` etc. are not read — see B4) |
| runtime consumer | `AudioDeviceManager` itself; the stream |

`AudioSettingsPage` holds no state (`:292-351`, `:760-777`). Each control calls
`setPreferredDriverType` / `switchDevice` / `switchInputDevice` /
`setSampleRate` / `setBufferSize`, each returns success or failure, and the page
re-reads via `getCurrentConfig()` (`:446`, `:918`).

**No boundary failure.** This is the shape the others should be measured
against. Its one gap is persistence: device choice does not survive a restart,
because the `Preferences` fields that look like they hold it are never read.

### Theme — two authorities, both required

| stage | where |
|---|---|
| displayed | `AppearanceSettingsPage` |
| editing | `m_pendingTheme` (a real editing state — cancel reverts) |
| committed | `NUIThemeManager::setActiveTheme` |
| persistence | `Preferences::theme` + `save()` |
| runtime consumer | `AestraWindowManager.cpp:156` on startup, with a fallback |

`applyChanges` (`:56`) writes both. **A verb or a future settings path must
write both too**, or the change evaporates on restart. The one place in settings
with a genuine editing stage, and it is handled correctly.

### B3 — General settings applies nothing

```cpp
void GeneralSettingsPage::applyChanges() {
    m_dirty = false;
}
```

The editing → committed boundary is empty. `cancelChanges()` resets the projects
path to a hardcoded Windows path. Whatever this page appears to configure, it
does not.

### B4 — seven `Preferences` fields with no runtime consumer

`Preferences::instance()` has **six call sites in the entire tree**. Per field:

| field | consumer outside `Preferences.*` |
|---|---|
| `theme` | live — window manager + appearance page |
| `autoSaveEnabled` | live — `AestraApp.cpp:234` |
| `audioDeviceId` | **none** |
| `exclusiveMode` | **none** |
| `showGrid` | **none** |
| `snapToGrid` | **none** |
| `gridSize` | **none** |
| `autoSaveIntervalSeconds` | **none** |
| `recentFiles` | **none** (`addRecentFile` has no callers) |

The audio fields are shadowed by `AudioDeviceManager`, which never consults
them. The grid fields are shadowed by `TrackManagerUI`'s own `m_snapEnabled` /
`m_snapSetting`.

> A `settings.setAudioDevice` verb written against `Preferences` would validate,
> persist, report success, and change nothing.

**These fields are not one bucket.** Sorting them is the next step, and the
buckets are different work:

- **authoritative** — `theme`, `autoSaveEnabled`. Keep.
- **persistence-only mirror of a live authority** — `audioDeviceId`,
  `exclusiveMode`, `showGrid`, `snapToGrid`, `gridSize`. These *should* exist:
  the live authority has no persistence today. Wire them to their authority, or
  delete them and accept the setting is session-only. Either is honest; the
  current state is not.
- **dead** — `autoSaveIntervalSeconds` (the interval is not configurable),
  `recentFiles` (nothing ever adds one). Delete unless a consumer is coming.

---

## B5 — "UI state" names two different things

| type | holds | persisted to |
|---|---|---|
| `Source/Core/UIState.h` | window geometry, browser width/visibility, mixer height/visibility, browser expanded folders, last path, timeline zoom + scroll | a global UI-state file, per install |
| `ProjectSerializer::UIState` | settings-dialog visibility + active page, panel geometry (B2) | inside the `.aes` project file |

Both are named `UIState` and both are reachable from `AestraApp`. They are
genuinely different things — one is per-install, one is per-project — and the
shared name invites exactly the confusion of writing to the wrong one.

Within the app-level `UIState`, two fields have no consumer:
`mixerHeight` and `verticalZoom` (0 references outside the struct). Same
treatment as B4's dead bucket.

---

## What this map says to do, in order

1. **B1 — view truth.** Expose `requestedOpen` and `visible` separately; make all
   six views answer from both sources. Consider surfacing `ViewFocus`. Smallest
   change, unblocks further `view.*` work, and the current query is actively
   misleading an agent.
2. **B4 — bucket the `Preferences` fields** into authoritative / mirror / dead
   and act on each bucket differently. This is the prerequisite for any
   `settings.*` verb, and for B3.
3. **B3 — General settings.** Once the buckets exist there is something for
   `applyChanges` to actually apply.
4. **B2 — panel geometry.** Finish the two ends, or delete the serializer support
   and say panel geometry is not persisted. Not urgent; it is inert rather than
   wrong.
5. **B5 — rename.** Cosmetic until someone writes to the wrong one; cheap
   insurance once the others are settled.

**Not decided here:** whether `AudioDeviceManager`'s setters are safe off the UI
thread (relevant to a headless `settings.audio.*`), and whether device choice
*should* persist — that is a product question, not an architectural one.

---

## Verification note

Mixed rungs, stated per claim rather than as a blanket.

**B1 is observed** — driven on the running app; see the table under B1. It began
as a prediction from reading `setViewFocus`, and was checked before being written
up, because the fix's shape depends on both concepts being real.

**B2, B3, B4, B5 are at the audit rung** — each is a read of current code with a
file:line, none has been driven. They are structural claims (a field with no
consumer, a function body that assigns one bool) where the code is strong
evidence, but none is a behavioural observation and none should be quoted as one.
