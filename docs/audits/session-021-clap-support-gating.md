# Session 021: CLAP Support Gating and Experimental Labeling

**Branch:** `codex/session-021-clap-support-gating` (from `develop`)
**Starting SHA:** `2a1c69e5`
**Date:** 2026-04-29
**Working tree:** Clean at start

---

## Summary

Added honest experimental labeling to CLAP plugin support. CLAP plugins now display "CLAP (Exp.)" in the plugin browser, and loading/saving CLAP state produces clear log warnings. No CLAP source files were removed; the code compiles and scans as before.

---

## Current CLAP Exposure Path (Before Fix)

| Stage | Behavior |
|-------|----------|
| **Scanning** | `.clap` files scanned from default search paths, metadata extracted |
| **Browser display** | Shown as "CLAP" with no indication of incomplete support |
| **Loading** | `CLAPPluginFactory::createInstance()` instantiates plugins |
| **MIDI input** | `process.in_events = nullptr` — events silently dropped |
| **State save** | `saveState()` returns empty `{}` — silently loses state |
| **State load** | `loadState()` returns `false` — silently fails |
| **Host callbacks** | `requestRestart`, `requestProcess`, `requestCallback` — all no-ops |
| **Parameter rescan/flush** | Stubs |

**Problem:** Users could load CLAP instruments, write MIDI to them, save the project, reload, and find all CLAP plugin state gone with no warning.

---

## Chosen Policy

**Experimental label** — CLAP plugins remain scannable and loadable, but are clearly marked as experimental in all user-facing surfaces. Diagnostic warnings are logged when incomplete features are exercised.

Rationale: CLAP audio processing works. Gating behind a flag or removing would break users who have CLAP effects that don't need MIDI or state. The label approach is the smallest honest change.

---

## Changes Made

### Plugin Browser — Experimental Label

| File | Change |
|------|--------|
| `Source/Core/AestraContent.cpp:375` | `"CLAP"` → `"CLAP (Exp.)"` (scan complete callback) |
| `Source/Core/AestraContent.cpp:3047` | `"CLAP"` → `"CLAP (Exp.)"` (refresh plugin list) |
| `AestraUI/Widgets/PluginUIController.cpp:459` | `"CLAP"` → `"CLAP (Exp.)"` (plugin UI controller) |
| `AestraUI/Widgets/PluginBrowserPanel.cpp:200` | Badge width adjusted for longer label (30→48 for CLAP) |
| `AestraUI/Widgets/PluginBrowserPanel.cpp:421` | Filter uses `find("CLAP")` instead of `== "CLAP"` |
| `AestraUI/Widgets/PluginBrowserPanel.h:28` | Comment updated |

### CLAP Host — Diagnostic Warnings

| File | Change |
|------|--------|
| `AestraAudio/src/Plugin/CLAPHost.cpp:244` | Warning logged on plugin load: "MIDI, state save/load, host callbacks not fully implemented" |
| `AestraAudio/src/Plugin/CLAPHost.cpp:431` | `saveState()` logs warning before returning empty |
| `AestraAudio/src/Plugin/CLAPHost.cpp:436` | `loadState()` logs warning before returning false |
| `AestraAudio/src/Plugin/CLAPHost.cpp:354` | Comment updated: "MIDI not implemented — events silently dropped" |

---

## Remaining CLAP Implementation Gaps

| Gap | Impact | Severity |
|-----|--------|----------|
| No MIDI event input | CLAP instruments cannot receive MIDI notes | High |
| No state save/load | Plugin settings lost on project reload | High |
| `requestRestart` no-op | Plugin-initiated restart ignored | Medium |
| `requestProcess` no-op | Plugin-initiated process request ignored | Medium |
| `request_callback` no-op | Plugin main-thread callbacks dropped | Medium |
| Parameter rescan/flush stubs | Dynamic parameter changes not reflected | Low |
| No transport info | `process.transport = nullptr` | Low |

---

## Tests Run

| Test | Result |
|------|--------|
| CommandHistoryTest | 15/15 passed |
| MixerCommandsTest | 17/17 passed |
| ClipCommandsTest | 8/8 passed |
| ArsenalBridgeContractTest | PASS |
| ArsenalExportLiveParityTest | PASS |

**Build:** `AestraAudioCore` and `AestraHeadless` compile clean.

---

## Files Changed

| File | Change |
|------|--------|
| `Source/Core/AestraContent.cpp` | "CLAP" → "CLAP (Exp.)" in 2 locations |
| `AestraUI/Widgets/PluginUIController.cpp` | "CLAP" → "CLAP (Exp.)" |
| `AestraUI/Widgets/PluginBrowserPanel.cpp` | Badge width, filter logic |
| `AestraUI/Widgets/PluginBrowserPanel.h` | Comment update |
| `AestraAudio/src/Plugin/CLAPHost.cpp` | Diagnostic warnings, comment updates |
| `docs/audits/session-021-clap-support-gating.md` | Session report |

---

## Final State

- **Branch:** `codex/session-021-clap-support-gating`
- **SHA:** `2a1c69e5` (starting)
- **Files changed:** 6
- **Working tree:** Clean after commit
