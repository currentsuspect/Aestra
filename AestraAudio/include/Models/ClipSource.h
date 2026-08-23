#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

// Forward declarations
struct AudioBufferData;
class WaveformCache;
class SourceManager;

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

    std::shared_ptr<const AudioBufferData> getSharedBuffer() const { return m_buffer; }

    std::shared_ptr<AudioBufferData> getBuffer() const { return m_buffer; }

    const std::string& getFilePath() const { return m_filePath; }

    ClipSourceID getID() const { return m_id; }

    uint64_t getContentRevision() const { return m_contentRevision; }

    bool isValid() const { return m_buffer && m_buffer->isValid(); }

    bool isReady() const { return isValid(); }

    // Setters
    void setName(const std::string& name) { m_name = name; }
    void setFilePath(const std::string& path) { m_filePath = path; }

    // Defined in ClipSource.cpp (it needs the complete SourceManager type, and
    // SourceManager.h already includes this header): attaching audio that flips
    // the source from not-ready to ready must bump the owning SourceManager's
    // revision so the timeline's waveform-cache sweep re-runs.
    void setBuffer(std::shared_ptr<AudioBufferData> buffer);

    std::shared_ptr<WaveformCache> getWaveformCache() const { return m_waveformCache; }
    void setWaveformCache(std::shared_ptr<WaveformCache> cache) { m_waveformCache = std::move(cache); }

    // ---- Anti-aliased filtered variant (Phase 4, F1) -------------------------
    // One optional low-pass-filtered copy of m_buffer, valid for exactly one
    // (contentRevision, targetRate, filter spec) key. Produced off the audio
    // thread by ClipPrefilterService; selected by PlaylistModel's runtime
    // snapshot when a clip is DOWNSAMPLED into the session.
    // THREADING CONTRACT: these fields are accessed ONLY on the model thread —
    // the thread that mutates TrackManager state and pumps graph rebuilds (the
    // app's main thread). That covers all three touch points: snapshot reads and
    // ensureClipPrefilters writes (graph build) plus the clearFilteredVariant()
    // in setBuffer above (import/project-load/record, same thread). The prefilter
    // WORKER never touches ClipSource. No lock needed under this contract.

    /// Filtered copy valid for `targetRate` and the CURRENT buffer content, or null.
    std::shared_ptr<const AudioBufferData> getFilteredBufferFor(uint32_t targetRate) const {
        if (m_filteredBuffer && m_filteredForRate == targetRate &&
            m_filteredContentRevision == m_contentRevision) {
            return m_filteredBuffer;
        }
        return nullptr;
    }

    void setFilteredVariant(std::shared_ptr<AudioBufferData> filtered, uint32_t targetRate,
                            uint64_t contentRevision, uint32_t specVersion) {
        m_filteredBuffer = std::move(filtered);
        m_filteredForRate = targetRate;
        m_filteredContentRevision = contentRevision;
        m_filteredSpecVersion = specVersion;
    }

    void clearFilteredVariant() {
        m_filteredBuffer.reset();
        m_filteredForRate = 0;
        m_filteredContentRevision = 0;
        m_filteredSpecVersion = 0;
    }

    uint32_t filteredVariantRate() const { return m_filteredForRate; }
    uint32_t filteredVariantSpec() const { return m_filteredSpecVersion; }
    bool hasFilteredVariant() const { return m_filteredBuffer != nullptr; }

private:
    // Owning manager, bound at creation time inside SourceManager. Null for
    // sources minted elsewhere (e.g. AuditionEngine's standalone preview
    // source) — those have no sweep to wake, so a null owner simply skips the
    // notification in setBuffer. Friendship rather than a public setter:
    // ownership is a creation-time fact, not something callers rewire.
    friend class SourceManager;

    void setOwner(SourceManager* owner) { m_owner = owner; }

    ClipSourceID m_id;
    std::string m_name;
    std::string m_filePath;
    std::shared_ptr<AudioBufferData> m_buffer;
    std::shared_ptr<WaveformCache> m_waveformCache;
    uint64_t m_contentRevision{0};
    SourceManager* m_owner{nullptr};

    // Filtered-variant slot (see threading contract above).
    std::shared_ptr<AudioBufferData> m_filteredBuffer;
    uint32_t m_filteredForRate{0};
    uint64_t m_filteredContentRevision{0};
    uint32_t m_filteredSpecVersion{0};
};

} // namespace Audio
} // namespace Aestra
