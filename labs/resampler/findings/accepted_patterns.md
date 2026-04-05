# Accepted Patterns

Optimizations that measurably improved performance and passed all gates.
Recorded here so future rounds can reuse or build on them.

## Power-of-2 Bitwise AND for Ring Buffer Indexing

**Where**: `SampleHistory::push()`, `SampleHistory::getWindow()`
**What**: Replace `% size` with `& (size - 1)` when size is a power of 2.
**Why it works**: `HISTORY_SIZE = 128` (2^7). Integer division is expensive;
bitwise AND is a single cycle. For negative values, `& mask` wraps correctly
in two's complement (e.g., `-1 & 127 = 127`).
**Round**: 07

## Power-of-2 Bitwise AND for Polyphase Index

**Where**: `process()` inner loop, phase quantization
**What**: Replace `% POLYPHASE_PHASES` with `& (POLYPHASE_PHASES - 1)`.
**Why it works**: `POLYPHASE_PHASES = 256` (2^8). Same principle as ring buffer.
**Round**: 08

## Hoist SIMD Dispatch Outside Channel Loop

**Where**: `process()` inner loop, per-output-sample channel iteration
**What**: Move `if (useSIMD)` outside the `for (ch)` loop so the branch is
evaluated once per output sample instead of once per channel per output sample.
**Why it works**: Eliminates branch mispredictions in the innermost loop.
**Round**: 05

## Inline Window Access in Hot Loop

**Where**: `process()` inner loop
**What**: Replace `m_history.getWindow(ch, samplePos0)` with direct computation
of `windowIdx` and `&m_history.data[ch][windowIdx]`.
**Why it works**: Eliminates function call overhead and the `size == 0` bounds
check per output sample. The bounds check is redundant after `configure()`.
**Round**: 11

## Reduce Mirror Factor from 3 to 2

**Where**: `SampleHistory::kMirrorFactor`
**What**: Changed from 3 to 2. The max `writePos + rel` is `127 + 127 = 254`,
which fits in `128 * 2 = 256` slots.
**Why it works**: Saves 33% of writes per `push()` call (2 writes per channel
instead of 3). Improves cache locality (smaller buffer).
**Round**: 10

## Hoist Member Variables to Locals in Hot Loop

**Where**: `process()` — `m_srcPosition`, `m_nextOutputSrcPos`
**What**: Copy to local `double` variables at start, write back at end.
**Why it works**: Reduces memory traffic to member variables in the hot loop.
The compiler may not always optimize member accesses to registers.
**Round**: 14

## Hoist Invariant Computations Outside Loops

**Where**: `process()` — `1.0/effectiveRatio`, history constants, output pointer
**What**: Compute once per `process()` call instead of per-frame or per-sample.
**Why it works**: Division and multiply are hoisted out of the loop entirely.
**Round**: 02, 09, 13

## Remove Dead Writes from Hot Loop

**Where**: `process()` — `m_historyFilled` increment
**What**: Removed the saturating increment of `m_historyFilled` — the variable
is never read outside of `reset()` and `configure()`.
**Why it works**: Eliminates a conditional branch and a store per input frame.
**Round**: 12

## SSE4.1 Target Attribute on Scalar Dot Product

**Where**: `dotProductScalar()`
**What**: `__attribute__((target("sse4.1")))` on x86_64.
**Why it works**: Enables the compiler to use SSE multiply-add instructions
for auto-vectorization, even without AVX2. Safe because SSE4.1 is ubiquitous
on x86_64 (all CPUs since ~2007).
**Round**: 15 (retry)

## `#pragma GCC ivdep` on Dot Product Loop

**Where**: `dotProductScalar()` inner loop
**What**: `#pragma GCC ivdep` before the loop.
**Why it works**: Tells the compiler there are no loop-carried dependencies,
enabling more aggressive vectorization and unrolling.
**Round**: 16
