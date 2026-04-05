# Bottlenecks

Known performance characteristics of the command history subsystem.

## Current State

No performance profiling has been done yet. The command system is not on the
hot path of the audio engine, so performance is secondary to correctness.

## Potential Areas

- `CommandHistory::pushAndExecute()` — allocates shared_ptr, may be called frequently
- History trimming — linear scan when memory limit is hit
- Callback invocation — fires on every state change

## What's NOT a Bottleneck

- Test execution — runs in milliseconds, not a performance concern.
