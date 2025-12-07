# 🐛 Bug Report & Technical Debt

## Overview
This report outlines potential bugs, missing features, and technical debt identified during the static analysis of the NOMAD DAW codebase.

## 🚨 Critical Issues

### 1. Hardcoded BPM
**Severity:** High
**Location:** `Source/TrackUIComponent.cpp`, `Source/TrackManagerUI.cpp`, `Source/Main.cpp`
**Description:**
The BPM (Beats Per Minute) is hardcoded to `120.0` in multiple locations instead of being retrieved from a central project configuration.
```cpp
double bpm = 120.0; // TODO: Get from project settings
```
**Impact:** Users cannot change the tempo of their project, rendering the DAW unusable for any genre other than 120 BPM house music.

### 2. Missing Track Selection Logic
**Severity:** Medium
**Location:** `Source/Main.cpp`
**Description:**
The track selection logic is incomplete. The main application loop has a TODO indicating that it cannot verify which track is actually selected.
```cpp
// TODO: Implement track selection in TrackManagerUI to get actually selected track
```
**Impact:** Operations that depend on the currently selected track (e.g., recording, effect application) may fail or apply to the wrong track.

## ⚠️ Missing Features

### 3. Audio Metering
**Severity:** Medium
**Location:** `Source/MixerView.cpp`
**Description:**
The mixer view lacks proper audio metering.
```cpp
// TODO: Implement proper metering from audio callback
```
**Impact:** Users have no visual feedback on audio levels, making mixing impossible.

### 4. Sample Name Rendering
**Severity:** Low
**Location:** `Source/TrackUIComponent.cpp`
**Description:**
Sample names are not rendered on the track clips.
```cpp
// TODO: Add text rendering for sample name when text API is available
```
**Impact:** Users cannot identify which sample is on which clip without auditioning it.

## 🔧 Technical Debt

### 5. Coordinate Transformation
**Location:** `Source/TrackManagerUI.cpp`
**Description:**
There is a TODO regarding coordinate transformation in `setClipRect`.
```cpp
// TODO: Fix coordinate transformation in setClipRect or add a UI-space clip method
```
**Impact:** Potential UI rendering glitches or hit-testing issues.

### 6. UI Caching
**Location:** `Source/TrackManagerUI.cpp`
**Description:**
Static UI controls and waveforms are not cached.
```cpp
// TODO: Cache static UI controls (buttons, labels) - not implemented yet
// TODO: Per-track FBO caching for waveforms - not implemented yet
```
**Impact:** Reduced rendering performance, potentially affecting the "buttery-smooth 60 FPS" goal.
