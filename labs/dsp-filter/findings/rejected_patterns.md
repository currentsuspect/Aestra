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
