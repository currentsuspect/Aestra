# 📝 Documentation & Comment Style Guide

This guide defines standards for writing documentation and code comments in Nomad DAW.

---

## 📋 Table of Contents

- [Documentation Principles](#-documentation-principles)
- [Markdown Documentation](#-markdown-documentation)
- [Code Comments](#-code-comments)
- [API Documentation](#-api-documentation)
- [Commit Messages](#-commit-messages)
- [Examples](#-examples)

---

## 🎯 Documentation Principles

### 1. Clarity Over Cleverness

Write for understanding, not to impress:

**Bad:**
```markdown
Leverage synergistic paradigms to effectuate audio pipeline optimization.
```

**Good:**
```markdown
Use efficient algorithms to process audio faster.
```

### 2. Show, Don't Just Tell

Include examples whenever possible:

**Bad:**
```markdown
The `Track` class manages audio playback.
```

**Good:**
```markdown
The `Track` class manages audio playback:

\`\`\`cpp
Track track;
track.loadSample("kick.wav");
track.play();
\`\`\`
```

### 3. Keep It Updated

Documentation must match the code:
- Update docs when changing code
- Remove outdated information
- Mark deprecated features clearly

### 4. Write for Beginners

Assume readers are new to the codebase:
- Define technical terms
- Link to related documentation
- Provide context and background

---

## 📄 Markdown Documentation

### File Structure

Every `.md` file should follow this structure:

```markdown
# [Title with Emoji]

[Brief one-line description]

---

## 📋 Table of Contents

- [Section 1](#-section-1)
- [Section 2](#-section-2)

---

## [Content Sections]

[Detailed content with examples]

---

## 📚 Related Resources

- [Link 1](path/to/doc1.md)
- [Link 2](path/to/doc2.md)

---

*Last updated: [Month Year]*
```

### Headings

Use emoji to make headings visually distinct:

```markdown
# 🎵 Audio System          (H1 - Document title)
## 🔧 Configuration         (H2 - Major section)
### Buffer Size             (H3 - Subsection, no emoji)
#### Example                (H4 - Detail level, no emoji)
```

**Emoji Guidelines:**
- 🎵 🎶 — Audio-related
- 🎨 🖌️ — UI-related
- 🔧 ⚙️ — Configuration, settings
- 📝 📄 — Documentation, writing
- 🐛 🔍 — Debugging, bugs
- ⚡ 🚀 — Performance, speed
- 📚 📖 — Resources, learning
- ✅ ❌ — Do's and don'ts

### Code Blocks

Always specify the language for syntax highlighting:

````markdown
```cpp
// C++ code
void myFunction() {
    Log::info("Hello!");
}
```

```bash
# Shell commands
cmake --build build
```

```json
{
  "version": "1.0"
}
```
````

### Lists

**Unordered Lists:**
```markdown
- First item
- Second item
  - Nested item
  - Another nested item
- Third item
```

**Ordered Lists:**
```markdown
1. First step
2. Second step
3. Third step
```

**Task Lists:**
```markdown
- [x] Completed task
- [ ] Pending task
- [ ] Another pending task
```

### Tables

Use tables for structured data:

```markdown
| Feature | Supported | Notes |
|---------|-----------|-------|
| WASAPI  | ✅        | Recommended |
| ASIO    | 🚧        | Planned |
| CoreAudio | ❌      | Future |
```

### Links

**Internal Links:**
```markdown
See [Building Guide](../BUILDING.md) for instructions.
```

**External Links:**
```markdown
Download [Tracy Profiler](https://github.com/wolfpld/tracy/releases).
```

**Anchor Links (same page):**
```markdown
Jump to [Configuration](#configuration).

## Configuration
```

### Callouts

Use blockquotes for important notes:

```markdown
> **Note:** This feature is experimental.

> **Warning:** This will delete all data!

> **Tip:** Press F12 to toggle debug overlay.
```

---

## 💬 Code Comments

### Comment Philosophy

**When to Comment:**
- ✅ Complex algorithms
- ✅ Non-obvious decisions
- ✅ Public API documentation
- ✅ Workarounds or hacks
- ✅ Performance-critical sections

**When NOT to Comment:**
- ❌ Obvious code
- ❌ Bad code (fix it instead)
- ❌ Redundant information

### Comment Style

**Single-line Comments:**
```cpp
// Use double-slash for single-line comments
```

**Multi-line Comments:**
```cpp
/*
 * Use block comments for multiple lines.
 * Each line starts with an asterisk.
 */
```

**Section Dividers:**
```cpp
// =============================================================================
// Audio Processing
// =============================================================================
```

### Good vs Bad Comments

**Bad Comment (states the obvious):**
```cpp
// Increment counter
counter++;
```

**Good Comment (explains why):**
```cpp
// Skip first sample to avoid click at start
counter++;
```

**Bad Comment (redundant):**
```cpp
// Set volume to 0.5
track.setVolume(0.5);
```

**Good Comment (adds context):**
```cpp
// Use 0.5 volume as default mixing headroom (-6dB)
track.setVolume(0.5);
```

### TODO Comments

Mark incomplete work clearly:

```cpp
// TODO(dylan): Implement sample rate conversion
// TODO(dylan): Optimize this loop (currently O(n²))
// FIXME(dylan): Memory leak when closing settings dialog
// HACK(dylan): Temporary workaround for Windows 11 DPI bug
```

**Format:** `// TODO(author): Description`

### Performance Comments

Document optimization decisions:

```cpp
// PERF: Use SIMD for 8x speedup on large buffers
void processVectorized(float* buffer, size_t size) {
    // ... SIMD code ...
}

// PERF: Cache waveform to avoid recomputing every frame (100x faster)
m_waveformCache.resize(4096);
```

---

## 📖 API Documentation

### Function Documentation

Use block comments for public APIs:

```cpp
/**
 * Loads an audio sample from disk.
 * 
 * @param path Absolute path to WAV file
 * @return true if loaded successfully, false otherwise
 * 
 * @note Only 16-bit PCM WAV files are supported.
 * @see saveSample() for saving audio
 * 
 * Example:
 * \code
 * Track track;
 * if (track.loadSample("C:/Samples/kick.wav")) {
 *     track.play();
 * }
 * \endcode
 */
bool loadSample(const std::string& path);
```

### Class Documentation

Document class purpose and usage:

```cpp
/**
 * Manages audio playback for a single track.
 * 
 * Tracks can load audio samples, play them back, and apply effects.
 * Each track runs in its own processing thread for low latency.
 * 
 * Thread Safety: All public methods are thread-safe.
 * 
 * Example:
 * \code
 * Track track;
 * track.loadSample("sample.wav");
 * track.setVolume(0.8);
 * track.play();
 * \endcode
 */
class Track {
    // ...
};
```

### Member Variable Documentation

Document non-obvious variables:

```cpp
class AudioEngine {
private:
    /// Master output volume (0.0 = silent, 1.0 = unity gain)
    std::atomic<float> m_masterVolume{1.0f};
    
    /// Sample rate of audio device (typically 44100 or 48000 Hz)
    uint32_t m_sampleRate;
    
    /// Pre-allocated buffer to avoid allocation in audio thread
    std::vector<float> m_mixBuffer;
};
```

### Doxygen Tags

Use Doxygen-compatible tags:

- `@param` — Function parameter
- `@return` — Return value
- `@note` — Important note
- `@warning` — Warning or caution
- `@see` — Related function/class
- `@throws` — Exceptions thrown
- `@deprecated` — Deprecated feature

---

## 📝 Commit Messages

### Format

```
<type>(<scope>): <subject>

<body>

<footer>
```

### Types

- `feat` — New feature
- `fix` — Bug fix
- `docs` — Documentation only
- `style` — Code style (formatting, no logic change)
- `refactor` — Code refactoring
- `perf` — Performance improvement
- `test` — Adding tests
- `chore` — Build process, dependencies

### Examples

**Feature:**
```
feat(audio): add WASAPI exclusive mode support

- Implement exclusive mode initialization
- Add fallback to shared mode
- Reduce latency from 20ms to 5ms

Closes #42
```

**Bug Fix:**
```
fix(ui): prevent scrollbar overlap with track controls

The scrollbar was positioned incorrectly when more than 10 tracks
were loaded, causing overlap with mute/solo buttons.

Fixed by adjusting scrollbar position calculation.

Fixes #128
```

**Documentation:**
```
docs: add performance tuning guide

Created comprehensive guide covering:
- Audio optimization techniques
- UI rendering best practices
- Memory management strategies
```

**Refactoring:**
```
refactor(core): simplify audio buffer allocation

Replaced dynamic allocation with pre-allocated buffers to improve
real-time safety and reduce latency spikes.

No functional changes.
```

### Commit Message Best Practices

**Do's:**
- ✅ Use present tense ("add feature", not "added feature")
- ✅ Capitalize first letter of subject
- ✅ No period at end of subject line
- ✅ Limit subject to 50 characters
- ✅ Wrap body at 72 characters
- ✅ Separate subject and body with blank line
- ✅ Use body to explain *what* and *why*, not *how*

**Don'ts:**
- ❌ Don't use vague messages ("fix bug", "update code")
- ❌ Don't commit unrelated changes together
- ❌ Don't include WIP (work in progress) commits in main branch

---

## 🎓 Examples

### Example 1: Function with Good Documentation

```cpp
/**
 * Calculates the playback position in seconds.
 * 
 * Converts internal sample position to time using the device sample rate.
 * This ensures accurate timing regardless of the sample's original rate.
 * 
 * @return Current playback position in seconds
 * 
 * @note Uses device sample rate, NOT the sample file's rate.
 * @see setPosition() to change playback position
 * 
 * Example:
 * \code
 * double pos = track.getPosition();
 * Log::info("Position: " + std::to_string(pos) + "s");
 * \endcode
 */
double Track::getPosition() const {
    // Use device sample rate for accurate time calculation
    return m_playbackPhase.load() / (double)m_deviceSampleRate;
}
```

### Example 2: README Section

```markdown
## 🛠️ How to Build

### Quick Start (Windows)

1. **Install Prerequisites:**
   - CMake 3.15+
   - Git
   - Visual Studio 2022 with C++ workload
   - PowerShell 7

2. **Clone and Build:**
   \`\`\`powershell
   git clone https://github.com/currentsuspect/NOMAD.git
   cd NOMAD
   
   # Configure build
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   
   # Build project
   cmake --build build --config Release --parallel
   \`\`\`

3. **Run Nomad:**
   \`\`\`powershell
   cd build/bin/Release
   ./NOMAD.exe
   \`\`\`

### Troubleshooting

If you encounter build errors, see [Building Guide](../BUILDING.md).
```

### Example 3: Inline Code Comment

```cpp
void TrackManager::processAudio(float* buffer, uint32_t frames, double time) {
    // Clear output buffer
    memset(buffer, 0, frames * 2 * sizeof(float));
    
    // Mix all tracks into output buffer
    for (auto& track : m_tracks) {
        if (!track->isMuted()) {
            // PERF: Process each track in parallel (future optimization)
            track->processAudio(buffer, frames, time);
        }
    }
    
    // Apply master volume (range: 0.0 - 2.0, default: 1.0)
    float masterVol = m_masterVolume.load();
    for (uint32_t i = 0; i < frames * 2; ++i) {
        buffer[i] *= masterVol;
        
        // SAFETY: Clamp to prevent clipping
        buffer[i] = std::clamp(buffer[i], -1.0f, 1.0f);
    }
}
```

---

## ✅ Documentation Checklist

Before submitting documentation, verify:

- [ ] Spell-check completed
- [ ] Code examples compile and run
- [ ] All links work (internal and external)
- [ ] Headings use consistent emoji style
- [ ] Code blocks have language specified
- [ ] Year references are current (2025)
- [ ] No outdated information
- [ ] Clear and concise language
- [ ] Examples included where helpful
- [ ] Related resources linked

---

## 📚 Additional Resources

- **[Contributing Guide](../CONTRIBUTING.md)** — How to contribute
- **[Coding Style Guide](../CODING_STYLE.md)** — Code formatting rules
- **[Bug Reports Guide](BUG_REPORTS.md)** — Reporting issues

---

**Write docs that you'd want to read!** 📖

*Last updated: January 2025*
