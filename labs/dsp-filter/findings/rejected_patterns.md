# Rejected Patterns

(No sessions yet)

## Current HEAD Is Not A Green Baseline

**Session**: M002
**What failed**: `AestraFilterTest` low-pass and high-pass checks on current
HEAD.
**Why it matters**: The lab now runs unattended and exposes a real red
correctness surface. This is not an optimization rejection; it means the
subsystem is not yet at a trustworthy starting baseline.
**Lesson**: Do not run bounded optimization rounds in this lab until the
correctness surface is green again.
**Status**: Resolved in M003 by fixing coefficient initialization/smoothing
behavior and normalizing the test measurement.

## Hoist Branches and Pointer-Walk the Inner `processBlock()` Loop

**Session**: M004 round 1
**What failed**: Replaced the indexed loop with branch-hoisted pointer paths
for drive and oversampling.
**Why it failed**: Correctness stayed green, but the advisory DSP density lane
regressed on this machine. The reshaped loop did not beat the compiler's
original codegen.

## Inline TwoX Oversampling Math Directly in `processBlock()`

**Session**: M004 round 2
**What failed**: Added a specialized `TwoX` fast path that manually inlined the
two oversampled biquad steps.
**Why it failed**: Correctness stayed green, but advisory performance remained
mixed-to-worse. The narrower specialization did not produce a trustworthy win.
