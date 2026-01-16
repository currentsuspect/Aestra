// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

/**
 * @file MultiClipPlaylist.h
 * @brief Master include for the Multi-Clip Playlist System
 * 
 * This header includes all components of the multi-clip playlist system:
 * 
 * ## Core Data Types
 * - TimeTypes.h: SampleIndex, SampleCount, SampleRange, grid utilities
 * - ClipSource.h: AudioBufferData, ClipSource, SourceManager
 * - PlaylistClip.h: PlaylistClipID, PlaylistClip
 * - PlaylistModel.h: PlaylistLane, PlaylistModel
 * 
 * ## Real-Time Engine
 * - PlaylistRuntimeSnapshot.h: ClipRuntimeInfo, LaneRuntimeInfo, snapshots, trash queue
 * - PlaylistMixer.h: RT-safe audio mixing
 * 
 * ## UI Support
 * - PlaylistGeometry.h: Pixel ↔ sample conversions, hit testing
 * - WaveformCache.h: Multi-resolution waveform peak cache
 * - SelectionModel.h: Clip/lane selection state
 * 
 * ## Persistence
 * - PlaylistSerializer.h: JSON save/load
 * 
 * ## Quick Start
 * 
 * ```cpp
 * #include "MultiClipPlaylist.h"
 * 
 * using namespace Nomad::Audio;
 * 
 * // Create managers
 * SourceManager sourceManager;
 * PlaylistModel model;
 * PlaylistSnapshotManager snapshotManager;
 * 
 * // Create a track
 * auto laneId = model.createLane("Track 1");
 * 
 * // Load audio file
 * auto sourceId = sourceManager.getOrCreateSource("path/to/audio.wav");
 * auto source = sourceManager.getSource(sourceId);
 * auto buffer = std::make_shared<AudioBufferData>();
 * // ... load audio into buffer ...
 * source->setBuffer(buffer);
 * 
 * // Add a clip
 * model.addClipFromSource(laneId, sourceId, 0, 48000 * 10);  // 10 seconds at 48kHz
 * 
 * // Build snapshot for audio thread
 * auto snapshot = model.buildRuntimeSnapshot(sourceManager);
 * snapshotManager.pushSnapshot(std::move(snapshot));
 * 
 * // In audio callback:
 * void processBlock(float* left, float* right, uint32_t numFrames) {
 *     auto* snap = snapshotManager.getCurrentSnapshot();
 *     PlaylistMixer::process(snap, playheadPosition, left, right, numFrames,
 *                            trackBuffer, clipBuffer);
 *     playheadPosition += numFrames;
 * }
 * 
 * // Periodically on engine thread:
 * snapshotManager.collectGarbage();
 * ```
 * 
 * ## Architecture Overview
 * 
 * ```
 * ┌─────────────────────────────────────────────────────────────┐
 * │                      UI THREAD                              │
 * │  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  │
 * │  │ SelectionModel│  │ PlaylistGeo  │  │ WaveformCache   │  │
 * │  └──────────────┘  └──────────────┘  └──────────────────┘  │
 * └─────────────────────────────────────────────────────────────┘
 *              │ reads                          │ reads
 *              ▼                                ▼
 * ┌─────────────────────────────────────────────────────────────┐
 * │                    ENGINE THREAD                            │
 * │  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  │
 * │  │ SourceManager│  │PlaylistModel │  │SnapshotManager  │  │
 * │  │ (owns audio) │  │ (owns clips) │  │ (builds snaps)  │  │
 * │  └──────────────┘  └──────────────┘  └──────────────────┘  │
 * └─────────────────────────────────────────────────────────────┘
 *                          │ atomic swap
 *                          ▼
 * ┌─────────────────────────────────────────────────────────────┐
 * │                    AUDIO THREAD (RT)                        │
 * │  ┌─────────────────────────────────────────────────────┐   │
 * │  │ PlaylistRuntimeSnapshot (read-only, immutable)      │   │
 * │  │ PlaylistMixer::process() (no alloc, no locks)       │   │
 * │  └─────────────────────────────────────────────────────┘   │
 * └─────────────────────────────────────────────────────────────┘
 * ```
 */

// Foundation
#include "TimeTypes.h"

// Data model
#include "ClipSource.h"
#include "PlaylistClip.h"
#include "PlaylistModel.h"

// Real-time engine
#include "PlaylistRuntimeSnapshot.h"
#include "PlaylistMixer.h"

// UI support
#include "PlaylistGeometry.h"
#include "WaveformCache.h"
#include "SelectionModel.h"

// Persistence
#include "PlaylistSerializer.h"

namespace Nomad {
namespace Audio {

/**
 * @brief Version information for the Multi-Clip Playlist System
 */
struct MultiClipPlaylistVersion {
    static constexpr int MAJOR = 1;
    static constexpr int MINOR = 0;
    static constexpr int PATCH = 0;
    static constexpr const char* STRING = "1.0.0";
};

} // namespace Audio
} // namespace Nomad
