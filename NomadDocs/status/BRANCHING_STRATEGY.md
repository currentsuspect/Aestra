# NOMAD Git Branching Strategy v1.0

**Maintained by:** Nomad Development Team  
**Last Updated:** October 2025

## Philosophy
"Branches are intentions. Commits are promises. Merges are rituals."

## Branch Structure

```
main ────────●───────●───────●───────▶
              ╲
develop ───────●─────●─────●────────▶
                 ╲
feature/audio ────●──●────▶
```

### `main` - The Sacred Branch
- **Purpose:** Production-ready, documented, stable releases only
- **Protection:** Never commit directly
- **Merge from:** `develop` only, after full testing
- **Tagging:** All releases tagged as `v1.0.0`, `v1.5.0`, etc.
- **Rule:** If it's on main, it's sacred

### `develop` - The Integration Branch
- **Purpose:** Active development, feature integration
- **Merge from:** Feature branches after review
- **Merge to:** `main` when stable
- **Rule:** Must always compile and run

### Feature Branches

#### `feature/nomad-core`
- **Purpose:** Build NomadCore layer (math, file I/O, threading, logging)
- **Base:** `develop`
- **Merge to:** `develop` when complete

#### `feature/nomad-plat`
- **Purpose:** Extract and perfect platform abstraction layer
- **Base:** `develop`
- **Dependencies:** None
- **Merge to:** `develop` when Win32/X11/Cocoa abstraction complete

#### `feature/nomad-audio`
- **Purpose:** Integrate RtAudio + build NomadAudio engine
- **Base:** `develop`
- **Dependencies:** `feature/nomad-core`
- **Merge to:** `develop` when audio I/O working

#### `feature/nomad-dsp`
- **Purpose:** DSP modules (filters, oscillators, envelopes)
- **Base:** `develop`
- **Dependencies:** `feature/nomad-audio`
- **Merge to:** `develop` for v1.5

#### `feature/nomad-sdk`
- **Purpose:** Plugin system and extension API
- **Base:** `develop`
- **Dependencies:** All core systems
- **Merge to:** `develop` for v3.0

### Hotfix Branches - The Emergency Channel

#### `hotfix/*`
- **Purpose:** Critical production fixes only
- **Base:** `main`
- **Merge to:** Both `main` AND `develop`
- **Rule:** Use sparingly, document thoroughly

**Hotfix Workflow:**
```bash
git checkout main
git checkout -b hotfix/critical-bug
# Fix, test, commit
git checkout main
git merge hotfix/critical-bug --no-ff
git checkout develop
git merge hotfix/critical-bug --no-ff
git branch -d hotfix/critical-bug
```

## Workflow

### Starting New Work
```bash
git checkout develop
git pull origin develop
git checkout -b feature/your-feature-name
```

### Daily Commits
```bash
git add .
git commit -m "feat: describe what you built

- Detail 1
- Detail 2

Architecture: which layers affected"
```

### Before Merging a Feature

**Completion Checklist:**
- [ ] Code compiles without warnings
- [ ] No regression in previous modules
- [ ] Architecture notes updated
- [ ] Documentation or demo added
- [ ] Self review complete

### Merging to Develop
```bash
git checkout develop
git pull origin develop
git merge feature/your-feature-name --no-ff
git push origin develop
```

### Release to Main
```bash
git checkout main
git merge develop --no-ff -m "release: v1.0.0 - description"
git tag -a v1.0.0 -m "NOMAD v1.0.0 - milestone description"
git push origin main --tags
```

## Commit Message Format

```
<type>: <subject>

<body>

Architecture: <affected layers>
```

### Types
- `feat`: New feature
- `fix`: Bug fix
- `refactor`: Code restructure (no behavior change)
- `perf`: Performance improvement
- `docs`: Documentation only
- `test`: Adding tests
- `chore`: Build/tooling changes

### Examples

**Feature:**
```
feat: implement RtAudio backend for NomadAudio

- Add RtAudioBackend.cpp with WASAPI support
- Create AudioDeviceManager interface
- Implement lock-free ring buffer for UI communication
- Add audio callback with <10ms latency

Architecture: NomadAudio, NomadCore (threading)
```

**Fix:**
```
fix: resolve OpenGL texture leak in NUIRendererGL

- Add glDeleteTextures cleanup in drawTexture
- Validate data pointer before upload
- Add memory profiling test

Architecture: NomadUI, NomadRenderer
```

**Refactor:**
```
refactor: extract platform window code to NomadPlat

- Move Win32 window creation from NomadUI to NomadPlat
- Create platform abstraction interface
- No behavior changes, pure architectural cleanup

Architecture: NomadPlat, NomadUI
```

## Current State

- ✅ `main`: Clean foundation (NomadUI complete, JUCE removed)
- ✅ `develop`: Ready for feature work
- ⏳ Feature branches: Create as needed

## Rules

1. **Never force push to `main` or `develop`**
2. **Always branch from `develop` for features**
3. **Document every commit with architecture notes**
4. **Test before merging to `develop`**
5. **Tag every `main` merge with version**

---

*"Build like silence is watching."*
