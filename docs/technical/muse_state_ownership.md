# Muse: application state ownership

**Status:** audit, 2026-07-27
**Scope:** the five `HostVerbDomain` values — Project, Settings, View, Browser, Dialog
**Companion to:** `AestraAudio/include/Commands/HostVerbRegistry.h`, `Source/App/MuseHostVerbs.cpp`

---

## Why this exists

`HostVerbRegistry` gives the application a way to register capabilities into
`MuseService` without inverting the dependency. What it does not do — and cannot
do — is tell you *what the authoritative model behind a verb is*. Nothing in the
type system objects to a handler that writes a field nobody reads.

`view.open`, `view.close` and `view.current` shipped in #624. The obvious next
move is `settings.*`, `browser.*`, `project.*`, `dialog.*` — and the #624 PR
already flagged that `settings.*` needs its model located first, because the
visible settings pages are not necessarily the source of truth.

This document is that survey. For each domain: **authoritative model → owning
thread → mutation API → query API → undoable? → headless-capable?**

Everything below was read out of the tree at `develop` as of 2026-07-27. Where a
claim is "nothing reads this", it is a grep result, and the grep is stated.

---

## Summary table

| Domain | Authoritative model | Owning thread | Undoable | Headless |
|---|---|---|---|---|
| **View** | Split — see below | UI | No | No |
| **Settings** | **No single model.** Three authorities + a partly-dead singleton | UI (device switch blocks) | No | No |
| **Browser** | `FileBrowser` (component-owned) | UI + background scan thread | No | No |
| **Project** | Split: `ProjectDocumentState` (identity) + `TrackManager` (content) + `CommandHistory` (history) | UI | Content yes, identity no | Partly |
| **Dialog** | None — visibility only | UI | No | No |

**Only one domain has an undo model at all.** `TrackManager`'s `CommandHistory`
covers project *content*. View, settings, browser, dialogs and project *identity*
have no undo and no change notification. The `mutates = false` on the shipped
view verbs is therefore correct, and it will be correct for most of what comes
next — which is worth knowing before designing a verb around "and Muse can undo
it".

---

## View

**Authoritative model: split, and the split is currently a bug.**

| View | Authority |
|---|---|
| `mixer`, `pianoRoll`, `arsenal`, `timeline` | `AestraContent::m_viewState.*Open` (intent) |
| `history`, `takes` | `panel->isVisible()` (actual) |

`AestraContent::isViewOpen` reads intent for the first four and actual state for
the last two (`AestraContent.cpp:1919`). Normally the two agree, because
`setViewOpen` writes `m_viewState` and mirrors it into the panel.

They stop agreeing in Audition mode. `setViewFocus` hides the mixer, piano roll
and Arsenal panels **without touching `m_viewState`** (`AestraContent.cpp:2141`),
because `m_viewState` is deliberately the *restore* state — leaving Audition
re-applies it. The consequence for Muse:

> In Audition mode, `view.current` reports `mixer: open` while the mixer is not
> on screen, and `view.open {view: mixer}` returns `changed: false` without
> making anything appear.

That is precisely the failure `view.current` exists to prevent — its own
description says it is there "so an agent can see the workspace instead of
assuming it". `history` and `takes` are unaffected, because they read the panel.

**Recommendation before adding more view verbs:** decide whether `isViewOpen`
answers *intent* or *actual*, and make all six agree. Reading the panel for all
six is the smaller change and matches what an agent means by "is the mixer
showing". `m_viewState` remains the restore state, unread by the query path.
`view.current` should probably also report the current `ViewFocus`, which no
verb exposes today.

- **Owning thread:** UI. `HostThreadAffinity::HostUiThread`, correctly declared.
- **Mutation API:** `AestraContent::setViewOpen / toggleView / setArsenalPanelVisible / setViewFocus`
- **Query API:** `AestraContent::isViewOpen` (and `m_viewFocus`, not exposed)
- **Undoable:** No. Window layout is not project state.
- **Headless:** No. There is no `AestraContent` in `MuseRepl`.

---

## Settings

**There is no settings model.** There are three unrelated authorities and one
singleton that is mostly dead. This is the domain that most needs deciding
before a verb is written.

