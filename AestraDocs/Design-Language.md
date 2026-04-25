# Aestra Design Language — Borrowed Metaphors

**Status:** Internal — Core design philosophy
**Last Updated:** 2026-04-11
**Owner:** Dylan

---

## Philosophy

Aestra's design language is built on metaphors borrowed from domains outside audio production. This creates interactions that feel fresh because they ARE fresh in the context of a DAW.

**The rule:** Every metaphor must be encoded in Aestra's native vocabulary so it feels like Aestra, not like a copy. Users should think "this is how Aestra works" not "this is like Unreal/Spotify/Git."

---

## The Metaphor Stack

### 1. Unreal Blueprints → Routing Visualizer

**Borrowed from:** Unreal Engine's node-based Blueprint editor for visual scripting.

**In Aestra:** The routing visualizer shows signal flow as a node graph with animated connections.

**Aestra vocabulary:**
- Nodes = Tracks, Buses, Returns, Master
- Wires = Routes (main output, sends, sidechain)
- Signal flow dots = Animated indicators showing audio traveling through wires

**Visual language:**
| Element | Style |
|---------|-------|
| Main output | Thick solid line, source track color |
| Audible send | Thin solid line, source track color |
| Sidechain | Dotted line, dimmed source color |
| Pre-fader send | Dashed line |
| Level indicator | Sliding dot on wire, proportional to send level |

**Color system:**
- Lines inherit source track color
- Drums = Red/orange
- Melodic = Blue/purple
- Vocal = Green
- FX Returns = Gold/amber
- Buses = White/gray
- Master = Bright white

**Layout:**
- Left-to-right signal flow
- Sources on left, buses in center, destinations on right
- Lanes for vertical grouping (drum tracks, melodic tracks)
- Animated dots travel along wires when audio is playing

**Interaction:**
- Drag from node output to node input = create route
- Click wire = edit route (gain, pan, pre/post, sidechain)
- Hover node = highlight connections, dim others
- Double-click node = open track inspector
- Right-click wire = delete, duplicate, change type

---

### 2. Spotify → Audition Mode

**Borrowed from:** Spotify's playback interface and playlist queue system.

**In Aestra:** A dedicated listening environment for final mix evaluation, album coherence, and translation checking.

**Aestra vocabulary:**
- Queue = Ordered list of tracks/sections to audition
- Now Playing = Currently auditioned track with waveform
- DSP Preset = "How will this sound on [platform]?"

**DSP Presets (translation checks):**
| Preset | Simulates |
|--------|-----------|
| Studio Reference | Flat, no processing |
| Spotify | -14 LUFS, -1dB true peak, loud normalization |
| Apple Music | -16 LUFS, Sound Check |
| YouTube | -14 LUFS, Opus compression artifacts |
| SoundCloud | -14 LUFS, 128kbps MP3 artifacts |
| Car Speakers | Harman curve, bass boost, treble roll-off |
| AirPods Pro | Adaptive EQ simulation |

**Key features:**
- Queue timeline tracks and external reference files
- A/B comparison (wet/dry DSP toggle)
- Waveform scrubbing
- Shuffle, repeat, crossfade
- Cover art display
- "Psychologically out of the DAW" — no mixer, no meters, just listening

**Album mode (future):**
- Ordered track list = album sequence
- Transition markers between tracks
- Coherence analysis across tracks
- Comments/markers for final tweaks linked back to timeline

---

### 3. Git → Version Control (Takes)

**Borrowed from:** Git's branching, committing, and merging model.

**In Aestra:** Mix versioning with musical naming instead of engineering terms.

**Terminology mapping:**
| Git | Aestra | Meaning |
|-----|--------|---------|
| Branch | Take | A divergent version of the mix |
| Commit | Snapshot | A saved state at a point in time |
| Merge | Blend | Combining changes from two Takes |
| Diff | Compare | Showing differences between Takes |
| HEAD | Current | The active Take |
| Stash | Draft | Uncommitted changes set aside |
| Remote | Cloud | Shared Takes via Aestra cloud (future) |

**Data model:**
- Hybrid: snapshots (full state) + deltas (command-based diffs)
- Snapshots for major milestones (before export, before major changes)
- Deltas for lightweight branching between snapshots
- Restore from any snapshot, apply deltas forward/backward

**UX:**
- "Create Take" = branch from current state
- Visual timeline of Takes (like git log --graph)
- "Compare Takes" = side-by-side diff showing changes
- "Blend Takes" = merge with conflict resolution (which version of this clip?)
- Auto-snapshots at key moments (before export, before plugin removal)

**Cloud (future):**
- "Publish Take" = push to Aestra cloud
- "Pull Take" = fetch collaborator's version
- GitHub-like interface for shared projects

