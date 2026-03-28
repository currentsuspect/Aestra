#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

// Forward declaration
struct AudioBufferData;
class WaveformCache;

/**
 * @brief Type-safe identifier for clip sources
 */
struct ClipSourceID {
    uint64_t value{0};
    ClipSourceID() = default;
    explicit ClipSourceID(uint64_t v) : value(v) {}
    explicit operator uint64_t() const { return value; }
    bool operator==(const ClipSourceID& other) const { return value == other.value; }
    bool operator!=(const ClipSourceID& other) const { return value != other.value; }
    bool isValid() const { return value != 0; }
};

/**
 * @brief Audio buffer data for clip sources
 */
struct AudioBufferData {
    std::vector<float> interleavedData;
    uint32_t sampleRate{44100};
    uint32_t numChannels{2};
    uint64_t numFrames{0};

    double durationSeconds() const {
        if (sampleRate == 0 || numChannels == 0)
            return 0.0;
        return static_cast<double>(numFrames) / static_cast<double>(sampleRate);
    }

    bool isValid() const { return !interleavedData.empty() && sampleRate > 0 && numChannels > 0; }
};

/**
 * @brief Audio source for clips
 */
class ClipSource {
public:
    ClipSource() = default;
    explicit ClipSource(ClipSourceID id, const std::string& name = "") : m_id(id), m_name(name) {}

    // Getters
    const std::string& getName() const { return m_name; }

    double getDurationSeconds() const { return m_buffer ? m_buffer->durationSeconds() : 0.0; }
    uint64_t getNumFrames() const { return m_buffer ? m_buffer->numFrames : 0; }
    uint32_t getSampleRate() const { return m_buffer ? m_buffer->sampleRate : 0; }
    uint32_t getNumChannels() const { return m_buffer ? m_buffer->numChannels : 0; }

    const AudioBufferData* getRawBuffer() const { return m_buffer.get(); }

    std::shared_ptr<AudioBufferData> getBuffer() const { return m_buffer; }

    const std::string& getFilePath() const { return m_filePath; }

    ClipSourceID getID() const { return m_id; }
    std::shared_ptr<WaveformCache> getWaveformCache() const { return m_waveformCache; }

    bool isValid() const { return m_buffer && m_buffer->isValid(); }

    bool isReady() const { return isValid(); }

    // Setters
    void setFilePath(const std::string& path) { m_filePath = path; }

    void setBuffer(std::shared_ptr<AudioBufferData> buffer) { m_buffer = std::move(buffer); }
    void setWaveformCache(std::shared_ptr<WaveformCache> cache) { m_waveformCache = std::move(cache); }

private:
    ClipSourceID m_id;
    std::string m_name;
    std::string m_filePath;
    std::shared_ptr<AudioBufferData> m_buffer;
    std::shared_ptr<WaveformCache> m_waveformCache;
};

} // namespace Audio
} // namespace Aestra
