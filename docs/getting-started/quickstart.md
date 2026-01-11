# NOMAD DAW Quickstart Tutorial

This quickstart guide will walk you through your first session with NOMAD DAW.

## 🎯 Overview

After building NOMAD, you'll learn:

- How to launch NOMAD and navigate the UI
- Understanding the FL Studio-inspired timeline
- Loading and playing audio samples
- Basic mixing controls
- Saving and loading projects

## 🚀 Launching NOMAD

### Windows
```powershell
cd build/bin/Release
./NOMAD.exe
```

### Linux/macOS
```bash
./build/bin/NOMAD
```

On first launch, NOMAD will:

1. Initialize the audio engine (WASAPI/ALSA/CoreAudio)
2. Load the default theme
3. Create a new empty project

## 🖥️ Understanding the Interface

NOMAD's interface consists of several key areas:

### Main Sections

```
┌─────────────────────────────────────────────────┐
│  Menu Bar        [File] [Edit] [View] [Help]   │
├─────────────────────────────────────────────────┤
│  Transport       [◀◀] [▶] [■] [●] [⏸]          │
├─────────────────────────────────────────────────┤
│                                                 │
│  Timeline / Pattern Editor                      │
│  (FL Studio-inspired sequencer)                 │
│                                                 │
├─────────────────────────────────────────────────┤
│  Mixer / Channel Rack                           │
│                                                 │
└─────────────────────────────────────────────────┘
```

### Key UI Components

1. **Menu Bar** — File operations, preferences, and help
2. **Transport Controls** — Play, pause, stop, record
3. **Timeline** — Pattern and playlist view with adaptive grid
4. **Mixer** — Volume, pan, mute, solo controls (in progress)
5. **Status Bar** — CPU usage, audio latency, sample rate

## 🎵 Your First Project

### Step 1: Configure Audio Settings

1. Open **File → Preferences** (or press `Ctrl+P`)
2. Navigate to **Audio Settings**
3. Select your audio interface
4. Set buffer size (512 samples recommended for ~10ms latency)
5. Choose sample rate (48000 Hz recommended)
6. Click **Apply**

!!! tip "Low Latency Tips"
    - Use WASAPI Exclusive mode on Windows for lowest latency
    - Smaller buffer sizes = lower latency but higher CPU usage
    - Start with 512 samples and adjust based on your system

### Step 2: Create a Pattern

1. Right-click in the timeline to create a new pattern
2. Name your pattern (e.g., "Kick Pattern")
3. The pattern editor will open

### Step 3: Add Samples (Coming Soon)

!!! warning "In Development"
    Sample manipulation and drag-and-drop features are currently in development.
    This section will be updated in Q1 2025 when these features are completed.

### Step 4: Use the Timeline

The timeline in NOMAD is inspired by FL Studio's workflow:

- **Pattern Mode** — Create and edit individual patterns
- **Playlist Mode** — Arrange patterns on the timeline
- **Adaptive Grid** — Automatically adjusts to zoom level
- **Waveform Visualization** — See your audio in real-time

#### Timeline Controls

| Action | Shortcut | Description |
|--------|----------|-------------|
| Play/Pause | `Space` | Start/stop playback |
| Stop | `Escape` | Stop and return to start |
| Zoom In | `Ctrl + +` | Increase timeline zoom |
| Zoom Out | `Ctrl + -` | Decrease timeline zoom |
| Scroll | `Mouse Wheel` | Navigate timeline |

### Step 5: Basic Mixing (In Progress)

!!! warning "In Development"
    Mixing controls are currently being implemented.
    Volume, pan, mute, and solo features will be available soon.

## 🎨 Customizing Your Workspace

### Theme Selection

NOMAD supports multiple themes:

1. Open **View → Theme**
2. Choose from:
   - **Dark Mode** (default) — Professional dark theme
   - **Light Mode** — Clean light theme
3. Toggle anytime with `Ctrl+T`

### UI Preferences

Adjust the interface to your liking:

- **FPS Limit** — 24-60 FPS adaptive rendering
- **Grid Snap** — Enable/disable grid snapping
- **Waveform Colors** — Customize visualization colors
- **Font Size** — Adjust UI text size

## 💾 Saving Your Project

1. Click **File → Save Project** (or press `Ctrl+S`)
2. Choose a location for your project
3. Enter a project name
4. Click **Save**

NOMAD project files use the `.nomad` extension.

!!! tip "Save Often"
    Use `Ctrl+S` frequently to save your work.
    Auto-save features are planned for a future update.

## 📊 Performance Monitoring

NOMAD displays real-time performance metrics:

- **CPU Usage** — Shows DSP load percentage
- **Audio Latency** — Current round-trip latency in milliseconds
- **Sample Rate** — Current audio sample rate
- **Buffer Size** — Current audio buffer size

Find these in the status bar at the bottom of the window.

## 🎹 Keyboard Shortcuts

Essential shortcuts to know:

| Action | Shortcut |
|--------|----------|
| New Project | `Ctrl+N` |
| Open Project | `Ctrl+O` |
| Save Project | `Ctrl+S` |
| Save As | `Ctrl+Shift+S` |
| Undo | `Ctrl+Z` |
| Redo | `Ctrl+Y` |
| Play/Pause | `Space` |
| Stop | `Escape` |
| Toggle Theme | `Ctrl+T` |

## 🐛 Troubleshooting

### Audio Not Working

1. Check audio device selection in preferences
2. Verify your audio interface is connected
3. Try increasing buffer size
4. On Windows, ensure WASAPI drivers are installed

### High CPU Usage

1. Increase buffer size (e.g., 512 → 1024 samples)
2. Reduce FPS limit in preferences
3. Close unnecessary background applications
4. Check for plugin CPU usage (when VST3 support is added)

### Visual Glitches

1. Update graphics drivers
2. Check OpenGL 3.3+ support
3. Try disabling MSAA in graphics settings
4. Report issues on GitHub

## 🚀 What's Next?

Now that you know the basics:

1. **[Explore the Architecture](../architecture/overview.md)** — Understand how NOMAD works
2. **[Read the Developer Guide](../developer/contributing.md)** — Contribute to NOMAD
3. **[Check the Roadmap](../technical/roadmap.md)** — See what's coming next
4. **[Join the Community](../community/code-of-conduct.md)** — Connect with other users

## 💡 Tips for Success

!!! tip "Best Practices"
    - Start with a reliable audio interface
    - Keep projects organized in dedicated folders
    - Save different versions as you work
    - Monitor CPU usage to avoid dropouts
    - Join GitHub Discussions for tips and tricks

---

**Need help?** Check the [FAQ](../technical/faq.md) or ask in [GitHub Discussions](https://github.com/currentsuspect/NOMAD/discussions).
