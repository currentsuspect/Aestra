# Aestra Documentation Index

**Quick navigation for all Aestra internal documentation.**
Public/site docs live in `../docs/` and are not duplicated here.

## Quick Start
- [Developer Guide](guides/DEVELOPER_GUIDE.md) — onboarding and philosophy
- [Build Status](status/BUILD_STATUS.md) — current build state
- [Branching Strategy](status/BRANCHING_STRATEGY.md) — git workflow
- Root: [`../README.md`](../README.md) · [`../BUILD.md`](../BUILD.md) · [`../CONTRIBUTING.md`](../CONTRIBUTING.md) · [`../AGENTS.md`](../AGENTS.md)

## Architecture
- [**ARCHITECTURE_AUDIT_2026Q2**](architecture/ARCHITECTURE_AUDIT_2026Q2.md) — cross-module audit, prioritized findings
- [**Arsenal Architecture**](architecture/Arsenal-Architecture.md) — route/bridge semantics, processing context, export parity
- [Adaptive FPS Architecture](architecture/ADAPTIVE_FPS_ARCHITECTURE.md)
- [AestraUI Coordinate System](architecture/AESTRAUI_COORDINATE_SYSTEM.md) — **critical** for UI work
- [Aestra Mode Implementation](architecture/AESTRA_MODE_IMPLEMENTATION.md)
- [Dropdown Architecture](architecture/DROPDOWN_ARCHITECTURE.md)
- [Text Alignment Analysis](architecture/Text_Alignment_Analysis_and_Fixes.md)

## Systems
- [Audio Driver System](systems/AUDIO_DRIVER_SYSTEM.md)
- [Audio Timing Quality](systems/AUDIO_TIMING_QUALITY.md)
- [Adaptive FPS Performance Diagnostic](systems/ADAPTIVE_FPS_PERFORMANCE_DIAGNOSTIC.md)
- [Custom Window Integration](systems/CUSTOM_WINDOW_INTEGRATION.md)
- [Dropdown System v2.0](systems/DROPDOWN_SYSTEM_V2.0.md)

## Guides
- [Developer Guide](guides/DEVELOPER_GUIDE.md)
- [UI Layout Guide](guides/UI_LAYOUT_GUIDE.md)
- [OpenGL Linking Guide](guides/OPENGL_LINKING_GUIDE.md)
- [Adaptive FPS Guide](guides/ADAPTIVE_FPS_GUIDE.md) · [Quick Ref](guides/ADAPTIVE_FPS_README.md)
- [Coordinate Utilities v1.1](guides/COORDINATE_UTILITIES_V1.1.md)
- [Dropdown Quick Reference](guides/DROPDOWN_QUICK_REFERENCE.md)
- [MSDF Text Rendering Analysis](guides/MSDF_Text_Rendering_Analysis.md) · [Implementation Summary](guides/MSDF_Fixes_Implementation_Summary.md)
- [Documentation Polish v1.1](guides/DOCUMENTATION_POLISH_V1.1.md)

## Implementation plans
- [Audio Quality — Master Plan](implementation/audio_quality_master_plan.md) · [Executive Summary](implementation/audio_quality_executive_summary.md)
- [PDC Implementation Plan](implementation/pdc_implementation_plan.md) — see also [PDC v2 Design](PDC-v2-Design.md)
- [True-Peak Implementation Plan](implementation/true_peak_implementation_plan.md)
- [`implementation/README.md`](implementation/README.md)

## Design specs (system-level, in this directory)
- [PDC v2 Design](PDC-v2-Design.md) — graph-aware plugin delay compensation
- [Mixer Deep Undo](mixer-deep-undo.md)
- [Piano Roll ↔ Arsenal Sync](piano-roll-arsenal-sync.md)
- [Transport-Aware Preview Ducking](transport-aware-preview-ducking.md)
- [Undo/Redo Trust Model](undo-redo-trust.md)
- [Unbounded Timeline](UNBOUNDED_TIMELINE.md)
- [Sinc-64 Optimization Tasks](sinc64-optimization-tasks.md)
- [Routing Implementation Checklist](Routing-Implementation-Checklist.md)

## Aestra-Comp v2 (effort-tracked)
- [Aestra-Comp v2 README](aestra-comp-v2/README.md)
- [Phase 0 — Foundation](aestra-comp-v2/phase-0-foundation.md)
- [Phase 1 — Core DSP](aestra-comp-v2/phase-1-core-dsp.md)
- Prompts: [Codex Phase 0](aestra-comp-v2/codex-phase-0-prompt.md) · [Codex Phase 1](aestra-comp-v2/codex-phase-1-prompt.md)

## Product & strategy (internal)
- [Product Strategy](Product-Strategy.md)
- [Product Roadmap](Roadmap-Product.md)
- [Pricing & Card System](Pricing.md)
- [Muse AI Spec](Muse-AI-Spec.md)
- [Design Language](Design-Language.md)
- [Design System](DESIGN_SYSTEM.md)

## Project status
- [Aestra Project Analysis](status/AESTRA_PROJECT_ANALYSIS.md)
- [Current State Analysis](status/CURRENT_STATE_ANALYSIS.md)
- [Build Status](status/BUILD_STATUS.md)
- [Documentation Status](status/DOCUMENTATION_STATUS.md)
- [Branching Strategy](status/BRANCHING_STRATEGY.md)
- [Screenshot Limitation](status/SCREENSHOT_LIMITATION.md)

## Bug reports
- [`Bug Reports/`](Bug%20Reports/) — postmortems and triage notes

## Refactor planning
- [Refactor Plan 2026](Refactor%20Plan%202026.md)

## Module READMEs
- [`../AestraCore/README.md`](../AestraCore/README.md)
- [`../AestraPlat/README.md`](../AestraPlat/README.md) · [DPI Support](../AestraPlat/docs/DPI_SUPPORT.md)
- [`../AestraAudio/README.md`](../AestraAudio/README.md) · [Quick Reference](../AestraAudio/QUICK_REFERENCE.md) · [RtAudio Integration](../AestraAudio/RTAUDIO_INTEGRATION.md)
- [`../AestraUI/README.md`](../AestraUI/README.md)

## Labs (research / benchmarks)
Each lab under `../labs/<topic>/` has a `program.md`, `LAB_BOOK.md`, `EVALS.md`, `findings/`, and `sessions/`. Benchmark `results/` outputs are gitignored.

---

**Conventions**
- `architecture/` — design rationale and invariants
- `systems/` — implementation-level descriptions of subsystems
- `guides/` — how-to and reference
- `implementation/` — active implementation plans
- `status/` — point-in-time project status
- Root of `AestraDocs/` — design specs and product/strategy
