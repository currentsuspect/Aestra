# Rejected Patterns

Optimizations that failed gates or were reverted. Recorded here so future
rounds don't repeat the same mistakes.

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
