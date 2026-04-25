# Post-v1.4.0 TODO

## Completed Today (2026-04-19)

### Stress Test Results

**PianoRoll Edge Cases:**
- 6/6 PASSED
  - Basic MidiNote structure ✓
  - Overlapping notes on same pitch - structure accepts ✓
  - Note at beat 0 - accepted ✓
  - Note at max beat boundary - accepted ✓
  - MIDI export with empty lanes - no crash ✓
  - Snap-to-scale with no scale set - no-op ✓

**PluginHost Edge Cases:**
- 10/10 PASSED
  - PluginInfo basic structure ✓
  - PluginParameter normalization ✓
  - Malformed metadata handled ✓
  - MidiBuffer basic operations ✓
  - MidiBuffer overflow handled ✓
  - Parameter state querying ✓
  - Duplicate plugin info - distinct instances ✓
  - Plugin state access after unload ✓
  - Parameter query after reload ✓

**TrackManager Edge Cases:**
- 10/10 PASSED
  - Short/max-length track name ✓
  - Channel ID sequence (master distinguished) ✓
  - Valid/invalid track index ✓
  - Reorder at boundaries (0 and last) ✓
  - No hard track count limit ✓
  - Automation invalidated on track removal ✓

**MixerChannel Edge Cases:**
- 16/16 PASSED
  - Volume 0 and max ✓
  - Volume out of range accepted (no clamping - UI responsibility) ✓
  - Multiple channels volume simultaneous ✓
  - Mute/solo consistency across channels ✓
  - Add send during playback ✓
  - Remove send during playback ✓
  - Remove invalid send index - safe ✓
  - Channel with no track defaults safe ✓

**FileBrowser Edge Cases:**
- 9/9 PASSED
  - Scan empty directory ✓
  - Scan non-audio files only ✓
  - Deeply nested paths ✓
  - Missing/deleted file handling ✓
  - Large directory (500+ files) ✓
  - Lexicographic sort on large dir ✓
  - Audio file extensions recognized ✓
  - Path with spaces ✓
  - Unicode filename ✓

**Transport Edge Cases:**
- 12/12 PASSED
  - Rapid start/stop cycles ✓
  - Seek while playing ✓
  - Seek to position 0 during playback ✓
  - Loop region end < start - detected ✓
  - Loop region fix via swap ✓
  - BPM change during playback ✓
  - BPM in valid range (20-500) ✓
  - Zero BPM rejected ✓
  - Time signature change ✓
  - Position at max project end ✓