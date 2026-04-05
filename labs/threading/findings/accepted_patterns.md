# Accepted Patterns

## Power-of-2 Ring Buffer Modulo → Bitmask

**Where**: `AestraCore/include/AestraThreading.h`
**What**: `LockFreeRingBuffer` uses `constexpr` dispatch: `(idx & (Size-1))` for power-of-2 sizes, `% Size` otherwise.
**Why it works**: Modulo is a `div` instruction (~20-80 cycles); bitwise AND is 1 cycle. Both test sizes (8, 1024) are power-of-2.
**Session**: M003 R2

## Barrier Spin-Wait with Pause Instruction

**Where**: `AestraCore/include/AestraThreading.h`
**What**: `Barrier::wait()` uses `__builtin_ia32_pause()` (x86) / `yield` (ARM) instead of `std::this_thread::yield()` syscall.
**Why it works**: `yield()` triggers a context switch (~1-5μs). `pause` is a ~100-cycle hint to the CPU pipeline. For audio graph micro-second waits, spin is correct.
**Session**: M003 R3

## ThreadPool Atomic Stop with Double-Checked Lock

**Where**: `AestraCore/include/AestraThreading.h`
**What**: `stop` is `std::atomic<bool>`. Destructor holds lock when setting `stop`. `enqueue` checks `stop` fast-path (relaxed), then re-checks under lock.
**Why it works**: Prevents TOCTOU race where tasks are enqueued after workers exit. Fast-path avoids lock in common case.
**Session**: M003 R4, R8

## Conditional Condition Variable Notification

**Where**: `AestraCore/include/AestraThreading.h`
**What**: `ThreadPool::enqueue` only calls `notify_one()` when `tasks.empty()` was true before emplacing.
**Why it works**: `notify_one()` is a futex syscall. If workers are already busy (queue not empty), they'll pick up new tasks without being woken.
**Session**: M003 R9

## Adaptive Exponential Backoff in Barrier

**Where**: `AestraCore/include/AestraThreading.h`
**What**: `Barrier::wait()` spins with 1→2→4→...→256 `pause` iterations, then yields.
**Why it works**: Short waits (common in audio graph) exit after 1-2 pauses. Long waits adapt to yield after 256 spins. Total before first yield: ~511 pauses ≈ 50K cycles ≈ 12μs at 4GHz.
**Session**: M003 R10

## Ring Buffer Size via Bitmask Arithmetic

**Where**: `AestraCore/include/AestraThreading.h`
**What**: For power-of-2 sizes, `size()` returns `(write - read) & (Size - 1)` leveraging unsigned wraparound. `available()` delegates to `capacity() - size()`.
**Why it works**: `(w - r) & mask` is correct for both wrapped and unwrapped cases due to unsigned arithmetic. Eliminates branch.
**Session**: M003 R11

## Atomic m_taskCount in RealTimeThreadPool

**Where**: `AestraCore/include/AestraThreading.h`
**What**: `m_taskCount` changed from `uint32_t` to `std::atomic<uint32_t>`.
**Why it works**: `dispatch` writes `m_taskCount`, worker wait predicate reads it. Without atomic, this is a data race. The acquire-release on `m_taskCounter` provides happens-before, but `m_taskCount` itself needs atomic access.
**Session**: M003 R12

## [[unlikely]] on Ring Buffer Full/Empty Guards

**Where**: `AestraCore/include/AestraThreading.h`
**What**: `push` and `pop` mark the full/empty branch as `[[unlikely]]`.
**Why it works**: In the audio engine hot path, the ring buffer is typically neither full nor empty. Compiler lays out the fast path contiguously.
**Session**: M003 R13

## ThreadPool Workers Reserved Upfront

**Where**: `AestraCore/include/AestraThreading.h`
**What**: `workers.reserve(numThreads)` before the construction loop.
**Why it works**: Avoids vector reallocation during thread construction. Minor but eliminates potential allocation during thread startup.
**Session**: M003 R14

## Enqueue Returns [[nodiscard]] bool

**Where**: `AestraCore/include/AestraThreading.h`
**What**: `ThreadPool::enqueue` returns `true` if task was accepted, `false` if pool is shutting down.
**Why it works**: Silent task drops after shutdown are a correctness hazard. Callers must handle rejection explicitly. Mirrors ring buffer `[[nodiscard]] bool push()`.
**Session**: M003 R16

## SpinLock Uses std::atomic_flag

**Where**: `AestraCore/include/AestraThreading.h`
**What**: `SpinLock` uses `std::atomic_flag` (guaranteed lock-free by C++ standard) with `pause` instruction per iteration.
**Why it works**: `std::atomic<bool>` may use internal locking on some platforms. `atomic_flag` is always lock-free. `pause` reduces power consumption and improves SMT fairness.
**Session**: M003 R17

## Diagnostic Asserts on Barrier::reset() and RealTimeThreadPool::dispatch()

**Where**: `AestraCore/include/AestraThreading.h`
**What**: `Barrier::reset()` asserts counter is 0. `RealTimeThreadPool::dispatch()` asserts `m_activeTasks` is 0.
**Why it works**: Both catch usage errors in debug builds: premature barrier reset and re-entrant dispatch before previous batch completes. Zero overhead in release builds.
**Session**: M003 R18, R19