### 1. Audio device configuration — authority is `AudioDeviceManager`

`AudioSettingsPage` holds no state. Every control calls straight through
(`AudioSettingsPage.cpp:292–351`, `760–777`):

- **Mutation:** `setPreferredDriverType`, `switchDevice`, `switchInputDevice`,
  `setSampleRate`, `setBufferSize` — each returns success/failure, and the
  manager guards against redundant stream reopens by equality check.
- **Query:** `getCurrentConfig`, `getDevices`, `getAvailableDriverTypes`,
  `getActiveDriverType`.

This is the cleanest surface in the whole audit and is ready for verbs as-is.
Note that a device switch reopens the stream and is **not instant** — a verb
must report the real outcome rather than assume it.

### 2. Theme — authority is `NUIThemeManager`, with a persisted copy

`AppearanceSettingsPage::applyChanges` does two things
(`AppearanceSettingsPage.cpp:56`): `NUIThemeManager::setActiveTheme` for the
live change, then `Preferences::theme` + `save()` for persistence. Startup reads
it back at `AestraWindowManager.cpp:156` and falls back to `Aestra-dark` if the
saved name is unknown.

So the theme has a live authority and a persisted authority, and a verb must
write **both** or the change evaporates on restart.

### 3. `Preferences` — a singleton whose fields mostly have no readers

`Source/Core/Preferences.h` looks like the settings model. It is not.
`Preferences::instance()` has **six call sites in the entire tree**, and per-field:

| Field | Readers outside `Preferences.*` |
|---|---|
| `theme` | live (window manager, appearance page) |
| `autoSaveEnabled` | live (`AestraApp.cpp:234`) |
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

### 4. `GeneralSettingsPage` is inert

```cpp
void GeneralSettingsPage::applyChanges() {
    m_dirty = false;
}
```

It applies nothing. `cancelChanges()` resets the projects-path field to a
hardcoded Windows path. Whatever this page appears to configure, it does not.

### Verdict for `settings.*`

Do not register a `settings.*` domain against `Preferences`. Either:

- **(a)** register narrow, honest verbs against the real authorities —
  `settings.audio.*` → `AudioDeviceManager`, `settings.theme` →
  `NUIThemeManager` + `Preferences::save()` — and register nothing for the
  fields that have no authority; or
- **(b)** make `Preferences` real first (give it readers, or delete the dead
  fields) and then register against it.

(a) is shippable now and does not pretend the rest exists. (b) is the larger
correctness cleanup and is worth doing regardless of Muse — dead persisted
settings are a bug with or without an agent.

| | |
|---|---|
| **Owning thread** | UI. Device switches reopen the stream and block. |
| **Undoable** | No. |
| **Headless** | No — but `AudioDeviceManager` itself is reachable from AestraAudio, so a future `settings.audio.*` could in principle be `HostThreadAffinity::Any`. Verify before declaring it. |

---

## Browser

**Authoritative model:** `FileBrowser`, which owns its own state
(`currentPath_`, `filteredFiles_`, the search input). Nothing else holds it.

- **Mutation:** `setCurrentPath`, `navigateTo`, `navigateToBreadcrumb`,
  `refresh`, `setSearchQuery`
- **Query:** `getCurrentPath`; the item list is internal (`filteredFiles_` is
  private and there is no accessor)
- **Undoable:** No. It has its own `navHistory_`, unrelated to `CommandHistory`.
- **Headless:** No.

**The thing to design around:** `FileBrowser` runs a background scan worker
(`FileBrowser.cpp:545`, `scanWorkerLoop`), with results drained on the UI thread
by `processScanResults`. Directory contents are therefore **eventually
consistent**. A `browser.navigate` verb that returns immediately will return
before the listing exists, and a `browser.list` issued straight afterwards will
see a partial or empty directory.

Any `browser.*` verb needs an explicit answer to this: either navigate returns a
completion the agent can wait on, or list reports a scan-in-progress state. It
must not return an empty list that is indistinguishable from an empty directory.

There is also no public accessor for the item list yet, so `browser.list` needs
one added — which is the right moment to decide what a browser entry looks like
on the wire.

---

## Project

