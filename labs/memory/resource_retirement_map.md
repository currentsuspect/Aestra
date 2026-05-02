# Resource Retirement Map

Aestra's `GarbageCollector` is deferred destruction / RCU-style reclamation for resources that are visible to the audio
thread through atomic or snapshot-style publication. It is not a tracing garbage collector and does not discover objects
at runtime. Non-real-time code explicitly retires old `std::shared_ptr` resources, and a non-real-time collector pass
destroys them after the audio side has dropped its references.

## Thread Rules

- The audio thread may hold references to already-published resources.
- Non-real-time threads may retire old resources with `GarbageCollector::release()`.
- A collector, idle, shutdown, or background non-real-time thread performs destruction with `collect()` or
  `drainUntilStable()`.
- `release()`, `collect()`, `drainUntilStable()`, `zombieCount()`, and `stats()` are non-real-time only.
- Audio `process()` paths must not call GC APIs, allocate for GC labels, log GC diagnostics, or wait for reclamation.

## Current Call-Site Audit

| File | Function | Likely context | RT-reachable? | Action |
|---|---|---:|---:|---|
| `AestraAudio/src/Plugin/SamplerPlugin.cpp` | `SamplerPlugin::shutdown()` | Plugin shutdown / non-RT lifecycle | No | Label retired `SampleData` and rely on RT guard |
| `AestraAudio/src/Plugin/SamplerPlugin.cpp` | `SamplerPlugin::loadSample()` | UI/loading/sample import | No | Label retired `SampleData` and rely on RT guard |
| `AestraAudio/src/Plugin/SamplerPlugin.cpp` | `SamplerPlugin::normalizeSample()` | UI/editor sample edit | No | Label retired `SampleData` and rely on RT guard |
| `AestraAudio/src/Plugin/SamplerPlugin.cpp` | `SamplerPlugin::reverseSample()` | UI/editor sample edit | No | Label retired `SampleData` and rely on RT guard |
| `Tests/AestraAudio/GarbageCollectorTest.cpp` | test helpers | Unit tests | Simulated only | Covers normal and marked-RT misuse paths |

No production `collect()`, `drainUntilStable()`, `stats()`, or `zombieCount()` call sites were found outside tests during
this pass.

## Resource Status

| Resource | Status | Notes |
|---|---|---|
| Sampler sample buffers | Already GC-safe | Current `SampleData` swaps retire old buffers through GC from non-RT code. |
| Plugin graph snapshots | Needs deferred destruction later | Snapshot publication can leave the audio thread holding old graph state. |
| Effect chain / plugin instances | Dangerous/unknown | Plugin destructors may be expensive or host-dependent; defer only after ownership model audit. |
| Waveform cache data | Needs deferred destruction later | Cache entries can be large and audio/UI-visible, but current ownership should be audited first. |
| Decoded browser preview audio | Needs deferred destruction later | Preview buffers may become audio-visible and should avoid destruction on callback paths. |
| Frozen/rendered audio assets | Needs deferred destruction later | Large immutable assets are good candidates once publication ownership is explicit. |
| Export/render graph resources | Does not need GC today | Offline export is non-RT; use GC only if resources become shared with live playback snapshots. |
| Automation snapshot/state objects | Dangerous/unknown | Needs a snapshot ownership audit before adoption. |

## Follow-Ups

- Keep every new GC caller in this map with a thread-context note.
- Prefer immutable shared resources plus atomic/snapshot publication before adopting GC.
- Add short-run CI coverage for RT misuse guards if the test harness grows death-test support.
- Audit plugin graph and effect chain lifetimes before using GC for plugin instances.
