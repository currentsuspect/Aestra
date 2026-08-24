// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "WaveformCache.h"

#include "AestraLog.h"
#include "WaveformSIMD.h"

#include <algorithm>
#include <cmath>
#include <shared_mutex>
#include <thread>

namespace Aestra {
namespace Audio {

// =============================================================================
// WaveformCache Implementation
// =============================================================================

WaveformCache::WaveformCache() = default;

WaveformCache::~WaveformCache() = default;

void WaveformCache::buildFromBuffer(const AudioBufferData& buffer, uint32_t baseSamplesPerPeak, uint32_t numLevels) {
    if (!buffer.isValid()) {
        Log::warning("WaveformCache: Cannot build from invalid buffer");
        return;
    }

    buildFromRaw(buffer.interleavedData.data(), buffer.numFrames, buffer.numChannels, baseSamplesPerPeak, numLevels);
}

void WaveformCache::buildFromRaw(const float* data, SampleIndex numFrames, uint32_t numChannels,
                                 uint32_t baseSamplesPerPeak, uint32_t numLevels) {
    if (!data || numFrames <= 0 || numChannels == 0) {
        Log::warning("WaveformCache: Invalid parameters for build");
        return;
    }

    // Shadow Build Strategy:
    // Build levels locally without holding the lock to prevent UI blocking.
    std::vector<WaveformMipLevel> localLevels;
    localLevels.resize(numLevels);

    // Build first level from raw data
    buildLevel(data, numFrames, numChannels, baseSamplesPerPeak, localLevels[0]);

    // Build subsequent levels from previous level
    for (uint32_t i = 1; i < numLevels; ++i) {
        buildNextLevel(localLevels[i - 1], localLevels[i]);
    }

    // Critical Section: atomic swap
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        std::swap(m_levels, localLevels);
        m_numChannels = numChannels;
        m_sourceFrames = numFrames;
        m_ready.store(true, std::memory_order_release);
    }
    // localLevels now holds the OLD data and is destroyed here (outside lock)

}

void WaveformCache::buildLevel(const float* data, SampleIndex numFrames, uint32_t numChannels, uint32_t samplesPerPeak,
                               WaveformMipLevel& outLevel) {
    outLevel.samplesPerPeak = samplesPerPeak;
    outLevel.numChannels = numChannels;
    outLevel.numPeaks = (numFrames + samplesPerPeak - 1) / samplesPerPeak;

    outLevel.peaks.resize(static_cast<size_t>(outLevel.numPeaks * numChannels));

    for (SampleIndex peakIdx = 0; peakIdx < outLevel.numPeaks; ++peakIdx) {
        SampleIndex startFrame = peakIdx * samplesPerPeak;
        SampleIndex endFrame = std::min(startFrame + samplesPerPeak, numFrames);
        uint32_t count = static_cast<uint32_t>(endFrame - startFrame);

        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            float minVal, maxVal;
            double sumSq = 0.0;

            WaveformSIMD::minMaxRMSChannel(data, numFrames, numChannels, ch, startFrame, endFrame, minVal, maxVal, sumSq);

            float rms = (count > 0) ? static_cast<float>(std::sqrt(sumSq / static_cast<double>(count))) : 0.0f;
            size_t idx = static_cast<size_t>(peakIdx * numChannels + ch);
            outLevel.peaks[idx] = WaveformPeak(minVal, maxVal, rms, count);
            outLevel.peaks[idx].sanitize();
        }
    }
}

void WaveformCache::buildNextLevel(const WaveformMipLevel& source, WaveformMipLevel& dest) {
    dest.samplesPerPeak = source.samplesPerPeak * MIP_LEVEL_MULTIPLIER;
    dest.numChannels = source.numChannels;
    dest.numPeaks = (source.numPeaks + MIP_LEVEL_MULTIPLIER - 1) / MIP_LEVEL_MULTIPLIER;

    dest.peaks.resize(static_cast<size_t>(dest.numPeaks * dest.numChannels));

    for (SampleIndex peakIdx = 0; peakIdx < dest.numPeaks; ++peakIdx) {
        SampleIndex startSourcePeak = peakIdx * MIP_LEVEL_MULTIPLIER;
        SampleIndex endSourcePeak = std::min(startSourcePeak + MIP_LEVEL_MULTIPLIER, source.numPeaks);

        for (uint32_t ch = 0; ch < dest.numChannels; ++ch) {
            WaveformPeak merged = source.getPeak(ch, startSourcePeak);

            for (SampleIndex i = startSourcePeak + 1; i < endSourcePeak; ++i) {
                merged.merge(source.getPeak(ch, i));
            }

            size_t idx = static_cast<size_t>(peakIdx * dest.numChannels + ch);
            dest.peaks[idx] = merged;
        }
    }
}

