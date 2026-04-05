# Rejected Patterns

## Direct Pointer Access Rewrite For `getPeakRange()` And `buildNextLevel()`

**Session**: M003 round 2
**What failed**: Replaced repeated `getPeak()` calls with direct pointer
arithmetic inside range merging and mip construction.
**Why it failed**: Correctness stayed green, but the advisory latency lane was
slightly worse than the prior accepted run. Because the lane is noisy and the
signal was not clearly positive, the rewrite was rejected.
