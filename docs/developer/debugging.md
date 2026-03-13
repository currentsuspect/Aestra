# Debugging Guide

This page is the lightweight entry point for debugging Aestra without sending people into a broken self-link loop.

## What this page is for

Use this guide when you need to:

- reproduce a bug cleanly
- gather logs before filing an issue
- figure out whether a failure is UI, project-state, plugin, or audio-engine related
- choose the right deeper reference doc

## Quick triage map

### 1. Project / persistence bugs
Start here when the symptom is:
- save/load mismatch
- autosave/recovery weirdness
- missing clips, patterns, or restored unit/plugin state

Recommended references:
- [Bug Reports Guide](bug-reports.md)
- [Testing & CI](../technical/testing_ci.md)
- [Roadmap](../technical/roadmap.md)

### 2. Internal plugin / Arsenal bugs
Start here when the symptom is:
- plugin not discoverable
- `createInstanceById(...)` fails
- unit loads but has no plugin
- Arsenal playback is silent

Recommended references:
- [Rumble MVP Plan](../technical/RUMBLE_MVP_PLAN.md)
- [Testing & CI](../technical/testing_ci.md)

High-signal self-contained tests:
- `RumbleStateTest`
- `RumblePluginFactoryTest`
- `RumbleUsagePathTest`
- `RumbleDiscoveryTest`
- `ArsenalInstrumentAttachmentTest`
- `InternalPluginProjectRoundTripTest`
- `RumbleRenderTest`
- `RumbleArsenalAudibleTest`

### 3. General audio-engine bugs
Start here when the symptom is:
- silence during playback
- crackles/dropouts
- offline render mismatch
- device init/fallback issues

Recommended references:
- [Performance Tuning](performance-tuning.md)
- [Testing & CI](../technical/testing_ci.md)
- [Threading Model](../technical/THREADING_MODEL.md)

## Practical debugging workflow

1. Reproduce the issue in the smallest possible scenario.
2. Check whether there is already a self-contained test covering that path.
3. If there is, run that first before changing code.
4. If there is not, write the smallest regression test you can.
5. Only then patch the code.
6. Re-run the focused test and one nearby smoke/integration test.

That order matters. It keeps Aestra from becoming a pile of fixes with no proof.

## Logging guidance

Prefer targeted logs over log spam.

Good logging targets:
- project load/save boundaries
- plugin discovery and creation boundaries
- Arsenal unit attach/load/enable boundaries
- pattern playback scheduling and routing edges

Avoid:
- per-sample logs
- noisy RT-thread logs in hot paths
- broad debug spam that makes real failures harder to see

## Before filing or escalating a bug

Capture:
- commit / branch
- build type
- exact repro steps
- expected result vs actual result
- whether a self-contained test exists
- whether the issue reproduces in headless/tests or only in UI/runtime flows

Then use:
- [Bug Reports Guide](bug-reports.md)

## Related references

- [Bug Reports Guide](bug-reports.md)
- [Performance Tuning](performance-tuning.md)
- [Testing & CI](../technical/testing_ci.md)
- [Rumble MVP Plan](../technical/RUMBLE_MVP_PLAN.md)
- [Roadmap](../technical/roadmap.md)
