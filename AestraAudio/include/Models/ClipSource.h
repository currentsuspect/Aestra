#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
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
 *
 * Samples live in exactly one of three storages. Decoded audio is float32 (4 bytes per sample), which
 * is most of a session's memory: a 4-minute stereo track is ~92 MB. When a buffer's samples are
 * exactly the values of 16- or 24-bit PCM (a 16/24-bit WAV or FLAC), tryCompact() keeps them as that
 * PCM (2 or 3 bytes per sample) and everything reads them back through copyFloats(). The conversion
 * is exact, so what the engine plays is bit-identical; a buffer that is not exactly representable
 * (MP3, a rendered float result, anything processed) stays float32 untouched.
 */
struct AudioBufferData {
    enum class Storage : uint8_t { Float32, Pcm16, Pcm24 };

    std::vector<float> interleavedData; ///< Float32 storage; EMPTY once the buffer is compact.
    std::vector<int16_t> pcm16;         ///< Storage::Pcm16
    std::vector<uint8_t> pcm24;         ///< Storage::Pcm24, little-endian, 3 bytes per sample
    Storage storage{Storage::Float32};
    uint32_t sampleRate{44100};
    uint32_t numChannels{2};
    uint64_t numFrames{0};

    double durationSeconds() const {
        if (sampleRate == 0 || numChannels == 0)
            return 0.0;
        return static_cast<double>(numFrames) / static_cast<double>(sampleRate);
    }

    bool isCompact() const { return storage != Storage::Float32; }

    /// Samples held (frames x channels), whichever storage holds them.
    uint64_t storedSamples() const {
        switch (storage) {
        case Storage::Pcm16:
            return pcm16.size();
        case Storage::Pcm24:
            return pcm24.size() / 3;
        default:
            return interleavedData.size();
        }
    }

    /// Bytes of sample memory this buffer holds.
    uint64_t residentBytes() const {
        return interleavedData.size() * sizeof(float) + pcm16.size() * sizeof(int16_t) + pcm24.size();
    }

    bool isValid() const { return storedSamples() > 0 && sampleRate > 0 && numChannels > 0; }

    /// One sample as float, by interleaved index. For occasional reads; use copyFloats() in loops.
    float sampleAt(uint64_t index) const {
        switch (storage) {
        case Storage::Pcm16:
            return index < pcm16.size() ? static_cast<float>(pcm16[index]) * (1.0f / 32768.0f) : 0.0f;
        case Storage::Pcm24:
            return index < pcm24.size() / 3 ? pcm24ToFloat(&pcm24[index * 3]) : 0.0f;
        default:
            return index < interleavedData.size() ? interleavedData[index] : 0.0f;
        }
    }

    /// Copy @p count interleaved samples starting at @p firstSample as float. Out-of-range samples are 0.
    /// Allocation-free (callable from the audio thread).
    void copyFloats(uint64_t firstSample, uint64_t count, float* out) const {
        const uint64_t total = storedSamples();
        const uint64_t avail = firstSample < total ? std::min<uint64_t>(count, total - firstSample) : 0;
        switch (storage) {
        case Storage::Pcm16: {
            const int16_t* src = pcm16.data() + firstSample;
            for (uint64_t i = 0; i < avail; ++i)
                out[i] = static_cast<float>(src[i]) * (1.0f / 32768.0f);
            break;
        }
        case Storage::Pcm24: {
            const uint8_t* src = pcm24.data() + firstSample * 3;
            for (uint64_t i = 0; i < avail; ++i)
                out[i] = pcm24ToFloat(src + i * 3);
            break;
        }
        default:
            if (avail > 0)
                std::memcpy(out, interleavedData.data() + firstSample, avail * sizeof(float));
            break;
        }
        for (uint64_t i = avail; i < count; ++i)
            out[i] = 0.0f;
    }

    /// Every sample as float (a copy). Large for long audio: prefer copyFloats() over a window.
    std::vector<float> toFloat() const {
        if (!isCompact())
            return interleavedData;
        std::vector<float> all(static_cast<size_t>(storedSamples()));
        copyFloats(0, all.size(), all.data());
        return all;
    }

