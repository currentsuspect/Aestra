// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "WaveformCache.h"
#include "WaveformSIMD.h"
#include "NomadLog.h"
#include <algorithm>
#include <cmath>
#include <future>
#include <queue>
#include <future>
#include <queue>
#include <thread>
#include <shared_mutex>

namespace Nomad {
namespace Audio {

// =============================================================================
// WaveformCache Implementation
// =============================================================================

WaveformCache::WaveformCache() = default;

WaveformCache::~WaveformCache() = default;

void WaveformCache::buildFromBuffer(const AudioBufferData& buffer,
                                     uint32_t baseSamplesPerPeak,
                                     uint32_t numLevels) {
    if (!buffer.isValid()) {
        Log::warning("WaveformCache: Cannot build from invalid buffer");
        return;
    }
    
    buildFromRaw(buffer.interleavedData.data(), 
                 buffer.numFrames, 
                 buffer.numChannels,
                 baseSamplesPerPeak, 
                 numLevels);
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
    
    Log::info("WaveformCache: Built " + std::to_string(numLevels) + " mip levels for " +
              std::to_string(numFrames) + " frames (" + std::to_string(numChannels) + " ch)");
}

void WaveformCache::buildLevel(const float* data, SampleIndex numFrames, uint32_t numChannels,
                                uint32_t samplesPerPeak, WaveformMipLevel& outLevel) {
    outLevel.samplesPerPeak = samplesPerPeak;
    outLevel.numChannels = numChannels;
    outLevel.numPeaks = (numFrames + samplesPerPeak - 1) / samplesPerPeak;
    
    outLevel.peaks.resize(static_cast<size_t>(outLevel.numPeaks * numChannels));
    
    for (SampleIndex peakIdx = 0; peakIdx < outLevel.numPeaks; ++peakIdx) {
        SampleIndex startFrame = peakIdx * samplesPerPeak;
        SampleIndex endFrame = std::min(startFrame + samplesPerPeak, numFrames);
        
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            float minVal, maxVal;
            
            // Use SIMD-accelerated min/max (4-8x faster)
            WaveformSIMD::minMaxChannel(data, numFrames, numChannels, ch,
                                         startFrame, endFrame, minVal, maxVal);
            
            size_t idx = static_cast<size_t>(peakIdx * numChannels + ch);
            outLevel.peaks[idx] = WaveformPeak(minVal, maxVal);
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
    if (m_levels.empty()) return 0;
    
    // Find level with samplesPerPeak <= samplesPerPixel
    // Start from finest (0) and go coarser
    for (size_t i = 0; i < m_levels.size(); ++i) {
        if (m_levels[i].samplesPerPeak >= samplesPerPixel) {
            return i > 0 ? i - 1 : 0;
        }
    }
    
    return m_levels.size() - 1;
}

void WaveformCache::getPeaksForRange(uint32_t channel,
                                      SampleIndex startSample, SampleIndex endSample,
                                      uint32_t numPixels,
                                      std::vector<WaveformPeak>& outPeaks) const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    
    outPeaks.clear();
    outPeaks.resize(numPixels);
    
    if (!m_ready.load(std::memory_order_acquire) || m_levels.empty() || numPixels == 0) {
        return;
    }
    
    if (channel >= m_numChannels || startSample >= endSample) {
        return;
    }
    
    // Calculate samples per pixel
    double samplesPerPixel = static_cast<double>(endSample - startSample) / numPixels;
    
    // Select appropriate mip level
    size_t levelIdx = 0;
    for (size_t i = 0; i < m_levels.size(); ++i) {
        if (m_levels[i].samplesPerPeak <= samplesPerPixel) {
            levelIdx = i;
        } else {
            break;
        }
    }
    
    const WaveformMipLevel& level = m_levels[levelIdx];
    double peaksPerPixel = samplesPerPixel / level.samplesPerPeak;
    
    // Generate peaks for each pixel
    for (uint32_t pixel = 0; pixel < numPixels; ++pixel) {
        double startPeakF = (startSample + pixel * samplesPerPixel) / level.samplesPerPeak;
        double endPeakF = (startSample + (pixel + 1) * samplesPerPixel) / level.samplesPerPeak;
        
        SampleIndex startPeak = static_cast<SampleIndex>(std::floor(startPeakF));
        SampleIndex endPeak = static_cast<SampleIndex>(std::ceil(endPeakF));
        
        outPeaks[pixel] = level.getPeakRange(channel, startPeak, endPeak);
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
    mutable std::mutex mutex;
};

WaveformCacheBuilder::WaveformCacheBuilder()
    : m_impl(std::make_unique<Impl>())
{
}

WaveformCacheBuilder::~WaveformCacheBuilder() {
    cancelAll();
}

void WaveformCacheBuilder::buildAsync(const ClipSource& source, CompletionCallback callback) {
    if (!source.isReady()) {
        Log::warning("WaveformCacheBuilder: Source not ready");
        if (callback) callback(nullptr);
        return;
    }
    
    m_impl->pendingCount.fetch_add(1);
    
    // Capture buffer by shared_ptr for thread safety
    auto buffer = source.getBuffer();
    auto* impl = m_impl.get();
    
    std::thread([buffer, callback, impl]() {
        if (impl->cancelFlag.load()) {
            impl->pendingCount.fetch_sub(1);
            if (callback) callback(nullptr);
            return;
        }
        
        auto cache = std::make_shared<WaveformCache>();
        cache->buildFromBuffer(*buffer);
        
        impl->pendingCount.fetch_sub(1);
        
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
    
    // Wait for pending builds to finish
    while (m_impl->pendingCount.load() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    m_impl->cancelFlag.store(false);
}

size_t WaveformCacheBuilder::getPendingCount() const {
    return m_impl->pendingCount.load();
}

} // namespace Audio
} // namespace Nomad
