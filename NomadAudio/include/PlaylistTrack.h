// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AudioClip.h"
#include <memory>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <random>

namespace Nomad {
namespace Audio {

/**
 * @brief UUID for PlaylistTrack identity
 */
struct PlaylistTrackUUID {
    uint64_t high = 0;
    uint64_t low = 0;
    
    bool isValid() const { return high != 0 || low != 0; }
    bool operator==(const PlaylistTrackUUID& other) const { return high == other.high && low == other.low; }
    bool operator!=(const PlaylistTrackUUID& other) const { return !(*this == other); }
    
    std::string toString() const {
        char buf[37];
        snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%012llx",
                 static_cast<uint32_t>(high >> 32),
                 static_cast<uint16_t>(high >> 16),
                 static_cast<uint16_t>(high),
                 static_cast<uint16_t>(low >> 48),
                 low & 0xFFFFFFFFFFFFULL);
        return std::string(buf);
    }
    
    static PlaylistTrackUUID generate() {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint64_t> dis;
        
        PlaylistTrackUUID uuid;
        uuid.high = dis(gen);
        uuid.low = dis(gen);
        uuid.high = (uuid.high & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
        uuid.low = (uuid.low & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
        return uuid;
    }
};

/**
 * @brief Playlist Track - a lane on the timeline that can contain multiple clips
 *
 * This represents a horizontal "lane" or "track" in the playlist view.
 * Each PlaylistTrack can contain multiple AudioClips at different positions.
 * 
 * The PlaylistTrack has its own properties (name, color, volume, pan, mute, solo)
 * that affect all clips within it.
 */
class PlaylistTrack {
public:
    /**
     * @brief Construct a new PlaylistTrack
     * @param name Display name for the track
     * @param trackIndex Visual index (for display ordering)
     */
    explicit PlaylistTrack(const std::string& name = "Track", uint32_t trackIndex = 0);
    ~PlaylistTrack() = default;
    
    // === IDENTITY ===
    const PlaylistTrackUUID& getUUID() const { return m_uuid; }
    void setUUID(const PlaylistTrackUUID& uuid) { m_uuid = uuid; }
    
    // === PROPERTIES ===
    void setName(const std::string& name) { m_name = name; }
    const std::string& getName() const { return m_name; }
    
    void setColor(uint32_t color) { m_color = color; }
    uint32_t getColor() const { return m_color; }
    
    uint32_t getTrackIndex() const { return m_trackIndex; }
    void setTrackIndex(uint32_t index) { m_trackIndex = index; }
    
    // === AUDIO PARAMETERS ===
    void setVolume(float volume) { m_volume.store(std::clamp(volume, 0.0f, 2.0f)); }
    float getVolume() const { return m_volume.load(); }
    
    void setPan(float pan) { m_pan.store(std::clamp(pan, -1.0f, 1.0f)); }
    float getPan() const { return m_pan.load(); }
    
    void setMute(bool mute) { m_muted.store(mute); }
    bool isMuted() const { return m_muted.load(); }
    
    void setSolo(bool solo) { m_soloed.store(solo); }
    bool isSoloed() const { return m_soloed.load(); }
    
    // === CLIP MANAGEMENT ===
    /**
     * @brief Add a clip to this track
     * @param clip The clip to add
     */
    void addClip(std::shared_ptr<AudioClip> clip);
    
    /**
     * @brief Remove a clip from this track
     * @param clip The clip to remove
     * @return true if clip was found and removed
     */
    bool removeClip(std::shared_ptr<AudioClip> clip);
    
    /**
     * @brief Remove a clip by UUID
     * @param uuid The UUID of the clip to remove
     * @return true if clip was found and removed
     */
    bool removeClip(const ClipUUID& uuid);
    
    /**
     * @brief Get all clips in this track
     * @return Vector of clips (sorted by start time)
     */
    const std::vector<std::shared_ptr<AudioClip>>& getClips() const { return m_clips; }
    
    /**
     * @brief Get number of clips
     */
    size_t getClipCount() const { return m_clips.size(); }
    
    /**
     * @brief Find clip at a specific timeline position
     * @param timelinePosition Position in seconds
     * @return Clip at that position, or nullptr if none
     */
    std::shared_ptr<AudioClip> getClipAtPosition(double timelinePosition) const;
    
    /**
     * @brief Find clip by UUID
     * @param uuid The UUID to search for
     * @return The clip, or nullptr if not found
     */
    std::shared_ptr<AudioClip> getClipByUUID(const ClipUUID& uuid) const;
    
    /**
     * @brief Get clip by index
     * @param index Index in the clips array
     * @return The clip, or nullptr if index out of range
     */
    std::shared_ptr<AudioClip> getClip(size_t index) const;
    
    /**
     * @brief Split a clip at a given timeline position
     * @param timelinePosition Position on the timeline to split at
     * @return The new clip (second half), or nullptr if no clip at position
     */
    std::shared_ptr<AudioClip> splitClipAt(double timelinePosition);
    
    /**
     * @brief Clear all clips from this track
     */
    void clearClips();
    
    // === TRACK QUERIES ===
    /**
     * @brief Check if track has any clips
     */
    bool isEmpty() const { return m_clips.empty(); }
    
    /**
     * @brief Get the total duration (end of last clip)
     */
    double getTotalDuration() const;
    
    /**
     * @brief Get the earliest clip start time
     */
    double getEarliestStartTime() const;
    
    // === AUDIO PROCESSING ===
    /**
     * @brief Process audio for all clips in this track
     * @param outputBuffer Output buffer (interleaved stereo)
     * @param numFrames Number of frames to process
     * @param timelinePosition Current playhead position in seconds
     * @param outputSampleRate Target sample rate
     */
    void processAudio(float* outputBuffer, uint32_t numFrames, double timelinePosition, double outputSampleRate);
    
    // === SYSTEM TRACK FLAG ===
    void setSystemTrack(bool isSystem) { m_isSystemTrack = isSystem; }
    bool isSystemTrack() const { return m_isSystemTrack; }

private:
    // Sort clips by start time
    void sortClips();
    
    // Identity
    PlaylistTrackUUID m_uuid;
    
    // Properties
    std::string m_name;
    uint32_t m_color = 0xFF4A90D9;
    uint32_t m_trackIndex = 0;
    
    // Audio parameters
    std::atomic<float> m_volume{1.0f};
    std::atomic<float> m_pan{0.0f};
    std::atomic<bool> m_muted{false};
    std::atomic<bool> m_soloed{false};
    
    // Clips in this track
    std::vector<std::shared_ptr<AudioClip>> m_clips;
    mutable std::mutex m_clipMutex;
    
    // Scratch buffer for mixing
    std::vector<float> m_scratchBuffer;
    
    // System track flag
    bool m_isSystemTrack = false;
};

} // namespace Audio
} // namespace Nomad
