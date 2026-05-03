# Session 006 — Restoration Listening Pack

## Status: PENDING DYLALN LISTENING

This pack needs local audio renders from Dylan's machine.
The reverb code changes are committed. Build and render locally.

## Test Scenarios

### Per-Mode Defaults
- Room default (decay 5.7s, damping 50%, predelay 10ms, size 0.52x, diffusion 64%)
- Hall default (same params)
- Plate default (same params)

### Stress Tests
- Vocal through Hall
- Vocal through Plate
- Full mix through Hall
- Full mix through Plate
- Bright transient through Plate
- Snare through Room

### Source Material
- snare
- bright ping
- vocal phrase
- chord stab
- low pulse
- mix bus

## What Changed (Session 004 → Session 006)

| Parameter | Session 004 | Session 006 | Direction |
|-----------|-------------|-------------|-----------|
| Predelay default | 30ms | 10ms | ← attached |
| Air blend (Room) | 7% | 15% | ← brighter |
| Air blend (Hall) | 5.8% | 14% | ← brighter |
| Air blend (Plate) | 5.5% | 18% | ← brighter |
| Tone cutoff base | +2000 Hz | ← raised |
| Box-cut (Room) | -4.6 dB | -3.3 dB | ← less cut |
| Box-cut (Hall) | -3.0 dB | -2.8 dB | ← less cut |
| Box-cut (Plate) | -4.2 dB | -3.3 dB | ← less cut |
| Mud HP blend (Room) | 70% | 40% | ← less thin |
| Mud HP blend (Hall) | 40% | 20% | ← less thin |
| Mud HP blend (Plate) | 60% | 35% | ← less thin |
| Mod depth default | 1.0 smp | 1.4 smp | ← more motion |
| Mod depth multiplier | 5.0 | 7.0 | ← more motion |
| Room modDepthScalar | 0.15 | 0.45 | ← audible mod |
| Random modulation | removed | 12% blend | ← organic |

## Build & Render

```bash
cd /path/to/Aestra
cmake --build build-linux --parallel
# Then render through your DAW or headless renderer
```
