// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "TimeTypes.h"
#include "ClipSource.h"
#include <atomic>
#include <cstdint>
#include <vector>

namespace Nomad {
namespace Audio {

// =============================================================================
// ClipRuntimeInfo - POD struct for RT thread
// =============================================================================

/**
 * @brief Flattened clip data for real-time audio processing
 * 
 * This is a POD-like struct that the audio thread can safely read.
 * Contains resolved pointers and cached values - no shared_ptr, no virtual calls.
 * 
 * CRITICAL: This struct must remain simple and cache-friendly.
 * The audio thread loops over arrays of these, so they should be contiguous.
 */
struct ClipRuntimeInfo {
    // === Audio Data (raw pointer, lifetime managed by engine thread) ===
    const AudioBufferData* audioData = nullptr;  ///< Raw pointer to audio buffer
    
    // === Source Properties ===
    uint32_t sourceSampleRate = 0;               ///< Source sample rate for SRC
    uint32_t sourceChannels = 0;                 ///< Source channel count
    
    // === Timeline Position (project sample rate) ===
    SampleIndex startTime = 0;                   ///< Start on timeline
    SampleIndex length = 0;                      ///< Duration on timeline
    
    // === Source Offset ===
    SampleIndex sourceStart = 0;                 ///< Offset into source audio
    
    // === Playback Properties ===
    float gainLinear = 1.0f;                     ///< Volume
    float pan = 0.0f;                            ///< Pan position
    bool muted = false;                          ///< Skip during playback
    
    // === Time-Stretch / SRC ===
    double playbackRate = 1.0;                   ///< Rate multiplier
    
    // === Fades ===
    SampleIndex fadeInLength = 0;
    SampleIndex fadeOutLength = 0;
    
    // === Flags ===
    uint32_t flags = 0;
    
    // === Helper Methods ===
    
    SampleIndex getEndTime() const { return startTime + length; }
    
    bool isValid() const { 
        return audioData != nullptr && audioData->isValid() && length > 0; 
    }
    
    bool overlaps(SampleIndex bufferStart, SampleIndex bufferEnd) const {
        return !(getEndTime() <= bufferStart || startTime >= bufferEnd);
    }
    
    /// Calculate gain at position including fades
    float getGainAt(SampleIndex timelinePos) const {
        if (timelinePos < startTime || timelinePos >= getEndTime()) return 0.0f;
        
        SampleIndex offsetFromStart = timelinePos - startTime;
        SampleIndex offsetFromEnd = getEndTime() - timelinePos;
        
        float fadeGain = 1.0f;
        
        if (fadeInLength > 0 && offsetFromStart < fadeInLength) {
            fadeGain *= static_cast<float>(offsetFromStart) / static_cast<float>(fadeInLength);
        }
        
        if (fadeOutLength > 0 && offsetFromEnd < fadeOutLength) {
            fadeGain *= static_cast<float>(offsetFromEnd) / static_cast<float>(fadeOutLength);
        }
        
        return gainLinear * fadeGain;
    }
};

// =============================================================================
// LaneRuntimeInfo - POD struct for RT thread
// =============================================================================

/**
 * @brief Flattened lane data for real-time audio processing
 * 
 * Contains a vector of clips (sorted by start time) and lane properties.
 */
struct LaneRuntimeInfo {
    std::vector<ClipRuntimeInfo> clips;          ///< Sorted by startTime
    
    float volume = 1.0f;
    float pan = 0.0f;
    bool muted = false;
    bool solo = false;
    
