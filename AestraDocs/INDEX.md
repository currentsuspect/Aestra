# Aestra Documentation Index

**The map of the internal docs vault.** This is the single front door — start here.
Public/site docs live in [`../docs/`](../docs/) (mkdocs) and are not duplicated here.

> Vault layout: every content doc lives in a topic folder. The vault **root**
> is reserved for the six design specs that source code cites by path, plus the
> `README.md` and `INDEX.md` navigation files (see [Root specs](#root-specs-cited-by-source)
> — do not move cited specs without updating the citing `.cpp/.h/CMakeLists`).

## Quick start
- [Developer Guide](guides/DEVELOPER_GUIDE.md) — onboarding and philosophy
- [Build Status](status/BUILD_STATUS.md) — current build state
- [Branching Strategy](status/BRANCHING_STRATEGY.md) — git workflow
- [Beta Gameplan (2026-07)](product/beta-gameplan-2026-07.md) — the "never steer" plan
- Root: [`../README.md`](../README.md) · [`../BUILD.md`](../BUILD.md) · [`../CONTRIBUTING.md`](../CONTRIBUTING.md) · [`../AGENTS.md`](../AGENTS.md) · [`../philosophy.md`](../philosophy.md)

## Root specs (cited by source)
These stay at the vault root because `.cpp/.h/CMakeLists.txt` reference them by
path. Moving them breaks those citations.
- [PDC v2 Design](PDC-v2-Design.md) — graph-aware plugin delay compensation
- [Audio Research Bench](audio-research-bench.md) — resampler/DSP measurement log
- [Audio Integrity Infrastructure](audio-integrity-infrastructure.md) — export/RT parity test policy
- [Clip Prefilter Lifecycle](clip-prefilter-lifecycle.md) — anti-alias clip prefilter design
- [RT Safety Audit](rt-safety-audit.md) — real-time allocation/lock findings
- [UI Type & Space Grammar](ui-type-space-grammar.md) — type/radius/spacing scale

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
- [Optimization Notes](systems/OPTIMIZE.md)

## Design specs (system-level)
Behavioral/interaction specs for subsystems.
- [Mixer Deep Undo](specs/mixer-deep-undo.md)
- [Undo/Redo Trust Model](specs/undo-redo-trust.md)
- [Piano Roll ↔ Arsenal Sync](specs/piano-roll-arsenal-sync.md)
- [Transport-Aware Preview Ducking](specs/transport-aware-preview-ducking.md)
- [Unbounded Timeline](specs/UNBOUNDED_TIMELINE.md)
- [Routing Implementation Checklist](specs/Routing-Implementation-Checklist.md)

## Audio quality
- [Path to All-A](audio/Path-to-All-A.md) — audio-quality grade roadmap
- [Audio Quality Validation Spec v1](audio/audio-quality-validation-spec-v1.md)
- [Audio Quality Audit — 2026-05](audio/audio-quality-audit-2026-05.md) · [2026-06](audio/audio-quality-audit-2026-06.md)
- [Sinc-64 Optimization Tasks](audio/sinc64-optimization-tasks.md)

## UI & visual design
- [Design Language](design/Design-Language.md)
- [Design System](design/DESIGN_SYSTEM.md)
- [UI Pass — 2026-05 Plan](design/UI_Pass_2026-05_Plan.md)
- See also root spec: [UI Type & Space Grammar](ui-type-space-grammar.md)

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
- [PDC Implementation Plan](implementation/pdc_implementation_plan.md) — see also root spec [PDC v2 Design](PDC-v2-Design.md)
- [True-Peak Implementation Plan](implementation/true_peak_implementation_plan.md)
- [Refactor Plan 2026](implementation/Refactor-Plan-2026.md)
- [`implementation/README.md`](implementation/README.md)

## Aestra-Comp v2 (effort-tracked)
- [Aestra-Comp v2 README](aestra-comp-v2/README.md)
- [Phase 0 — Foundation](aestra-comp-v2/phase-0-foundation.md)
- [Phase 1 — Core DSP](aestra-comp-v2/phase-1-core-dsp.md)
- Prompts: [Codex Phase 0](aestra-comp-v2/codex-phase-0-prompt.md) · [Codex Phase 1](aestra-comp-v2/codex-phase-1-prompt.md)

## Product & strategy (internal)
- [Product Strategy](product/Product-Strategy.md)
- [Product Roadmap](product/Roadmap-Product.md)
- [Pricing & Card System](product/Pricing.md)
- [Muse AI Spec](product/Muse-AI-Spec.md)
- [Beta Gameplan (2026-07)](product/beta-gameplan-2026-07.md)

## Security (internal)
- [License Verification Architecture Audit](security/license-verification-audit.md) — **sensitive**: documents a known bypass; keep internal

## Project status
- [Aestra Project Analysis](status/AESTRA_PROJECT_ANALYSIS.md)
- [Current State Analysis](status/CURRENT_STATE_ANALYSIS.md)
- [Build Status](status/BUILD_STATUS.md)
- [Documentation Status](status/DOCUMENTATION_STATUS.md)
- [Branching Strategy](status/BRANCHING_STRATEGY.md)
- [Issue Governance Policy](status/issue-governance-policy.md)
- [Weekly Health — 2026-W31](status/Weekly-Health-2026-W31.md) — expires end of week; durable contract is `docs/technical/engineering-health.md`
- [Screenshot Limitation](status/SCREENSHOT_LIMITATION.md)

## Bug reports
- [`Bug Reports/`](Bug%20Reports/) — postmortems and triage notes

## Module READMEs
- [`../AestraCore/README.md`](../AestraCore/README.md)
- [`../AestraPlat/README.md`](../AestraPlat/README.md) · [DPI Support](../AestraPlat/docs/DPI_SUPPORT.md)
- [`../AestraAudio/README.md`](../AestraAudio/README.md) · [Quick Reference](../AestraAudio/QUICK_REFERENCE.md) · [RtAudio Integration](../AestraAudio/RTAUDIO_INTEGRATION.md)
- [`../AestraUI/README.md`](../AestraUI/README.md)

## Labs (research / benchmarks)
Each lab under `../labs/<topic>/` has a `program.md`, `LAB_BOOK.md`, `EVALS.md`, `findings/`, and `sessions/`. Benchmark `results/` outputs are gitignored.

---

**Folder conventions**

| Folder | Holds |
| ------ | ----- |
| _(root)_ | The six design specs cited by source code, plus `README.md` and `INDEX.md` navigation |
| `architecture/` | Design rationale and invariants |
| `systems/` | Implementation-level descriptions of subsystems |
| `specs/` | System-level behavioral/interaction design specs |
| `audio/` | Audio-quality audits, validation specs, DSP task lists |
| `design/` | Visual / UI design language and pass plans |
| `guides/` | How-to and reference |
| `implementation/` | Active implementation plans |
| `product/` | Product strategy, pricing, roadmap, planning |
| `security/` | Security audits (internal, sensitive) |
| `status/` | Point-in-time project status and process governance |
| `aestra-comp-v2/` | Effort-tracked plugin build |
| `Bug Reports/` | Postmortems and triage notes |
| `images/` | Diagram / screenshot assets |
