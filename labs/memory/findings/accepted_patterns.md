# Accepted Patterns

Optimizations that measurably improved memory behavior and passed all gates.
Recorded here so future rounds can reuse or build on them.

## Enable Memory Profiling Globally

**Where**: `AestraCore/CMakeLists.txt`
**What**: Added `AESTRA_ENABLE_MEMORY_PROFILING` to `target_compile_definitions`.
**Why it works**: The profiler infrastructure was already complete — macros,
`MemoryStats` struct, `recordMemoryAllocation()`/`recordMemoryDeallocation()`.
The only missing piece was the compile flag. Zero runtime overhead when profiler
is disabled (macros are no-ops), and minimal overhead when enabled (counter
increments).
**Round**: 01 (session 001)

## Wire AESTRA_MEMORY_ALLOC/FREE at Key Allocation Sites

**Where**: `AudioProcessor.cpp` (AudioBufferManager), `Filter.cpp` (resize),
`SampleRateConverter.cpp` (filter bank)
**What**: Added `AESTRA_MEMORY_ALLOC(size)` after each `new`/`make_unique`,
and `AESTRA_MEMORY_FREE(size)` in destructors.
**Why it works**: These are the primary allocation sites in audio-thread-adjacent
code. Wiring them gives visibility into when and how much memory is allocated
during configure and processing. Foundation for measuring and eliminating
hot-path allocations.
**Round**: 01 (session 001)
