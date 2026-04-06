# Rejected Patterns

Optimizations that failed gates or were reverted. Recorded here so future
rounds don't repeat the same mistakes.

## Tap-Count-Specialized Dot Product via Switch/If Dispatch

**Round**: 01 (session 004)
**What**: Added `dotProductFixed<N>()` template for fully unrolled dot products
and dispatched via `switch (numTaps)` / `if (numTaps <= 4)` in the stereo output
path for Linear (2), Cubic (4), Sinc8 (8), Sinc16 (16), Sinc64 (64).
**Why it failed**: The switch/if dispatch overhead dominated for larger tap counts.
Sinc8 regressed +41.5%, Sinc16 +50.3%, Sinc64 +118.2%. Even the refined version
with `if (numTaps <= 4)` caused Cubic to regress +10.3% to +17.7%. The branch
added per-output-sample overhead that outweighed the loop-unroll benefit for all
but Linear (which improved -21%).
**Lesson**: The existing `dotProductScalar` with unroll-by-8 + SSE4.1 target is
already optimal for larger tap counts. Adding dispatch branches in the hot loop
costs more than the generic path. Only consider compile-time template specialization
(if the quality level is known at compile time, which it isn't).

## Restoring `#pragma GCC ivdep` on Unrolled Dot Product

**Round**: 02 (session 004)
**What**: Added `#pragma GCC ivdep` (wrapped in `#ifdef __GNUC__`) back to the
unroll-by-8 loop in `dotProductScalar()`. This pragma was present at round 16
(commit `ec273726`) but was dropped when unroll-by-8 was added in session 002.
**Why it failed**: Regressed Sinc8 +11.8%, Sinc64 down +7.1%, Sinc16 down +7.9%.
The unrolled loop with explicit 8-wide accumulation already provides ILP. The
`ivdep` pragma confused GCC 15's auto-vectorizer into making worse scheduling
decisions. The pragma was likely dropped intentionally in session 002 for this reason.
**Lesson**: With explicit loop unrolling, `ivdep` is counterproductive on GCC 15.

## Eliminating `srcNextDiff` Variable

**Round**: 03 (session 004)
**What**: Removed `srcNextDiff` entirely. Computed `historyPos` directly as
`historySizeMinus1 - srcPosition + nextOutputSrcPos` instead of
`historySizeMinus1 - srcNextDiff`. Eliminated the `srcNextDiff -= invRatio`
dead write in the output loop (saving 1 double subtract per output sample).
**Why it failed**: Massive regression across ALL cases: Linear +64%, Cubic +79%,
Sinc64 +68%. CV was 0.2%, confirming real regression (not noise). The math was
provably correct — the regression was due to GCC 15's register allocator using
`srcNextDiff` for optimal register allocation. Removing it forced a worse
allocation strategy across the entire function.
**Lesson**: With GCC 15 -O3, seemingly "dead" variables can improve register
allocation. Do NOT remove local variables from the hot loop even if they appear
to have no readers — the compiler uses them as register hints.

## Hoisting `writePos` to a Local Variable

**Round**: 15 (attempt 1)
**What**: Copied `m_history.writePos` to a local `uint32_t` at the start of
`process()`, used it in the inner loop instead of reading from `m_history`.
**Why it failed**: `writePos` changes every frame via `m_history.push()`.
Hoisting it to a local meant the inner loop always saw the initial value,
causing incorrect window access and breaking round-trip correctness
(RMS error went from 0.068 to 0.790).
**Lesson**: Only hoist member variables that are truly invariant for the
entire `process()` call. `writePos` is modified by `push()` which is called
inside the loop. `srcPosition` and `nextOutputSrcPos` are safe because they
are only written by the loop itself.

## Conditional Wrap Instead of Modulo for `push()`

**Round**: Attempted during session 001 (not formally numbered)
**What**: Replaced `writePos = (writePos + 1) % size` with
`++writePos; if (writePos >= size) writePos = 0;`
**Why it was reverted**: Mixed results — helped some cases, hurt others.
The branch is unpredictable when `writePos` wraps (every 128 iterations).
The modulo on a power-of-2 constant is already optimized by the compiler
to a bitwise AND. The conditional branch added branch misprediction overhead.
**Lesson**: On this machine, the compiler already optimizes `% power_of_2`
to `& mask`. Replacing it with a conditional branch is not an improvement.
The bitwise AND (round 07) is the correct approach.