    /// Find clips overlapping a buffer range using binary search
    std::pair<size_t, size_t> getClipRangeForBuffer(SampleIndex bufferStart, 
                                                      SampleIndex bufferEnd) const {
        if (clips.empty()) return {0, 0};
        
        // Binary search for first clip that might overlap
        size_t first = 0;
        size_t last = clips.size();
        
        // Find first clip whose end > bufferStart
        size_t lo = 0, hi = clips.size();
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (clips[mid].getEndTime() <= bufferStart) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        first = lo;
        
        // Find first clip whose start >= bufferEnd
        lo = first;
        hi = clips.size();
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (clips[mid].startTime < bufferEnd) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        last = lo;
        
        return {first, last};
    }
};

// =============================================================================
// PlaylistRuntimeSnapshot - Complete playlist state for RT thread
// =============================================================================

/**
 * @brief Immutable snapshot of playlist state for audio thread
 * 
 * This struct is created by the engine thread and atomically swapped
 * for the audio thread to read. The audio thread never modifies it.
 * 
 * Lifecycle:
 * 1. Engine thread creates new snapshot from PlaylistModel
 * 2. Engine thread atomically swaps pointer with audio thread's current
 * 3. Old snapshot goes to trash queue
 * 4. Engine thread (later) deletes old snapshot from trash queue
 * 
 * CRITICAL: Never delete a snapshot on the audio thread!
 */
struct PlaylistRuntimeSnapshot {
    std::vector<LaneRuntimeInfo> lanes;
    
    double projectSampleRate = 48000.0;
    uint64_t modificationId = 0;                 ///< For tracking version
    
    /// Check if any lane has solo enabled
    bool hasSoloLane() const {
        for (const auto& lane : lanes) {
            if (lane.solo) return true;
        }
        return false;
    }
    
    /// Check if a lane should be audible (considering solo/mute)
    bool isLaneAudible(size_t laneIndex, bool hasSolo) const {
        if (laneIndex >= lanes.size()) return false;
        const auto& lane = lanes[laneIndex];
        if (lane.muted) return false;
        if (hasSolo && !lane.solo) return false;
        return true;
    }
};

// =============================================================================
// SnapshotTrashQueue - Safe garbage collection for snapshots
// =============================================================================

/**
 * @brief Lock-free queue for deferred snapshot deletion
 * 
 * The audio thread pushes old snapshot pointers here.
 * The engine thread pops and deletes them.
 * 
 * This ensures that memory deallocation NEVER happens on the audio thread.
 * 
 * Implementation: Simple SPSC (Single Producer Single Consumer) ring buffer.
 * Producer: Audio thread (pushes old snapshots)
 * Consumer: Engine thread (pops and deletes)
 */
template<size_t Capacity = 16>
class SnapshotTrashQueue {
public:
    SnapshotTrashQueue() : m_writeIndex(0), m_readIndex(0) {
        for (size_t i = 0; i < Capacity; ++i) {
            m_buffer[i] = nullptr;
        }
    }
    
    ~SnapshotTrashQueue() {
        // Clean up any remaining snapshots
        PlaylistRuntimeSnapshot* ptr = nullptr;
        while (pop(ptr)) {
            delete ptr;
        }
    }
    
    /**
     * @brief Push a snapshot pointer to be deleted later
     * 
     * Called from audio thread. Lock-free, wait-free.
     * @return false if queue is full (should not happen with proper sizing)
     */
    bool push(PlaylistRuntimeSnapshot* snapshot) {
        size_t writeIdx = m_writeIndex.load(std::memory_order_relaxed);
        size_t nextWrite = (writeIdx + 1) % Capacity;
        
        if (nextWrite == m_readIndex.load(std::memory_order_acquire)) {
            return false; // Queue full
        }
        
        m_buffer[writeIdx] = snapshot;
        m_writeIndex.store(nextWrite, std::memory_order_release);
        return true;
    }
    
    /**
     * @brief Pop a snapshot pointer for deletion
     * 
     * Called from engine thread.
     * @return false if queue is empty
     */
    bool pop(PlaylistRuntimeSnapshot*& snapshot) {
        size_t readIdx = m_readIndex.load(std::memory_order_relaxed);
        
        if (readIdx == m_writeIndex.load(std::memory_order_acquire)) {
            return false; // Queue empty
        }
        
        snapshot = m_buffer[readIdx];
        m_buffer[readIdx] = nullptr;
        m_readIndex.store((readIdx + 1) % Capacity, std::memory_order_release);
        return true;
    }
    