const WaveformMipLevel* WaveformCache::getLevel(size_t levelIndex) const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    if (levelIndex < m_levels.size()) {
        return &m_levels[levelIndex];
    }
    return nullptr;
}

size_t WaveformCache::selectLevel(double samplesPerPixel) const {
    if (m_levels.empty())
        return 0;

    // Prefer coarsest level where samplesPerPeak <= samplesPerPixel,
    // capping at the finest level if zoomed in beyond LOD0.
    size_t best = 0;
    for (size_t i = 0; i < m_levels.size(); ++i) {
        if (m_levels[i].samplesPerPeak <= samplesPerPixel) {
            best = i;
        } else {
            break;
        }
    }
    return best;
}

void WaveformCache::getPeaksForRange(uint32_t channel, SampleIndex startSample, SampleIndex endSample,
                                     uint32_t numPixels, std::vector<WaveformPeak>& outPeaks) const {
    getPeaksForRangePrecise(channel, static_cast<double>(startSample), static_cast<double>(endSample), numPixels,
                            outPeaks);
}

void WaveformCache::getPeaksForRangePrecise(uint32_t channel, double startSample, double endSample,
                                            uint32_t numPixels, std::vector<WaveformPeak>& outPeaks) const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);

    outPeaks.clear();
    outPeaks.resize(numPixels);

    if (!m_ready.load(std::memory_order_acquire) || m_levels.empty() || numPixels == 0 ||
        !std::isfinite(startSample) || !std::isfinite(endSample)) {
        return;
    }

    const double sourceFrames = static_cast<double>(m_sourceFrames);
    startSample = std::clamp(startSample, 0.0, sourceFrames);
    endSample = std::clamp(endSample, 0.0, sourceFrames);
    if (channel >= m_numChannels || startSample >= endSample) {
        return;
    }

    const double samplesPerPixel = (endSample - startSample) / static_cast<double>(numPixels);
    const WaveformMipLevel& level = m_levels[selectLevel(samplesPerPixel)];
    mergePixelRange(level, channel, startSample, samplesPerPixel, numPixels, outPeaks);
}

void WaveformCache::getPeaksForRangePreciseStereo(uint32_t channelLeft, uint32_t channelRight, double startSample,
                                                  double endSample, uint32_t numPixels,
                                                  std::vector<WaveformPeak>& outLeft,
                                                  std::vector<WaveformPeak>& outRight) const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);

    outLeft.clear();
    outLeft.resize(numPixels);
    outRight.clear();
    outRight.resize(numPixels);

    if (!m_ready.load(std::memory_order_acquire) || m_levels.empty() || numPixels == 0 ||
        !std::isfinite(startSample) || !std::isfinite(endSample)) {
        return;
    }

    const double sourceFrames = static_cast<double>(m_sourceFrames);
    startSample = std::clamp(startSample, 0.0, sourceFrames);
    endSample = std::clamp(endSample, 0.0, sourceFrames);
    if (channelLeft >= m_numChannels || channelRight >= m_numChannels || startSample >= endSample) {
        return;
    }

    const double samplesPerPixel = (endSample - startSample) / static_cast<double>(numPixels);
    const WaveformMipLevel& level = m_levels[selectLevel(samplesPerPixel)];
    mergePixelRange(level, channelLeft, startSample, samplesPerPixel, numPixels, outLeft);
    mergePixelRange(level, channelRight, startSample, samplesPerPixel, numPixels, outRight);
}

