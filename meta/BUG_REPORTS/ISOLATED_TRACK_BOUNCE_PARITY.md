# Isolated Track Bounce Render Parity

**Issue:** Slice 3 of the 2026-05 audio quality audit consolidated master bounce to use `AudioExporter::render`, but isolated track bounce still uses the old `AudioRenderer::renderBlock` path.

**Impact:** Isolated track bounce lacks master-stage processing and dithering, causing audio quality mismatch with master bounce and export.

**Current State:**
- Master bounce (trackId == -1): Delegates to `AudioExporter::render` with full master-stage processing and dithering
- Isolated track bounce (trackId >= 0): Uses `AudioRenderer::renderBlock` without master-stage processing

**TODO:**
- Add isolated track support to `AudioExporter::render`
- Update `AudioEngine::bounceRangeToWav` to delegate all bounces to `AudioExporter`
- Ensure isolated track bounce gets consistent dithering and processing

**Reference:** `AestraAudio/src/Core/AudioEngine.cpp:3028` (TODO comment)
