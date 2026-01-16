// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <memory>
#include <vector>
#include <string>
#include <atomic>
#include <random>
#include <cstdint>

namespace Nomad {
namespace Audio {

// Forward declarations
class SamplePool;
struct SampleData;

/**
 * @brief Simple UUID implementation for stable clip identity
 */
struct ClipUUID {
    uint64_t high = 0;
    uint64_t low = 0;
    
    bool isValid() const { return high != 0 || low != 0; }
    bool operator==(const ClipUUID& other) const { return high == other.high && low == other.low; }
    bool operator!=(const ClipUUID& other) const { return !(*this == other); }
    bool operator<(const ClipUUID& other) const { 
        return high < other.high || (high == other.high && low < other.low); 
    }
    
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
    
    static ClipUUID generate() {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint64_t> dis;
        
        ClipUUID uuid;
        uuid.high = dis(gen);
        uuid.low = dis(gen);
        // Set version 4 (random) and variant bits
        uuid.high = (uuid.high & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
        uuid.low = (uuid.low & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
        return uuid;
    }
    
    static ClipUUID fromString(const std::string& str) {
        ClipUUID uuid;
        if (str.length() >= 36) {
            unsigned int a, b, c, d;
            unsigned long long e;
            if (sscanf(str.c_str(), "%8x-%4x-%4x-%4x-%12llx", &a, &b, &c, &d, &e) == 5) {
                uuid.high = (static_cast<uint64_t>(a) << 32) | 
                           (static_cast<uint64_t>(b) << 16) | 
                           static_cast<uint64_t>(c);
                uuid.low = (static_cast<uint64_t>(d) << 48) | e;
            }
        }
        return uuid;
    }
};

/**
 * @brief Audio Clip - represents a piece of audio on the timeline
 *
 * A Clip is a non-destructive reference to audio data with:
 * - Position on timeline (where it starts)
 * - Trim start/end (which portion of the source audio to use)
 * - Gain and other properties
 * 
 * Multiple clips can reference the same source audio data.
 * Clips exist within a PlaylistTrack (lane on the timeline).
 */
class AudioClip {
public:
    /**
     * @brief Construct a new clip
     * @param name Display name for the clip
     */
    explicit AudioClip(const std::string& name = "Clip");
    
    /**
     * @brief Construct a clip from audio data
     * @param audioData The audio sample data
     * @param numSamples Number of samples per channel
     * @param sampleRate Sample rate in Hz
     * @param numChannels Number of channels (1=mono, 2=stereo)
     * @param name Display name
     */
    AudioClip(const float* audioData, uint32_t numSamples, uint32_t sampleRate, 
              uint32_t numChannels, const std::string& name = "Clip");
    
    ~AudioClip() = default;
    
    // === IDENTITY ===
    const ClipUUID& getUUID() const { return m_uuid; }
    void setUUID(const ClipUUID& uuid) { m_uuid = uuid; }  // Only for deserialization
    
    // === PROPERTIES ===
    void setName(const std::string& name) { m_name = name; }
    const std::string& getName() const { return m_name; }
    
    void setColor(uint32_t color) { m_color = color; }  // ARGB format
    uint32_t getColor() const { return m_color; }
    
    // === TIMELINE POSITION ===
    /** Where this clip starts on the timeline (in seconds) */
    void setStartTime(double seconds) { m_startTime = seconds; }
    double getStartTime() const { return m_startTime; }
    
    /** Where this clip ends on the timeline (derived from start + duration) */
    double getEndTime() const { return m_startTime + getTrimmedDuration(); }
    
    // === AUDIO DATA ===
    void setAudioData(const float* data, uint32_t numSamples, uint32_t sampleRate, uint32_t numChannels);
    void clearAudioData();
    
    const std::vector<float>& getAudioData() const { return m_audioData; }
    uint32_t getSampleRate() const { return m_sampleRate; }
    uint32_t getNumChannels() const { return m_numChannels; }
    
    /** Full duration of the source audio (before trimming) */
    double getSourceDuration() const;
    
    // === NON-DESTRUCTIVE TRIMMING ===
    /** Set where playback begins within the source audio (in seconds) */
    void setTrimStart(double seconds);
    double getTrimStart() const { return m_trimStart; }
    
    /** Set where playback ends within the source audio (in seconds) */
    void setTrimEnd(double seconds);
    double getTrimEnd() const { return m_trimEnd; }
    
    /** Duration after trimming is applied */
    double getTrimmedDuration() const;
    
    /** Reset trim to use full source audio */
    void resetTrim();
    
    // === CLIP GAIN ===
    void setGain(float gain) { m_gain = gain; }  // Linear gain (1.0 = unity)
    float getGain() const { return m_gain; }
    
    // === SOURCE PATH (for reloading/display) ===
    void setSourcePath(const std::string& path) { m_sourcePath = path; }
    const std::string& getSourcePath() const { return m_sourcePath; }
    
    // === OPERATIONS ===
    /**
     * @brief Split this clip at a position
     * @param positionInClip Position within the clip (relative to trim start) in seconds
     * @return New clip containing the second half, or nullptr if split position invalid
     * 
     * After splitting:
     * - This clip is trimmed to end at the split point
     * - New clip starts at the split point on timeline and contains the remainder
     */
    std::shared_ptr<AudioClip> splitAt(double positionInClip);
    
    /**
     * @brief Create a duplicate of this clip
     * @return New clip with same audio data but new UUID
     */
    std::shared_ptr<AudioClip> duplicate() const;
    
    /**
     * @brief Check if a timeline position falls within this clip
     * @param timelinePosition Position on timeline in seconds
     * @return true if the position is within clip bounds
     */
    bool containsTimelinePosition(double timelinePosition) const;
    
    /**
     * @brief Convert timeline position to position within source audio
     * @param timelinePosition Position on timeline in seconds
     * @return Position within source audio (accounting for trim), or -1 if outside clip
     */
    double timelineToSourcePosition(double timelinePosition) const;
    
    // === AUDIO PROCESSING ===
    /**
     * @brief Process audio for this clip at a given timeline position
     * @param outputBuffer Output buffer to write to (interleaved stereo)
     * @param numFrames Number of frames to process
     * @param timelinePosition Current playhead position in seconds
     * @param outputSampleRate Target sample rate
     */
    void processAudio(float* outputBuffer, uint32_t numFrames, double timelinePosition, double outputSampleRate);

private:
    // Identity
    ClipUUID m_uuid;
    
    // Properties
    std::string m_name;
    uint32_t m_color = 0xFF4A90D9;  // Default blue color
    
    // Timeline position
    double m_startTime = 0.0;
    
    // Audio data (owned by this clip - could be changed to shared pool reference)
    std::vector<float> m_audioData;
    uint32_t m_sampleRate = 44100;
    uint32_t m_numChannels = 2;
    
    // Non-destructive trim (in seconds from start of source audio)
    double m_trimStart = 0.0;
    double m_trimEnd = 0.0;  // 0 means use full length
    
    // Clip gain
    float m_gain = 1.0f;
    
    // Source file path
    std::string m_sourcePath;
};

} // namespace Audio
} // namespace Nomad