void WaveformCache::mergePixelRange(const WaveformMipLevel& level, uint32_t channel, double startSample,
                                    double samplesPerPixel, uint32_t numPixels,
                                    std::vector<WaveformPeak>& outPeaks) const {
    // Raw-pointer walk over the level's interleaved peak storage. Same math as
    // getPeakRange(), minus the per-entry bounds check and 16-byte by-value
    // return that dominated profiles on wide clips.
    const WaveformPeak* peaks = level.peaks.data();
    const size_t stride = level.numChannels;
    const size_t ch = channel;

    for (uint32_t pixel = 0; pixel < numPixels; ++pixel) {
        const double pixelStart = startSample + static_cast<double>(pixel) * samplesPerPixel;
        const double pixelEnd = startSample + static_cast<double>(pixel + 1) * samplesPerPixel;
        SampleIndex startPeak = static_cast<SampleIndex>(std::floor(pixelStart / level.samplesPerPeak));
        SampleIndex endPeak = static_cast<SampleIndex>(std::ceil(pixelEnd / level.samplesPerPeak));
        endPeak = std::max(endPeak, startPeak + 1);

        SampleIndex s = std::max<SampleIndex>(startPeak, 0);
        SampleIndex e = std::min<SampleIndex>(endPeak, level.numPeaks);
        if (s >= e) {
            outPeaks[pixel] = WaveformPeak();
            continue;
        }

        WaveformPeak acc = peaks[static_cast<size_t>(s) * stride + ch];
        for (SampleIndex i = s + 1; i < e; ++i) {
            acc.merge(peaks[static_cast<size_t>(i) * stride + ch]);
        }
        outPeaks[pixel] = acc;
    }
}

WaveformPeak WaveformCache::getQuickPeak(uint32_t channel, SampleIndex startSample, SampleIndex numSamples) const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);

    if (!m_ready.load(std::memory_order_acquire) || m_levels.empty()) {
        return WaveformPeak();
    }

    if (channel >= m_numChannels || numSamples <= 0) {
        return WaveformPeak();
    }

    // Use coarsest level that still covers the range
    const WaveformMipLevel& level = m_levels.back();

    SampleIndex startPeak = startSample / level.samplesPerPeak;
    SampleIndex endPeak = (startSample + numSamples + level.samplesPerPeak - 1) / level.samplesPerPeak;

    return level.getPeakRange(channel, startPeak, endPeak);
}

void WaveformCache::clear() {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_ready.store(false, std::memory_order_release);
    m_levels.clear();
    m_numChannels = 0;
    m_sourceFrames = 0;
}

size_t WaveformCache::getMemoryUsage() const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    size_t total = 0;
    for (const auto& level : m_levels) {
        total += level.peaks.size() * sizeof(WaveformPeak);
    }
    return total;
}

// =============================================================================
// WaveformCacheBuilder Implementation
// =============================================================================

struct WaveformCacheBuilder::Impl {
    std::atomic<size_t> pendingCount{0};
    std::atomic<bool> cancelFlag{false};
};

WaveformCacheBuilder::WaveformCacheBuilder() : m_impl(std::make_shared<Impl>()) {}

WaveformCacheBuilder::~WaveformCacheBuilder() {
    cancelAll();
}

void WaveformCacheBuilder::buildAsync(const ClipSource& source, CompletionCallback callback) {
    if (!source.isReady()) {
        Log::warning("WaveformCacheBuilder: Source not ready");
        if (callback)
            callback(nullptr);
        return;
    }

    m_impl->pendingCount.fetch_add(1);

    // Capture buffer by shared_ptr for thread safety
    auto buffer = source.getBuffer();
    auto impl = m_impl; // shared_ptr copy keeps Impl alive

    std::thread([buffer, callback, impl]() {
        struct PendingDecrement {
            std::atomic<size_t>* cnt;
            explicit PendingDecrement(std::atomic<size_t>* c) : cnt(c) {}
            ~PendingDecrement() { cnt->fetch_sub(1); }
        } guard(&impl->pendingCount);

        if (impl->cancelFlag.load()) {
            if (callback)
                callback(nullptr);
            return;
        }

        auto cache = std::make_shared<WaveformCache>();
        cache->buildFromBuffer(*buffer);

        if (callback) {
            callback(cache);
        }
    }).detach();
}

std::shared_ptr<WaveformCache> WaveformCacheBuilder::buildSync(const ClipSource& source) {
    if (!source.isReady()) {
        Log::warning("WaveformCacheBuilder: Source not ready");
        return nullptr;
    }

    auto cache = std::make_shared<WaveformCache>();
    cache->buildFromBuffer(*source.getBuffer());
    return cache;
}

void WaveformCacheBuilder::cancelAll() {
    m_impl->cancelFlag.store(true);

    // Wait up to 5 seconds for pending builds to finish
    int waits = 0;
    constexpr int kMaxWaits = 5000; // 5 seconds
    while (m_impl->pendingCount.load(std::memory_order_acquire) > 0 && waits < kMaxWaits) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ++waits;
    }

    m_impl->cancelFlag.store(false);
}

size_t WaveformCacheBuilder::getPendingCount() const {
    return m_impl->pendingCount.load();
}

} // namespace Audio
} // namespace Aestra