    /**
     * @brief Delete all pending snapshots
     * 
     * Called from engine thread during update loop.
     */
    void collectGarbage() {
        PlaylistRuntimeSnapshot* snapshot = nullptr;
        while (pop(snapshot)) {
            delete snapshot;
        }
    }

private:
    PlaylistRuntimeSnapshot* m_buffer[Capacity];
    std::atomic<size_t> m_writeIndex;
    std::atomic<size_t> m_readIndex;
};

// =============================================================================
// PlaylistEngine - Bridge between model and RT thread
// =============================================================================

/**
 * @brief Manages the snapshot exchange between threads
 * 
 * Responsibilities:
 * - Holds the current snapshot pointer for audio thread
 * - Handles atomic swap when model changes
 * - Manages the trash queue for safe deletion
 * 
 * Usage pattern:
 * 1. Engine thread calls pushSnapshot() when model changes
 * 2. Audio thread calls getCurrentSnapshot() in processBlock
 * 3. Engine thread calls collectGarbage() periodically
 */
class PlaylistSnapshotManager {
public:
    PlaylistSnapshotManager() : m_currentSnapshot(nullptr), m_pendingSnapshot(nullptr) {}
    
    ~PlaylistSnapshotManager() {
        // Clean up
        delete m_currentSnapshot.load();
        delete m_pendingSnapshot.load();
        m_trashQueue.collectGarbage();
    }
    
    /**
     * @brief Push a new snapshot to be picked up by audio thread
     * 
     * Called from engine thread when playlist model changes.
     * The old pending snapshot (if any) goes to trash.
     */
    void pushSnapshot(std::unique_ptr<PlaylistRuntimeSnapshot> snapshot) {
        PlaylistRuntimeSnapshot* newPtr = snapshot.release();
        PlaylistRuntimeSnapshot* oldPending = m_pendingSnapshot.exchange(newPtr, 
            std::memory_order_acq_rel);
        
        if (oldPending) {
            // Old pending snapshot was never picked up, trash it
            m_trashQueue.push(oldPending);
        }
    }
    
    /**
     * @brief Get current snapshot for audio processing
     * 
     * Called from audio thread at start of processBlock.
     * Automatically picks up any pending snapshot.
     * 
     * @return Current snapshot (may be nullptr if none set)
     */
    const PlaylistRuntimeSnapshot* getCurrentSnapshot() {
        // Check for pending update
        PlaylistRuntimeSnapshot* pending = m_pendingSnapshot.exchange(nullptr, 
            std::memory_order_acq_rel);
        
        if (pending) {
            // Swap in new snapshot, push old to trash
            PlaylistRuntimeSnapshot* old = m_currentSnapshot.exchange(pending, 
                std::memory_order_acq_rel);
            if (old) {
                m_trashQueue.push(old);
            }
        }
        
        return m_currentSnapshot.load(std::memory_order_acquire);
    }
    
    /**
     * @brief Collect garbage (delete old snapshots)
     * 
     * Called from engine thread periodically (e.g., in update loop).
     * Safe to call frequently.
     */
    void collectGarbage() {
        m_trashQueue.collectGarbage();
    }
    
    /**
     * @brief Check if there's a pending snapshot waiting
     */
    bool hasPendingSnapshot() const {
        return m_pendingSnapshot.load(std::memory_order_acquire) != nullptr;
    }

private:
    std::atomic<PlaylistRuntimeSnapshot*> m_currentSnapshot;
    std::atomic<PlaylistRuntimeSnapshot*> m_pendingSnapshot;
    SnapshotTrashQueue<16> m_trashQueue;
};

} // namespace Audio
} // namespace Nomad
