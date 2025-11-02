# 🐛 Bug Reports Guide

This guide explains how to report bugs effectively for Nomad DAW, ensuring issues can be reproduced and fixed quickly.

---

## 📋 Table of Contents

- [Before You Report](#before-you-report)
- [How to Report a Bug](#how-to-report-a-bug)
- [Bug Report Template](#bug-report-template)
- [Reproduction Steps](#reproduction-steps)
- [Gathering System Information](#gathering-system-information)
- [Log Files and Debugging](#log-files-and-debugging)
- [Visual Bugs and Screenshots](#visual-bugs-and-screenshots)
- [Audio Bugs and Recordings](#audio-bugs-and-recordings)
- [What Happens Next](#what-happens-next)

---

## 🔍 Before You Report

Before creating a bug report, please:

### 1. Search Existing Issues
Check if someone has already reported the bug:
- Visit [GitHub Issues](https://github.com/currentsuspect/NOMAD/issues)
- Search for keywords related to your bug
- Check both open and closed issues

### 2. Verify It's a Bug
Ensure the behavior is actually a bug:
- Check the documentation for expected behavior
- Try reproducing on a clean install
- Test with minimal plugins/samples loaded
- Verify your system meets [minimum requirements](../README.md#-supported-platforms--requirements)

### 3. Update to Latest Version
- Pull the latest code from `main` branch
- Rebuild the project: `cmake --build build --config Release`
- Test if the bug still occurs

---

## 📝 How to Report a Bug

### Step 1: Open a New Issue
1. Go to [GitHub Issues](https://github.com/currentsuspect/NOMAD/issues/new)
2. Choose "Bug Report" if a template is available
3. Fill in all requested information

### Step 2: Use a Clear Title
**Bad:** "It doesn't work"  
**Good:** "Audio cuts out 7 seconds early on 48kHz samples"

**Bad:** "Crash when clicking"  
**Good:** "Application crashes when clicking transport bar during playback"

### Step 3: Provide Complete Information
Include:
- **Summary** — Brief description of the bug
- **Expected Behavior** — What should happen
- **Actual Behavior** — What actually happens
- **Steps to Reproduce** — Exact steps to trigger the bug
- **Environment** — OS, build type, audio driver, etc.
- **Logs/Screenshots** — Evidence of the issue

---

## 📄 Bug Report Template

```markdown
## Bug Summary
<!-- One-line description of the issue -->

## Environment
- **OS:** Windows 11 22H2
- **Build Type:** Debug / Release
- **Audio Driver:** WASAPI Exclusive / Shared
- **Audio Device:** Focusrite Scarlett 2i2
- **Sample Rate:** 48000 Hz
- **Buffer Size:** 512 samples
- **Nomad Version:** Commit SHA or date

## Expected Behavior
<!-- What you expected to happen -->

## Actual Behavior
<!-- What actually happened -->

## Steps to Reproduce
1. Launch Nomad DAW
2. Load a 48kHz WAV file
3. Press play
4. Observe that audio cuts 7 seconds early

## Reproducibility
<!-- Always / Sometimes / Rarely -->

## Workaround
<!-- If you found a temporary fix, describe it here -->

## Additional Context
<!-- Logs, screenshots, recordings, stack traces -->
```

---

## 🔁 Reproduction Steps

Clear reproduction steps are **critical** for fixing bugs. Follow this format:

### Example: Good Reproduction Steps

```markdown
## Steps to Reproduce
1. Launch NOMAD.exe
2. Click "File" → "Open"
3. Navigate to C:\Samples\
4. Load "Kick_48kHz.wav" (1.5 MB, 10 seconds long)
5. Press spacebar to play
6. Observe audio stops at 3.0 seconds instead of 10.0 seconds

## Expected Result
Audio plays for full 10 seconds

## Actual Result
Audio stops at 3.0 seconds, timer freezes
```

### Example: Bad Reproduction Steps

```markdown
## Steps to Reproduce
1. Open file
2. Play it
3. Bug happens
```

**Why bad?** Missing crucial details like file format, file size, timing, and specific actions.

---

## 🖥️ Gathering System Information

### Windows System Info
Run in PowerShell:
```powershell
# OS Information
Get-ComputerInfo | Select-Object WindowsProductName, WindowsVersion, OsHardwareAbstractionLayer

# Audio Devices
Get-WmiObject Win32_SoundDevice | Select-Object Name, Status, DeviceID

# GPU Information
Get-WmiObject Win32_VideoController | Select-Object Name, DriverVersion, VideoMemoryType
```

### Build Information
Include in your report:
```bash
# Git commit SHA
git rev-parse HEAD

# CMake configuration
cmake --version
cmake -LA build | grep -E "CMAKE_BUILD_TYPE|NOMAD"

# Compiler version
cl.exe /?  # For MSVC
gcc --version  # For GCC
```

---

## 📂 Log Files and Debugging

### Locating Log Files
Nomad logs are typically saved to:
- **Windows:** `%APPDATA%\Nomad\logs\nomad.log`
- **Linux:** `~/.nomad/logs/nomad.log`

### Including Logs in Reports
1. Reproduce the bug
2. Immediately close Nomad
3. Copy the log file
4. Attach to GitHub issue (or paste relevant sections)

**Tip:** Enable verbose logging before reproducing:
```cpp
// In Source/Main.cpp
Log::setLogLevel(LogLevel::Debug);  // Change from Info to Debug
```

### Example Log Section
```plaintext
[INFO] [2025-01-15 14:32:10] AudioDeviceManager: Initializing WASAPI Exclusive mode
[INFO] [2025-01-15 14:32:10] Track: Loading sample C:\Samples\Kick_48kHz.wav
[ERROR] [2025-01-15 14:32:11] Track: Sample rate mismatch - expected 44100, got 48000
[WARN] [2025-01-15 14:32:11] Track: Playback stopped early at position 3.0s
```

**Analysis:** This log reveals a sample rate mismatch bug.

---

## 📸 Visual Bugs and Screenshots

For UI bugs, include screenshots:

### Good Screenshot Practices
- **Annotate** — Use arrows and text to highlight the issue
- **Full Context** — Show the entire window, not just the bug
- **Multiple Angles** — Before/after screenshots if applicable
- **Readable Text** — Ensure text is sharp and readable

### Example: Reporting a UI Bug
```markdown
## Visual Bug: Scrollbar Overlaps Track Controls

**Screenshot:**
![Scrollbar Bug](https://i.imgur.com/example.png)

**Description:**
The vertical scrollbar overlaps the mute/solo buttons when more than 10 tracks are loaded.

**Expected:**
Scrollbar should be to the right of all track controls.
```

### Tools for Annotations
- **Windows:** Snip & Sketch (Win + Shift + S)
- **ShareX:** Free screenshot tool with annotations
- **Paint.NET:** Free image editor

---

## 🎵 Audio Bugs and Recordings

For audio-related bugs, provide:

### 1. Audio Recordings
- Record the buggy audio output using a screen recorder (OBS, ShareX)
- Include both Nomad's audio output and system audio
- Upload to a file host (Dropbox, Google Drive, GitHub)

### 2. Sample Files
If the bug is related to a specific audio file:
- Provide the sample file (if under 10 MB)
- Or provide specifications: format, sample rate, bit depth, duration
- Use free hosting: WeTransfer, Dropbox, Google Drive

### 3. Audio Configuration
Include:
```markdown
## Audio Configuration
- **Driver:** WASAPI Exclusive
- **Device:** Focusrite Scarlett 2i2 USB
- **Sample Rate:** 48000 Hz
- **Buffer Size:** 512 samples
- **Latency:** 10.67 ms (measured)
- **Channels:** Stereo (2)
```

### Example: Audio Glitch Report
```markdown
## Audio Bug: Crackling on 96kHz Playback

**Recording:** [Dropbox Link - Crackling_96kHz.mp3]

**Sample File:** [Dropbox Link - Original_96kHz.wav]

**Description:**
When playing 96kHz samples, intermittent crackling occurs every 2-3 seconds.
Crackling does not occur on 48kHz samples.

**Audio Setup:**
- Driver: WASAPI Shared (no Exclusive mode support)
- Device: Realtek High Definition Audio
- Buffer: 1024 samples
```

---

## ⏱️ Performance Issues

For performance bugs (lag, FPS drops):

### Profiling Data
1. Enable Tracy profiler (if available):
   ```cpp
   #define NOMAD_PROFILE_ENABLED 1
   ```
2. Reproduce the lag
3. Export Tracy trace: `File → Export JSON`
4. Attach to bug report

### FPS Display
1. Press F12 to toggle FPS overlay
2. Take a screenshot showing FPS drop
3. Note specific actions that cause FPS to drop

### Example: Performance Bug Report
```markdown
## Performance: FPS drops to 12 when opening settings

**Normal FPS:** 28-30 FPS  
**With Settings Open:** 12-17 FPS

**Screenshot:**
![FPS Drop](https://i.imgur.com/fps-drop.png)

**Profiling:**
- Settings dialog renders every frame without dirty checking
- Background overlay uses expensive alpha blending
- Dropdowns re-render all items every frame
```

---

## 🔧 Crash Reports

For crashes (application closes unexpectedly):

### Include Stack Trace
**Windows (Visual Studio):**
1. Build in Debug mode
2. Run under debugger (F5)
3. When crash occurs, copy stack trace from "Call Stack" window
4. Include in bug report

**Example Stack Trace:**
```
NomadUI::NUIButton::onRender(NUIRenderer&) Line 142
NomadUI::NUIComponent::render() Line 89
Main::run() Line 256
main() Line 45
```

### Windows Error Reporting
If you get a Windows crash dialog:
1. Click "View problem details"
2. Copy "Fault Module Name" and "Exception Code"
3. Include in report

### Minimal Crash Reproduction
Try to isolate the crash:
```markdown
## Crash: NULL pointer dereference in TransportBar

**Minimal Steps:**
1. Launch Nomad (clean state, no samples loaded)
2. Click Play button
3. Immediately click Stop button
4. Application crashes

**Stack Trace:**
```
TransportBar::update() — Line 89 — Access violation reading 0x00000000
```

**Analysis:**
TransportBar tries to access m_trackManager without null check.
```

---

## ✅ What Happens Next

After you submit a bug report:

### 1. Triage (1-2 days)
- Developers will review your report
- May ask for clarification or additional information
- Bug will be labeled (e.g., `bug`, `audio`, `ui`, `high-priority`)

### 2. Reproduction (1-7 days)
- Developers attempt to reproduce the bug
- If reproducible, bug is confirmed and prioritized
- If not reproducible, you may be asked for more details

### 3. Investigation (variable)
- Developers analyze the root cause
- May require code debugging and profiling
- Timeline depends on complexity and priority

### 4. Fix (variable)
- Developer implements a fix
- Fix is tested to ensure no regressions
- Pull request is created and reviewed

### 5. Release (next version)
- Fix is merged into `main` branch
- Bug is closed and marked as fixed
- Listed in release notes and changelog

---

## 🏷️ Bug Report Best Practices

### Do's ✅
- Be specific and detailed
- Include reproduction steps
- Provide logs and screenshots
- Use clear, professional language
- Test on latest version first
- One bug per report

### Don'ts ❌
- Don't report multiple bugs in one issue
- Don't use vague descriptions ("it's broken")
- Don't assume developers know your setup
- Don't skip reproduction steps
- Don't report feature requests as bugs
- Don't be rude or demanding

---

## 🎯 Bug Report Quality Checklist

Before submitting, verify:
- [ ] Clear, descriptive title
- [ ] Detailed reproduction steps
- [ ] Expected vs actual behavior
- [ ] System information included
- [ ] Logs or screenshots attached
- [ ] Latest version tested
- [ ] Searched for duplicates
- [ ] One bug per report
- [ ] Professional tone

---

## 📚 Related Resources

- **[Contributing Guide](CONTRIBUTING.md)** — How to contribute to Nomad
- **[Debugging Guide](developer/debugging.md)** — Advanced debugging techniques
- **[GitHub Issues](https://github.com/currentsuspect/NOMAD/issues)** — Browse existing bugs

---

**Thank you for helping improve Nomad DAW!** 🙏

Quality bug reports make a huge difference in fixing issues quickly and efficiently.

*Last updated: January 2025*