**Authoritative model: split three ways, and the split is real rather than
accidental.**

| Aspect | Owner |
|---|---|
| Identity — canonical path, autosave/recovery/snapshot paths, untitled-ness, overwrite protection | `ProjectDocumentState` (`Source/Core/ProjectDocumentState.h`), held by `AestraApp` |
| Content — tracks, lanes, clips, patterns, mixer | `TrackManager` (AestraAudio) |
| History | `TrackManager::getCommandHistory()` |
| Dirty flag | `TrackManager::isModified()` |

Orchestration lives in `AestraApp`: `saveCurrentProject`, `saveProjectAs`,
`saveProject`, `saveProjectToPath`, `loadProjectFromPath`.

- **Owning thread:** UI for the orchestration.
- **Undoable:** content yes, identity no. Saving is not an undoable operation
  and must never enter `CommandHistory`.
- **Headless:** **partly, and this is the one domain where that is true.**
  `TrackManager`, `ProjectSerializer` and `CommandHistory` all live below the UI
  and are compiled into the headless targets. `ProjectDocumentState` is a
  header-only value type in `Source/Core` with no UI dependency.

That makes `project.*` the only domain where a verb could plausibly be
`HostThreadAffinity::Any` and work in `MuseRepl` — which is also what makes it
the most valuable next domain, and the most dangerous to get wrong. A save verb
that runs off the UI thread while the UI thread is editing the model is a data
race, not a capability. `ProjectDocumentState::requiresSaveAs()` is the guard
that stops "save" from silently becoming "save as" — a verb must respect it and
report it, not work around it.

---

## Dialog

**There is no dialog state model.** `AestraWindowManager` owns the instances —
`SettingsDialog`, `ConfirmationDialog`, `RecoveryDialog`, `ExportDialog` — with
set/get accessors and nothing else (`AestraWindowManager.h:82–99`).

- **Mutation / query:** each dialog's own visibility.
- **Undoable:** No.
- **Headless:** No.

The interesting question for `dialog.*` is not how to open one — it is what a
*modal* means to an agent. `ConfirmationDialog` exists to block on a human
answer. A verb that opens it and returns leaves the app waiting on a decision
that Muse then has to make through a second verb, and any agent that fails
mid-flow leaves the app modal and stuck.

**Recommendation:** do not register `dialog.open`. Register the *outcome* the
dialog exists to obtain — `project.saveAs {path}` rather than
`dialog.open {saveAs}` — which is the same principle `HostVerbRegistry.h`
already states about verbs being capabilities rather than gestures. A modal is
the most gesture-shaped thing in the app.

---

## Cross-cutting

**Nothing but the command history has change notification.** `CommandHistory`
has `addOnStateChanged`; `Preferences`, `ViewState`, `FileBrowser` and the
dialogs have none. Any future "what changed since I last looked" capability
needs that built, not discovered.

**Five domains, five different ownership patterns.** They are not five instances
of one shape:

- View — a struct on `AestraContent`, mirrored into panels
- Settings — three unrelated authorities, plus dead persisted fields
- Browser — a component owning its own state behind a worker thread
- Project — a split between identity, content and history
- Dialog — no model, only instances

Verbs should be designed against the actual owner, one domain at a time. A
generic "host state" abstraction over these five would be inventing a uniformity
the application does not have.

---

## Not determined by this audit

- Whether `AudioDeviceManager`'s setters are safe to call from a non-UI thread.
  They are reachable from AestraAudio, which is what makes a headless
  `settings.audio.*` conceivable, but #391/#600 changed its locking and that
  needs reading before anything is declared `HostThreadAffinity::Any`.
- What a browser entry should look like on the wire.
- Whether `ViewFocus` (Arsenal / Timeline / Audition / RoutingMap) should be a
  separate verb or a field on `view.current`.

## Concrete follow-ups this produced

1. `isViewOpen` disagrees with the screen in Audition mode — `view.current` is
   already shipping this. Fix before more view verbs.
2. `Preferences` has seven fields with no readers, and `GeneralSettingsPage`
   applies nothing. A correctness bug in its own right.
3. `FileBrowser` needs a public item-list accessor and a scan-completion signal
   before `browser.*` is possible at all.
