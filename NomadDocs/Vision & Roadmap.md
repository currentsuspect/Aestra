# Nomad DAW — Vision & Roadmap 2026

**Target date:** **Jan 1, 2026** (00:00 UTC)
**Release:** v1.0 “Nomad Core”

## Vision (1-liner)

A fast, minimal, creator-first DAW that feels snappy on low-spec machines and scales up gracefully.

---

## v1.0 Goals (Definition of Done)

1. **Official Release v1**

* Public build + signed installer; versioned `v1.0.0`.
* Website/download page + README + changelog + EULA.
* Crash-reporting & metrics opt-in.

2. **Core Feature Set**

* **Playlist/Arranger**, **Mixer**, **Piano Roll**, **Sequencer** (pattern/clip), plus **essential stock plugins** (EQ, Compressor, Reverb, Limiter, Utility Gain/Pan, Tuner).
* Import/export: WAV/MP3 (via codec), MIDI in/out.
* Preset saving/loading for plugins.

3. **30 FPS Stable UI**

* Median FPS ≥ 30; p95 ≥ 28 during standard workflows (edit, scroll, zoom, drag, open settings).
* GPU/CPU frame times logged; no long >150 ms stalls.

4. **No Memory Leaks**

* Leak check clean on exit (AddressSanitizer or CRT leak checker).
* p95 memory growth < 3% over 30-minute heavy editing session (load/save, undo/redo, open/close plugins).

5. **“Close to zero” Bugs & Crashes**

* **Crash-free sessions ≥ 99.5%** in pre-release cohort.
* **Open critical bugs: 0; High: ≤ 3;** all with workarounds; **Medium/Low:** triaged to v1.x.

6. **Demo Project**

* Ships in installer; exercises playlist, mixer, piano roll, sequencer, and all stock plugins; renders glitch-free.
* Tutorial notes/markers included.

7. **Sub-10 ms Latency (consistent)**

* With supported devices: **WASAPI Exclusive** or **ASIO**, at 44.1/48 kHz, 128–256 frames: **round-trip ≤ 10 ms**, measured via loopback; no periodic underruns over 10-minute play.

---

## Scope Boundaries (Non-Goals for v1)

* No third-party VST hosting (v1.x).
* No elastic audio/time-stretch (basic resample only).
* No network collaboration/cloud.

---

## Milestones & Schedule (high level)

**M1 – Feature Freeze (Nov 15, 2025)**

* All core features in place; only bug fixes/perf from here.

**M2 – Performance & Stability (Nov 16–Dec 7)**

* Target FPS, memory, and latency objectives; CI perf gates added.

**M3 – Release Candidate 1 (Dec 8)**

* RC1 build; start external test cohort; crash/telemetry on.

**M4 – Bug Burn-down (Dec 9–Dec 21)**

* Drive Critical/High to targets; RC2 if needed.

**M5 – Content & Docs (Dec 15–Dec 24)**

* Demo project final; quickstart, tooltips, release notes.

**M6 – Code Freeze & Sign (Dec 26)**

* Only release-blocking fixes allowed; signed installer produced.

**Launch – Jan 1, 2026**

* v1.0 publish; website + announcement.

---

## Workstreams & Leads

* **Audio Engine & Latency:** driver switching, buffer strategy, limiter/ramp (Owner: Audio lead)
* **UI/Rendering:** FPS, virtualization, scissoring, hit-testing (Owner: UI lead)
* **Mixer/Piano Roll/Sequencer:** correctness & UX polish (Owner: Feature lead)
* **Perf/Memory/QA:** benchmarks, ASan/ubsan, soak tests (Owner: QA lead)
* **Install/Docs/Demo:** content, signing, release packaging (Owner: Release mgr)

---

## Acceptance Tests (per goal)

**G3 – 30 FPS**

* Record 10 scripted sessions (5 min each). Pass if median ≥30, p95 ≥28; no >150 ms stalls count >3 per session.

**G4 – Leaks**

* ASan build: 0 leak reports after scripted session + exit.
* Release build soak (30 min): RSS growth p95 <3%.

**G5 – Stability**

* Crash-free rate ≥99.5% across ≥200 cumulative test hours.
* All Critical=0; High≤3 with workarounds documented.

**G7 – Latency**

* Loopback test at 44.1/48 kHz, 128–256 frames: RTL ≤10 ms for 3 different devices (USB, onboard, ASIO interface).

---

## CI/CD Gates

* Build: Windows Release/Debug + ASan; unit & integration tests green.
* Perf jobs fail if FPS/latency/memory thresholds regress.
* Static analysis (clang-tidy, /analyze) zero new High findings.
* Artifact: signed installer + symbols + demo project.

---

## Risk Register (top 6) & Mitigations

1. **Exclusive mode distortion when device is busy** → Detect busy, auto-fallback to Shared with ramp; block UI toggle.
2. **Expensive UI paths drop FPS** → Virtualize lists, throttle layout, cache text, GPU scissor.
3. **Hidden leaks in plugin graph** → Strict ownership audits; ASan nightly; fuzz load/unload.
4. **Latency varies across drivers** → Provide ASIO path; presets per device; underrun-safe limiter.
5. **Schedule compression in December** → Feature freeze by Nov 15; code freeze Dec 26; cut non-essentials.
6. **Installer/signing delays** → Dry-run signing by Dec 15; backup cert flow.

---

## Launch Checklist

* [ ] RC build signed; version stamped.
* [ ] Demo project loads & renders; tutorial shown on first run.
* [ ] Crash reporter & telemetry opt-in prompt.
* [ ] Website page live (download, quickstart, system reqs, known issues).
* [ ] Press/announcement post drafted.

---

## Post-Launch (v1.0.1–v1.2 themes)

* VST3 hosting, MIDI learn, Render Queue, Keymap editor, Project autosave, Improved metering, Theme system.
