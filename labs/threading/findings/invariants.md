# Invariants

1. **SPSC exactly-once**: LockFreeRingBuffer delivers each item exactly once.
2. **Capacity**: `capacity() == Size - 1`.
3. **Size invariant**: `size() + available() == capacity()`.
4. **ThreadPool completeness**: Every enqueued task executes exactly once.
5. **Barrier**: Unblocks only when all expected signals arrive.
6. **Atomic correctness**: Flag, counter, spinlock work under contention.
7. **Cache alignment**: `alignas(64)` on head/tail prevents false sharing.
