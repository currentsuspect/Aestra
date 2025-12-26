# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased] - 2025-12-26

### Optimized
- **Audio Engine Performance**:
    - **Pan Law**: Replaced expensive per-sample trigonometry (`sin`/`cos`) with per-block gain smoothing (`gainL`/`gainR`), significantly reducing CPU overhead in the mixing loop.
    - **Resampling**: Implemented pre-calculated window tables for all Sinc Interpolators (8, 16, 32, 64-point), removing iterative Bessel function calculations from the audio callback.

### Fixed
- **Audio Engine Stability**:
    - Fixed "Master Silence" bug where buffer reallocation invalidated routing pointers (added `compileGraph` to `setBufferConfig`).
    - Restored missing audio summing loop in `renderGraph`.
    - Added Safety Fallback for unmapped tracks to ensure consistent gain behavior if UI parameters are missing.

## [Unreleased] - 2025-12-23

### Added
- **Audio Preview Scrubbing**:
    - Real-time scrubbing (click/drag) on waveforms in the File Preview Panel.
    - Dual-mode duration: 8-second initial limit, unlocked to 300 seconds upon scrubbing.
    - Auto-restart logic: Scrubbing a finished sample now automatically restarts playback from the seek point.
    - Real-time playhead visualization on the waveform.
- **File Preview Panel UX**:
    - Professional "Empty State" with a large file icon when no selection is active.
    - Improved Metadata display: Duration, Sample Rate, and Channel configuration.
    - Compact Folder info layout with side-by-side icon and text.

### Fixed
- **Audio Engine**:
    - Fixed critical crash/silence issue when scrubbing short samples or performing rapid seek requests.
    - Removed hardcoded 5-second duration limit in `NomadContent`.
    - Fixed duplicate `seek` method implementation in `PreviewEngine`.
- **UI**:
    - Fixed Folder name clipping/ellipsis behavior in the Preview Panel.
    - Disabled playback interactions for folders to prevent invalid engine states.
- **Build System**:
    - Suppressed CMake deprecation warnings from the FreeType dependency for cleaner build output.

### Changed
- **Tests**:
    - Updated `WavLoaderTest.cpp` to use `PlaylistTrack.h` instead of the legacy `Track.h`.
- **Documentation**:
    - Created `ALL_WALKTHROUGHS.txt` as a central index for project history.
