# Accepted Patterns

## Remove Per-Build Info Logging From `buildFromRaw()`

**Where**: `AestraAudio/src/IO/WaveformCache.cpp`
**What**: Removed the `Log::info(...)` call emitted after every cache rebuild.
**Why it works**: The advisory concurrency test rebuilds the cache dozens of
times per run. Logging every build adds avoidable background-thread overhead and
contention without improving correctness.
**Observed effect**: On this machine, one accepted run improved advisory reader
latency from about `0.0466 ms` average / `0.175 ms` max to about `0.0285 ms`
average / `0.156 ms` max, with more writer rebuild completions.
**Session**: M003
