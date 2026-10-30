---
name: Crash Report
about: Aestra closed, froze, or lost work — what to attach so it can be fixed
title: '[CRASH] '
labels: 'bug, priority: critical'
assignees: ''
---

# Crash Report

<!--
A crash report is worth exactly as much as what is attached to it. Aestra
records more than most people realise, and almost none of it is anywhere
obvious, so this template is mostly a map to it.

If you can only manage one thing: attach the two log files. Everything else
below can be reconstructed from them or asked for later.
-->

## What happened

<!-- One or two sentences. "Closed instantly with no dialog while dragging a
clip" is more useful than "crashed". -->

## Did it close, freeze, or misbehave?

- [ ] Closed instantly (no dialog)
- [ ] Froze / stopped responding
- [ ] Showed an error and kept running
- [ ] Lost work without closing

## The log — please attach it

**From v0.8.2**, Aestra writes one log file per run, named by when the run started
(`aestra-YYYYMMDD-HHMMSS.log`), and keeps the last 20:

| System | Folder |
|---|---|
| Linux | `~/.local/state/aestra/` (or `$XDG_STATE_HOME/aestra/`) |
| Windows | `%LOCALAPPDATA%\Aestra\logs\` |
| macOS | `~/Library/Logs/Aestra/` |

Attach the file from the run that crashed: the newest one, or the one whose time
matches the crash. Started from a terminal, Aestra prints the file's path on its
first line.

**Before v0.8.2** there were two files, `runtime_log.txt` (startup) and
`aestra_debug.log` (everything after), in whatever directory Aestra was launched
from (from a desktop icon, usually your home folder). They append and are never
rotated, so include the whole file, or its last few hundred lines plus the time
of the crash.

- [ ] Log attached

## The project

- [ ] `.aes` project file attached (or: it was an unsaved new project)

**If work was lost, also look for the autosave.** It sits next to your project
rather than in a system folder:

```
YourProject.aes            <- the project
YourProject.autosave/      <- timestamped backups live here
```

- [ ] Autosave / backup files attached, or none existed

<!-- If the project is private, say so — a crash can usually be narrowed down
from the logs alone, and a redacted or cut-down project that still reproduces
it is just as good. -->

## Build

<!-- Copy the two lines from the top of the log (before v0.8.2: runtime_log.txt). They look like:
     Aestra Starting - Aestra-2025-Core
     Working Directory: /home/you
     If you built it yourself, the git commit is more useful than a version. -->

- Build id / commit:
- Release or Debug:
- Installed, or built from source:

## System

- OS and version:
- CPU / RAM:
- Audio device and driver (WASAPI / ALSA / JACK / PipeWire):
- Sample rate and buffer size, if you know them:

<!-- Audio device details matter more than they look: a large share of crashes
and freezes come in through the device layer, and buffer size is the first
thing anyone will ask about a dropout or a hang. -->

## Reproducing it

**How often:**

- [ ] Every time, same steps
- [ ] Sometimes, same steps
- [ ] Once so far

**Steps:**

1.
2.
3.

<!-- If it only happened once and you cannot reproduce it, still file it. The
logs are the point; an unrepeatable crash with a full log is a real report. Say
what you were doing in the seconds before, even vaguely — that is often enough
to find it in the log. -->

## Anything else

<!-- Plugins loaded (and whether removing one stops it), whether it started
after a specific change, another app using the audio device at the time. -->

---

*"Clarity before speed."*
