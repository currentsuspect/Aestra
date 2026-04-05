# Bottlenecks

No profiling done yet. The threading primitives are foundational — improvements
here benefit the entire audio engine.

## Potential Areas

- LockFreeRingBuffer: throughput under high contention
- ThreadPool: task dispatch overhead for small tasks
- SpinLock: contention behavior under high thread count
