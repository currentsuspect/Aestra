# Nomad V1 Beta Roadmap: The Path to Convergence
**Status:** ACTIVE
**Timeline:** Early 2026 — December 2026
**Target:** Windows-only Public Beta
**Author:** Bolt (Strategy Agent)

---

## 1. Executive Summary

Nomad is no longer in the "exploration" phase. It is in the **convergence** phase.
We have exactly **one year** (12 months) to ship a credible, stable, and distinctive V1 Beta.

**The Goal:** A "boringly correct" professional DAW core with a focused, high-performance workflow.
**The Constraint:** Windows 10/11 only. No cross-platform distractions. No monetization logic.
**The Method:** Ruthless elimination of technical debt and "finishing" of core systems.

---

## 2. Current State Assessment (Brutal & Honest)

### ✅ Production-Grade (Keep & Protect)
*   **Audio Engine Core (`NomadAudio`):** The lock-free architecture, ring buffers (`LockFreeRingBuffer`), and WASAPI integration are solid. The separation of the real-time thread from the UI thread is conceptually correct.
*   **Logging & Profiling:** The `NomadLog` and `NomadUnifiedProfiler` systems are in place and useful.
*   **Visual Aesthetics:** The UI rendering framework (`NomadUI`) is performant and looks good, though the architecture needs tightening.

### 🚧 Experimental / Incomplete (The Danger Zone)
*   **The Monolith (`Main.cpp`):** **CRITICAL RISK.** 1,500+ lines of initialization, event handling, and UI glue code. This violates every principle of maintainability and makes adding features like Undo/Redo or detailed plugin management impossible.
*   **VST3 Hosting (`VST3Host.cpp`):** The code exists, but it is **not integrated**. It is currently a side-car module. It handles loading but lacks robust state management, reliable automation hooks, and deep integration into the `AudioGraph`. It is currently a prototype, not a production system.
*   **Project Persistence:** We have a basic `ProjectSerializer`, but it is not robust. It lacks versioning, comprehensive state capture (especially plugin state chunks), and "safe save" mechanisms.

### 🛑 Missing but Critical (Must Build)
*   **Undo/Redo System:** Non-existent. A DAW without Undo is a toy. This requires a Command Pattern implementation that touches almost every action in the UI.
*   **Drag & Drop:** The UI lacks native-feeling drag-and-drop for samples and plugins.
*   **Automation Lanes:** The data structures for automation exist in theory (`AutomationCurve`), but the UI and engine integration for recording and playing back parameter changes is missing.

---

## 3. Defining "Nomad V1 Beta"

**The "V1 Beta" is not feature parity with Ableton Live. It is a stable, usable musical instrument.**

### Platform
*   **OS:** Windows 10 / 11 (64-bit) ONLY.
*   **Audio:** WASAPI (Primary), ASIO (Secondary/Beta).

### Supported Workflows (The "Happy Path")
1.  **Creation:** Drag sample to timeline -> Slice -> Arrange.
2.  **Sound Design:** Load VST3 Synth -> Add VST3 FX -> Automate Cutoff.
3.  **Structure:** Group Tracks -> Mix -> Export to WAV.
4.  **Reliability:** Save Project -> Crash -> Reload -> Project is intact.

### Explicit Scope

| Feature | Status | Notes |
| :--- | :--- | :--- |
| **VST3 Support** | **IN** | Must be "Boringly Correct". Load, Save State, Automate, UI. |
| **Audio Recording** | **IN** | Basic mono/stereo recording with monitoring. |
| **Undo/Redo** | **IN** | Infinite stack, covers all destructive actions. |
| **Project Save** | **IN** | Robust JSON format + VST3 opaque data chunks. |
| **macOS / Linux** | **OUT** | Post-Beta Phase 1. |
| **Muse / DRM** | **OUT** | No keys, no activation, no paywalls. |
| **Video Support** | **OUT** | Distraction. |
| **Scripting / API** | **OUT** | Internal use only. |

---

## 4. Phased Roadmap (2026)

**Rule:** You do not move to the next phase until the current phase is **stable**.

### Phase 1: The Foundation (Q1: Jan - Mar)
**Theme:** "Kill the Monolith"
1.  **Refactor `Main.cpp`:** Split into `AppController`, `WindowDelegate`, `AudioController`, `ProjectManager`.
2.  **Plugin Lifecycle Integration:** Move `VST3Host` from "prototype" to "core". Ensure plugins scan, load, and process audio within the main `AudioEngine` graph correctly.
3.  **Command Infrastructure:** Build the `CommandManager` (Undo/Redo backend).

### Phase 2: Core Data & Integrity (Q2: Apr - Jun)
**Theme:** "Data is Sacred"
1.  **Project Serialization:** Implement the full JSON schema. Ensure it saves: Track settings, Mixer state, Plugin States (Chunk data), Automation curves.
2.  **Undo/Redo Implementation:** Wire up the UI to the `CommandManager`. Every knob twist, every move, every delete must be undoable.
3.  **Auto-Save:** Implement background auto-save to prevent data loss.

### Phase 3: The Plugin Reality (Q3: Jul - Sep)
**Theme:** "Plays Well With Others"
1.  **VST3 Automation:** Connect UI knobs -> Engine Parameters -> VST3 Parameters.
2.  **Plugin UI Embedding:** ensure plugin windows open, close, and resize without crashing the renderer or stealing keyboard focus incorrectly.
3.  **Latency Compensation (PDC):** Ensure mixing phase-coherent audio when using heavy plugins.

### Phase 4: UX Hardening & Polish (Q4: Oct - Dec)
**Theme:** "Feels Like an Instrument"
1.  **Drag & Drop:** smooth drag from browser to timeline, plugin to track.
2.  **Optimization:** Profile `NomadUI` rendering. Ensure 60fps steady state.
3.  **Crashproofing:** Stress test audio engine. Fuzz test project loader.
4.  **Beta Freeze (Dec 1):** Code freeze. Only critical bug fixes.

---

## 5. Industry Reality Check

**Competitor Analysis:**
*   **Ableton:** Wins on "Live" performance and Session View. *Nomad should not compete here yet.*
*   **Reaper:** Wins on infinite customization and efficiency. *Nomad wins on UX and "freshness".*
*   **Bitwig:** Wins on modulation. *Nomad wins on simplicity and focus.*

**Nomad's Position:**
Nomad V1 Beta is for the producer who wants a **modern, lightweight, no-nonsense DAW**. It competes by being faster to open, easier to look at, and less cluttered than 20-year-old legacy codebases. It positions itself as **"The Editor's DAW"**—precise, fast, clean.

---

## 6. Hard Truths & Engineering Directives

1.  **Stop Starting New Things.** If it isn't in the Phase list above, it doesn't exist. No new experimental synths. No new experimental UI paradigms.
2.  **Plugins Are Not Optional.** You cannot "fake" VST3 support. If it crashes with Serum or FabFilter, Nomad is broken.
3.  **Architecture First.** You cannot build Undo/Redo on top of a spaghetti `Main.cpp`. The refactor is the prerequisite for the product.
4.  **Windows First.** Ignore Linux compilation errors for now. Ignore macOS APIs. Focus 100% of your energy on making the Windows experience flawless.

**Commit to this one year of focused execution, and Nomad will launch as a serious contender.**
