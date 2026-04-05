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
**Round**: 16 (session 001)

## Explicit Loop Unrolling for Dot Product

**Where**: `dotProductScalar()`
**What**: Unroll by 8 with remainder handling, replacing the simple loop.
**Why it works**: Gives the compiler known iteration count patterns for better
register allocation and instruction scheduling. Works well with SSE4.1 target.
**Round**: 01 (session 002)

## Dead Branch Removal in Window Access

**Where**: `process()` inner loop, window index computation
**What**: Removed `if (rel < 0) rel += size` after `samplePos0 & sizeMask`.
**Why it works**: Bitwise AND with a power-of-2 mask never produces negative
values in two's complement. The branch was dead code left over from the original
`getWindow()` which used `%` (can return negative for negative operands).
**Round**: 02 (session 002)

## Stereo Fast Path (Channel Loop Unroll)

**Where**: `process()` inner loop, channel iteration
**What**: Added `if (m_channels == 2)` fast path with two independent dot
product calls instead of a loop.
**Why it works**: All benchmarks use 2 channels. Unrolling to two independent
accumulators lets the CPU schedule both dot products in parallel with no loop
overhead.
**Round**: 03 (session 002)

## `__restrict__` on Dot Product Parameters

**Where**: `dotProductScalar()` signature
**What**: Added `__restrict__` to both `a` and `b` pointer parameters.
**Why it works**: Documents non-aliasing intent and gives the compiler permission
to vectorize more aggressively without proving independence. Marginal gain on
top of ivdep + unroll, but zero cost.
**Round**: 04 (session 002)

## Hoist Per-Frame Double Subtractions

**Where**: `process()` — output sample loop
**What**: Hoist `srcPosition - halfTapsD` and `srcPosition - historySizeMinusHalfTaps`
into locals before the output loop (constant per input frame).
**Why it works**: Eliminates redundant double subtractions per output sample.
Significant for upsampling cases where many output samples are generated per
input frame.
**Round**: 05 (session 002)
