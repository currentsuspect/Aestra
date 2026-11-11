---
name: Bug Report
about: Report an issue to help improve Aestra
title: '[BUG] '
labels: 'bug'
assignees: ''
---

# Bug Report

## Description
A clear description of what's broken.

## Environment

**Platform:**
- OS: [e.g., Windows 11, macOS 14, Ubuntu 22.04]
- Architecture: [e.g., x64, ARM64]
- Aestra Version: [e.g., v1.0.0]
- Build Type: [Release/Debug]

**Hardware:**
- CPU: [e.g., Intel i7-12700K]
- RAM: [e.g., 16GB]
- GPU: [e.g., NVIDIA RTX 3060] (if relevant)

## Steps to Reproduce

1. 
2. 
3. 

## Expected Behavior
What should happen?

## Actual Behavior
What actually happens?

## Logs/Output

<!--
From v0.8.2 Aestra writes one log per run, aestra-YYYYMMDD-HHMMSS.log, in
  Linux    ~/.local/state/aestra/   (or $XDG_STATE_HOME/aestra/)
  Windows  %LOCALAPPDATA%\Aestra\logs\
  macOS    ~/Library/Logs/Aestra/
and prints the path on its first line when started from a terminal.

Before v0.8.2 there were two files, runtime_log.txt (startup) and
aestra_debug.log (the run), in the directory Aestra was launched from.
Both append and are never rotated, so an old file holds many runs — the lines
around the time it went wrong are the ones that matter.

If Aestra crashed or froze, please use the Crash Report template instead: it
asks for the autosave and build details that a crash needs and this one does not.
-->

```
Paste relevant logs, error messages, or console output here
```

## Screenshots
If applicable, add screenshots.

## Additional Context

**Affected Layer:**
- [ ] AestraCore
- [ ] AestraPlat
- [ ] AestraUI
- [ ] AestraAudio
- [ ] AestraSDK
- [ ] Build System
- [ ] Other: ___________

**Severity:**
- [ ] Critical (crashes, data loss)
- [ ] High (major functionality broken)
- [ ] Medium (feature partially broken)
- [ ] Low (minor issue, workaround exists)

## Possible Solution
If you have ideas on how to fix this, share them here.

---

*"Clarity before speed."*