---

### 4. Google Maps → Timeline Modes (C|E|A)

**Borrowed from:** Google Maps' layer switching (Map, Satellite, Terrain).

**In Aestra:** The timeline has multiple view modes for the same data, switchable via pipe syntax.

**The pipe: `C|E|A`**

| Mode | Full Name | Shows |
|------|-----------|-------|
| C | Clips | Audio/MIDI clips on timeline (default arrangement view) |
| E | Editor | Piano roll / pattern editor (focused editing) |
| A | Automation | Automation curves overlaid on timeline |

**Interaction:**
- Click C, E, or A to switch modes
- Pipe syntax can combine: `C|A` shows clips with automation overlay
- Smooth transition between modes (animated zoom/morph)
- Each mode remembers its zoom level independently

**Future modes (speculative):**
| Mode | Shows |
|------|-------|
| M | Mixer overlay on timeline |
| R | Routing graph overlay |
| S | Spectrum analysis overlay |

**The Maps metaphor extends to zoom:**
- Zoomed out = overview (like satellite view)
- Zoomed in = detail (like street view)
- Minimap in corner = overview rectangle
- Pinch to zoom = smooth continuous zoom

---

### 5. Terminal Piping → Signal Flow

**Borrowed from:** Unix terminal pipe operator (`|`).

**In Aestra:** Signal flow is expressed as pipes connecting processing stages.

**Examples:**
```
track | bus | master
track | reverb_send | reverb_return | master
808 | sidechain → compressor | master
```

**Where this appears:**
- Route display in mixer inspector: `Track 1 → Drum Bus → Master`
- C|E|A mode switch is itself a pipe
- Future: command palette could accept pipe syntax for routing

**Visual encoding:**
- `|` = normal signal flow (solid arrow)
- `→` = explicit destination
- `┈` = sidechain (dotted)
- The pipe character is Aestra's signature visual element

---

### 6. Fortnite → Card System

**Borrowed from:** Fortnite's cosmetic card/skin system (free game, paid cosmetics).

**In Aestra:** Gamified license cards with rarity, identity, and collectibility.

**Cards:**
| Tier | Color | Rarity | Obtained By |
|------|-------|--------|-------------|
| Core | Grey | Common | Free — everyone gets one |
| Campus | Blue | Academic | Free — .edu verification |
| Supporter | Silver | Uncommon | $5/mo subscription |
| Founder | Gold | Legendary | $129 one-time (limited window) |

**Properties:**
- Visual card art with tier-specific styling
- Unique ID number (especially Founders: #0042)
- Seasonal variants for active Supporters
- Level/frame upgrades through usage (not payment)
- Physical card for Founders (metal, engraved)

**Display locations:**
- App header (small card icon)
- Audition "Now Playing" when sharing
- Community profiles
- Project sharing (future)

---

### 7. Code Copilot → Muse AI

**Borrowed from:** GitHub Copilot's predictive code completion.

**In Aestra:** Predictive creative assistant for music production.

**Core principle:** Suggest, don't generate. Autocomplete, don't author.

**See [Muse-AI-Spec.md](./Muse-AI-Spec.md) for full specification.**

---

## Design Rules

### Rule 1: Metaphors Must Be Native

Never reference the source metaphor in user-facing text. Don't say "like Unreal Blueprints" — say "routing visualizer." Don't say "like Spotify" — say "Audition mode." The metaphor informs the design, but the name is Aestra's.

### Rule 2: Consistency Across Metaphors

The pipe syntax (`|`) appears in multiple places:
- `C|E|A` timeline modes
- `track | bus | master` routing display
- Signal flow visualization

This consistency makes the borrowed metaphors feel like a unified language, not a collection of disconnected ideas.

### Rule 3: Visual Hierarchy

Each system has a clear visual hierarchy:
- Routing: color-coded by source, line style by route type
- Audition: dark theme, waveform-focused, minimal chrome
- Timeline modes: animated transitions between views
- Cards: tier-specific colors, premium materials feel

### Rule 4: Interaction Over Configuration

Aestra favors direct manipulation over dialog boxes:
- Drag to route (not "add send" dialog)
- Click to switch mode (not "view settings")
- Accept/dismiss suggestions (not "configure Muse")

### Rule 5: Respect the User

Every borrowed metaphor serves the user's workflow:
- Routing visualizer = understanding signal flow
- Audition mode = evaluating like a listener
- Version control = protecting creative work
- Timeline modes = viewing the same data differently
- Card system = identity and belonging
- Muse AI = creative assistance

None of these exist to extract money, data, or attention. They exist to make the user better at making music.

---

*This document is internal. The design language is core to Aestra's identity — protect it.*