    /// Largest finite |sample| in the buffer (0 when empty). Walks compact storage in small slices, so it
    /// never builds a whole-file float copy.
    float peakAbs() const {
        float peak = 0.0f;
        if (!isCompact()) {
            for (const float v : interleavedData)
                if (std::isfinite(v))
                    peak = std::max(peak, std::abs(v));
            return peak;
        }
        float slice[4096];
        const uint64_t total = storedSamples();
        for (uint64_t at = 0; at < total; at += 4096) {
            const uint64_t n = std::min<uint64_t>(4096, total - at);
            copyFloats(at, n, slice);
            for (uint64_t i = 0; i < n; ++i)
                peak = std::max(peak, std::abs(slice[i])); // PCM values are always finite
        }
        return peak;
    }

    /// Becomes float32 again (e.g. before an in-place edit). No-op if already float.
    void expandToFloat() {
        if (!isCompact())
            return;
        interleavedData = toFloat();
        std::vector<int16_t>().swap(pcm16);
        std::vector<uint8_t>().swap(pcm24);
        storage = Storage::Float32;
    }

    /**
     * @brief Keep the samples as 16- or 24-bit PCM when that loses nothing.
     * @return true if the buffer is now compact. Verified sample by sample: any value that is not exactly
     *         k / 2^15 (or k / 2^23) with an integer k in range, including NaN and anything over full
     *         scale, leaves the buffer float32 and unchanged. Main/import thread only.
     */
    bool tryCompact() {
        if (isCompact())
            return true;
        const size_t n = interleavedData.size();
        if (n == 0)
            return false;
        const float* f = interleavedData.data();

        // Exactness test per sample: x * 2^bits is an exact integer in range. Scaling by a power of two is
        // exact in float32, so no rounding is involved; the loop is branch-free per block so it vectorizes,
        // and a failing file is rejected within its first block.
        const auto fitsGrid = [&](float scale, int32_t lo, int32_t hi) {
            constexpr size_t kBlock = 4096;
            for (size_t at = 0; at < n; at += kBlock) {
                const size_t end = std::min(n, at + kBlock);
                bool ok = true;
                for (size_t i = at; i < end; ++i) {
                    const float t = f[i] * scale;
                    const int32_t k = static_cast<int32_t>(t);
                    ok &= (static_cast<float>(k) == t) & (k >= lo) & (k <= hi);
                }
                if (!ok)
                    return false;
            }
            return true;
        };

        if (fitsGrid(32768.0f, -32768, 32767)) {
            std::vector<int16_t> packed(n);
            for (size_t i = 0; i < n; ++i)
                packed[i] = static_cast<int16_t>(static_cast<int32_t>(f[i] * 32768.0f));
            pcm16 = std::move(packed);
            std::vector<float>().swap(interleavedData);
            storage = Storage::Pcm16;
            return true;
        }

        if (!fitsGrid(8388608.0f, -8388608, 8388607))
            return false;
        std::vector<uint8_t> packed(n * 3);
        for (size_t i = 0; i < n; ++i) {
            const int32_t k = static_cast<int32_t>(f[i] * 8388608.0f);
            packed[i * 3] = static_cast<uint8_t>(k & 0xFF);
            packed[i * 3 + 1] = static_cast<uint8_t>((k >> 8) & 0xFF);
            packed[i * 3 + 2] = static_cast<uint8_t>((k >> 16) & 0xFF);
        }
        pcm24 = std::move(packed);
        std::vector<float>().swap(interleavedData);
        storage = Storage::Pcm24;
        return true;
    }

private:
    static float pcm24ToFloat(const uint8_t* p) {
        int32_t k = static_cast<int32_t>(p[0]) | (static_cast<int32_t>(p[1]) << 8) | (static_cast<int32_t>(p[2]) << 16);
        k = (k << 8) >> 8; // sign-extend 24 -> 32
        return static_cast<float>(k) * (1.0f / 8388608.0f);
    }
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
