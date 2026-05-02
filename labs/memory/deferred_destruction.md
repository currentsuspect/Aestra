# Deferred Destruction in Aestra

Aestra uses deferred destruction for resources that can be published to the real-time audio thread through atomic
`std::shared_ptr` swaps. The audio callback may briefly hold an older resource after UI, loading, or shutdown code has
already installed a replacement. Destroying the old resource immediately on the writer thread can be safe, but only when
the audio side has definitely dropped its reference. The `GarbageCollector` centralizes that wait.

This is RCU-style resource reclamation, not a tracing garbage collector. It retains retired `shared_ptr` instances and
collects them once `use_count() == 1`, meaning only the collector still owns the object.

Threading rules:

- `release()` is non-real-time only. It may use a mutex and may allocate if the bounded incoming queue overflows.
- `collect()` is non-real-time only. It owns the zombie list and may run destructors.
- Object destruction happens on the thread that calls `collect()`.
- The audio callback must never call `release()`, `collect()`, diagnostics, or shutdown drains.
- Debug builds can mark the audio callback thread and report/assert if GC APIs are called from that context.
- Audio code should only publish/load the shared resource and keep processing free of locks, allocation, logging, and I/O.
- Engine-published meter snapshots, continuous params, and channel-slot maps are retired through GC from non-RT
  setters so their previous shared buffers do not disappear during an in-flight audio block.

Production cadence:

- `AudioEngine::performNonRealtimeMaintenance()` is the normal periodic hook.
- It is called from the GUI app loop, headless render loop, and offline export loop.
- It is throttled internally to roughly twice per second so frequent UI ticks do not waste work.
- `AudioEngine::drainDeferredResourcesForShutdown()` is the explicit shutdown drain and runs after the audio stream is closed.

Current use:

- Sampler sample buffers are atomically replaced, and old `SampleData` objects are retired through `GarbageCollector`.
- See `resource_retirement_map.md` for the current call-site audit and candidate future resources.

Likely future uses:

- decoded sample assets
- render graph snapshots
- plugin graph resources
- waveform cache entries
- frozen audio data
- other immutable buffers published to the audio engine
