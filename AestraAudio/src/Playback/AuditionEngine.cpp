// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "AuditionEngine.h"

#include "AestraLog.h"
#include "ClipResampler.h" // Sinc64Turbo resampling
#include "ClipSource.h"
#include "DSP/PanLaw.h"
#include "MetadataParser.h"
#include "MiniAudioDecoder.h"

#include <algorithm>
#include <chrono>
#include <random>

namespace Aestra {
namespace Audio {

AuditionEngine::AuditionEngine() {
    m_decodeWorkerThread = std::thread(&AuditionEngine::decodeWorkerLoop, this);
    Log::info("[AuditionEngine] Created");
}

AuditionEngine::~AuditionEngine() {
    cancelPendingDecodes();
    {
        std::lock_guard<std::mutex> lock(m_decodeWorkerMutex);
        m_decodeWorkerRunning = false;
        m_pendingDecodeJob.reset();
    }
    m_decodeWorkerCV.notify_one();
    if (m_decodeWorkerThread.joinable()) {
        m_decodeWorkerThread.join();
    }

    stop();
    Log::info("[AuditionEngine] Destroyed");
}

// === Queue Management ===

void AuditionEngine::addToQueue(const std::string& filePath, bool isReference) {
    // Construct item and parse metadata outside the lock
    AuditionQueueItem item;
    item.id = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    item.filePath = filePath;
    item.isReference = isReference;
    item.isFromTimeline = false;

    Log::info("[AuditionEngine] Parsing metadata...");
    AudioMetadata meta = MetadataParser::parse(filePath);
    Log::info("[AuditionEngine] Metadata parsed OK");

    item.title = meta.title.empty() ? "" : meta.title;
    item.artist = meta.artist.empty() ? (isReference ? "Reference Track" : "Unknown Artist") : meta.artist;
    item.album = meta.album;
    item.durationSeconds = meta.durationSeconds;
    item.coverArtData = std::move(meta.coverArtData);
    item.coverArtMimeType = meta.coverArtMimeType;

    if (item.title.empty()) {
        size_t lastSlash = filePath.find_last_of("/\\");
        size_t lastDot = filePath.find_last_of('.');
        if (lastSlash != std::string::npos && lastDot != std::string::npos && lastDot > lastSlash) {
            item.title = filePath.substr(lastSlash + 1, lastDot - lastSlash - 1);
        } else if (lastSlash != std::string::npos) {
            item.title = filePath.substr(lastSlash + 1);
        } else {
            item.title = filePath;
        }
    }

    // Only hold the lock for queue mutation
    bool shouldPreload = false;
    std::function<void()> onQueueUpdated;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        Log::info("[AuditionEngine] Pushing to queue vector...");
        m_queue.push_back(std::move(item));
        Log::info("[AuditionEngine] Queue push done, size=" + std::to_string(m_queue.size()));

        if (m_queue.size() == 1 && m_currentIndex < 0) {
            m_currentIndex = 0;
            shouldPreload = true;
        }

        Log::info("[AuditionEngine] addToQueue complete");
    }
    {
        std::lock_guard<std::mutex> callbackLock(m_callbackMutex);
        onQueueUpdated = m_onQueueUpdated;
    }

    Log::info("[AuditionEngine] Calling onQueueUpdated...");
    if (onQueueUpdated) {
        onQueueUpdated();
    }

    if (shouldPreload) {
        Log::info("[AuditionEngine] Preloading first queued track...");
        loadCurrentTrack(false);
    }
}

void AuditionEngine::addTimelineTrack(uint32_t trackId, const std::string& trackName) {
    std::lock_guard<std::mutex> lock(m_queueMutex);

    AuditionQueueItem item;
    item.id = "timeline_" + std::to_string(trackId);
    item.filePath = ""; // Will be rendered on-demand
    item.title = trackName;
    item.artist = "From Timeline";
    item.isFromTimeline = true;
    item.sourceTrackId = trackId;
    item.isReference = false;

    m_queue.push_back(std::move(item));

    Log::info("[AuditionEngine] Added timeline track: " + trackName);
}

void AuditionEngine::clearQueue() {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    cancelPendingDecodes();
    stop();
    m_queue.clear();
    m_currentIndex = -1;
    m_currentSource.store(std::shared_ptr<ClipSource>(nullptr), std::memory_order_release);
    Log::info("[AuditionEngine] Queue cleared");
}

void AuditionEngine::removeFromQueue(size_t index) {
    std::function<void()> onQueueUpdated;
    bool shouldReloadCurrent = false;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (index >= m_queue.size()) return;

        const int32_t removed = static_cast<int32_t>(index);
        m_queue.erase(m_queue.begin() + static_cast<std::ptrdiff_t>(index));

        if (m_queue.empty()) {
            m_currentIndex = -1;
            m_currentSource.store(std::shared_ptr<ClipSource>(nullptr), std::memory_order_release);
            m_positionSeconds.store(0.0, std::memory_order_release);
            m_cachedDurationSeconds.store(0.0, std::memory_order_release);
            m_isPlaying.store(false, std::memory_order_release);
        } else if (m_currentIndex > removed) {
            --m_currentIndex;
        } else if (m_currentIndex == removed) {
            if (m_currentIndex >= static_cast<int32_t>(m_queue.size())) {
                m_currentIndex = static_cast<int32_t>(m_queue.size()) - 1;
            }
            shouldReloadCurrent = true;
        }

    }
    {
        std::lock_guard<std::mutex> callbackLock(m_callbackMutex);
        onQueueUpdated = m_onQueueUpdated;
    }

    if (shouldReloadCurrent) {
        loadCurrentTrack(false);
    }
    if (onQueueUpdated) onQueueUpdated();
}

void AuditionEngine::moveQueueItem(size_t fromIndex, size_t toIndex) {
    std::function<void()> onQueueUpdated;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (fromIndex >= m_queue.size() || toIndex >= m_queue.size() || fromIndex == toIndex) {
            return;
        }

        auto moved = std::move(m_queue[fromIndex]);
        m_queue.erase(m_queue.begin() + static_cast<std::ptrdiff_t>(fromIndex));
        m_queue.insert(m_queue.begin() + static_cast<std::ptrdiff_t>(toIndex), std::move(moved));

        const int32_t from = static_cast<int32_t>(fromIndex);
        const int32_t to = static_cast<int32_t>(toIndex);
        if (m_currentIndex == from) {
            m_currentIndex = to;
        } else if (from < m_currentIndex && to >= m_currentIndex) {
            --m_currentIndex;
        } else if (from > m_currentIndex && to <= m_currentIndex) {
            ++m_currentIndex;
        }

    }
    {
        std::lock_guard<std::mutex> callbackLock(m_callbackMutex);
        onQueueUpdated = m_onQueueUpdated;
    }
    if (onQueueUpdated) onQueueUpdated();
}

std::optional<AuditionQueueItem> AuditionEngine::getCurrentItem() const {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int32_t>(m_queue.size())) {
        return m_queue[static_cast<size_t>(m_currentIndex)];
    }
    return std::nullopt;
}

void AuditionEngine::nextTrack() {
    std::string filePath;
    double lastPosition = 0.0;
    bool isTimeline = false;
    std::string title;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_queue.empty())
            return;

        if (m_shuffle.load()) {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<size_t> dist(0, m_queue.size() - 1);
            m_currentIndex = static_cast<int32_t>(dist(gen));
        } else {
            m_currentIndex++;
            if (m_currentIndex >= static_cast<int32_t>(m_queue.size())) {
                if (m_repeatMode == RepeatMode::All) {
                    m_currentIndex = 0;
                } else {
                    m_currentIndex = static_cast<int32_t>(m_queue.size()) - 1;
                    pause();
                    return;
                }
            }
        }

        const auto& item = m_queue[static_cast<size_t>(m_currentIndex)];
        filePath = item.filePath;
        lastPosition = item.lastPosition;
        isTimeline = item.isFromTimeline;
        title = item.title;
    }
    loadCurrentTrackImpl(filePath, lastPosition, isTimeline, title, true);
}

void AuditionEngine::previousTrack() {
    std::string filePath;
    double lastPosition = 0.0;
    bool isTimeline = false;
    std::string title;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_queue.empty())
            return;

        // If more than 3 seconds into track, restart current track
        if (m_positionSeconds.load() > 3.0) {
            seekSeconds(0.0);
            return;
        }

        m_currentIndex--;
        if (m_currentIndex < 0) {
            if (m_repeatMode == RepeatMode::All) {
                m_currentIndex = static_cast<int32_t>(m_queue.size()) - 1;
            } else {
                m_currentIndex = 0;
            }
        }

        const auto& item = m_queue[static_cast<size_t>(m_currentIndex)];
        filePath = item.filePath;
        lastPosition = item.lastPosition;
        isTimeline = item.isFromTimeline;
        title = item.title;
    }
    loadCurrentTrackImpl(filePath, lastPosition, isTimeline, title, true);
}

void AuditionEngine::jumpToTrack(size_t index) {
    std::string filePath;
    double lastPosition = 0.0;
    bool isTimeline = false;
    std::string title;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (index >= m_queue.size())
            return;

        m_currentIndex = static_cast<int32_t>(index);
        const auto& item = m_queue[index];
        filePath = item.filePath;
        lastPosition = item.lastPosition;
        isTimeline = item.isFromTimeline;
        title = item.title;
    }
    loadCurrentTrackImpl(filePath, lastPosition, isTimeline, title, true);
}

// === Transport Control ===

void AuditionEngine::play() {
    if (m_currentIndex < 0 && !m_queue.empty()) {
        jumpToTrack(0);
    } else if (m_currentIndex >= 0 && !m_currentSource.load(std::memory_order_acquire)) {
        // Track selected but not yet decoded — decode now (lazy load)
        std::string filePath;
        double lastPosition = 0.0;
        bool isTimeline = false;
        std::string title;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_currentIndex < 0 || m_currentIndex >= static_cast<int32_t>(m_queue.size()))
                return;
            const auto& item = m_queue[static_cast<size_t>(m_currentIndex)];
            filePath = item.filePath;
            lastPosition = item.lastPosition;
            isTimeline = item.isFromTimeline;
            title = item.title;
        }
        loadCurrentTrackImpl(filePath, lastPosition, isTimeline, title, true);
    }

    bool wasPlaying = m_isPlaying.exchange(true);
    std::function<void(bool)> onPlaybackStateChanged;
    {
        std::lock_guard<std::mutex> callbackLock(m_callbackMutex);
        onPlaybackStateChanged = m_onPlaybackStateChanged;
    }
    if (!wasPlaying && onPlaybackStateChanged) {
        onPlaybackStateChanged(true);
    }

    Log::info("[AuditionEngine] Play");
}

void AuditionEngine::pause() {
    bool wasPlaying = m_isPlaying.exchange(false);
    std::function<void(bool)> onPlaybackStateChanged;
    {
        std::lock_guard<std::mutex> callbackLock(m_callbackMutex);
        onPlaybackStateChanged = m_onPlaybackStateChanged;
    }
    if (wasPlaying && onPlaybackStateChanged) {
        onPlaybackStateChanged(false);
    }

    Log::info("[AuditionEngine] Pause");
}

void AuditionEngine::stop() {
    m_isPlaying.store(false);
    m_positionSeconds.store(0.0);

    std::function<void(bool)> onPlaybackStateChanged;
    {
        std::lock_guard<std::mutex> callbackLock(m_callbackMutex);
        onPlaybackStateChanged = m_onPlaybackStateChanged;
    }
    if (onPlaybackStateChanged) {
        onPlaybackStateChanged(false);
    }

    Log::info("[AuditionEngine] Stop");
}

void AuditionEngine::togglePlayPause() {
    if (m_isPlaying.load()) {
        pause();
    } else {
        play();
    }
}

void AuditionEngine::seekNormalized(double position) {
    auto currentSource = m_currentSource.load(std::memory_order_acquire);
    if (currentSource) {
        double duration = getDurationSeconds();
        seekSeconds(position * duration);
    }
}

void AuditionEngine::seekSeconds(double seconds) {
    seconds = std::max(0.0, std::min(seconds, getDurationSeconds()));
    m_positionSeconds.store(seconds);

    // Seek the source
    // TODO: Implement source seeking when ClipSource gets seek support
    // if (m_currentSource) {
    //     m_currentSource->setPosition(seconds);
    // }

    std::function<void(double)> onPositionChanged;
    {
        std::lock_guard<std::mutex> callbackLock(m_callbackMutex);
        onPositionChanged = m_onPositionChanged;
    }
    if (onPositionChanged) {
        onPositionChanged(seconds);
    }
}

double AuditionEngine::getPositionNormalized() const {
    double duration = getDurationSeconds();
    if (duration <= 0.0)
        return 0.0;
    return std::clamp(getPositionSeconds() / duration, 0.0, 1.0);
}

double AuditionEngine::getPositionSeconds() const {
    const double raw = std::max(0.0, m_positionSeconds.load());
    const double duration = getDurationSeconds();
    if (duration > 0.0) {
        return std::min(raw, duration);
    }
    return raw;
}

double AuditionEngine::getDurationSeconds() const {
    auto currentSource = m_currentSource.load(std::memory_order_acquire);
    if (currentSource) {
        return currentSource->getDurationSeconds();
    }

    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int32_t>(m_queue.size())) {
        return m_queue[static_cast<size_t>(m_currentIndex)].durationSeconds;
    }
    return 0.0;
}

// === DSP Chain ===

void AuditionEngine::setDSPPreset(const AuditionDSPPreset& preset) {
    m_currentPreset = preset;
    Log::info("[AuditionEngine] DSP Preset: " + preset.name);
}

// === Audio Processing ===

void AuditionEngine::processBlock(float* output, uint32_t numFrames, uint32_t numChannels) {
    // Clear output first
    std::fill(output, output + numFrames * numChannels, 0.0f);

    if (!m_isPlaying.load(std::memory_order_relaxed)) {
        return;
    }

    // RT-safe atomic load - bumps refcount, keeps object alive for this scope
    // NOTE: C++17 uses free function std::atomic_load_explicit()
    //       C++20+ has member syntax: std::atomic<std::shared_ptr<T>>::load()
    auto currentSource = m_currentSource.load(std::memory_order_acquire);
    if (!currentSource) {
        return;
    }

    // Read from source
    const auto* srcBuffer = currentSource->getRawBuffer();
    if (!srcBuffer || !srcBuffer->isValid()) {
        return;
    }

    double srcRate = static_cast<double>(srcBuffer->sampleRate);
    double dstRate = m_sampleRate.load(std::memory_order_relaxed);

    // Safety check for rates
    if (srcRate <= 0.0 || dstRate <= 0.0) {
        return;
    }

    double currentPos = m_positionSeconds.load(std::memory_order_relaxed);
    double ratio = srcRate / dstRate;
    (void)ratio; // Used for reference

    // Get interleaved data pointer for Sinc64 interpolation
    const float* interleavedData = srcBuffer->interleavedData.data();
    const int64_t totalFrames = static_cast<int64_t>(srcBuffer->numFrames);
    const uint32_t srcChannels = srcBuffer->numChannels;

    // Process frames with Sinc64Turbo (measured quality: Aestra-Internals: aestra-docs/audio-research-bench.md)
    for (uint32_t i = 0; i < numFrames; ++i) {
        // Calculate source frame position (fractional)
        double srcPosition = currentPos * srcRate;

        // Advance time for next sample
        currentPos += 1.0 / dstRate;

        // Bounds check
        if (srcPosition < 0 || srcPosition >= static_cast<double>(totalFrames)) {
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                output[i * numChannels + ch] = 0.0f;
            }
            continue;
        }

        // Use Sinc64Turbo for stereo sources (optimal path)
        if (srcChannels >= 2 && numChannels >= 2) {
            float outL, outR;
            Interpolators::Sinc64Turbo::interpolate(interleavedData, totalFrames, srcPosition, outL, outR);
            output[i * numChannels] = outL;
            output[i * numChannels + 1] = outR;
        } else {
            // Mono source or mono output - use ClipResampler (falls back to Cubic for mono)
            m_resampler.setQuality(ClipResamplingQuality::High);

            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                uint32_t srcCh = (srcChannels == 1) ? 0 : ch;
                output[i * numChannels + ch] =
                    m_resampler.getSample(interleavedData, totalFrames, srcChannels, srcPosition, srcCh);
            }
        }
    }

    // Match the main engine's centered-track reference gain. Audition is a
    // separate listening surface, but bypass should not be louder than placing
    // the same file on a default centered track.
    const float outputGain = m_volume.load(std::memory_order_relaxed) * PanLaw::kEqualPowerCenterGain;
    if (outputGain != 1.0f) {
        for (uint32_t i = 0; i < numFrames * numChannels; ++i) {
            output[i] *= outputGain;
        }
    }

    // Check for end of track
    double duration = m_cachedDurationSeconds.load(std::memory_order_acquire);
    const double boundedPos = (duration > 0.0)
                                  ? std::clamp(currentPos, 0.0, duration)
                                  : std::max(0.0, currentPos);
    m_positionSeconds.store(boundedPos, std::memory_order_relaxed);

    if (currentPos >= duration && duration > 0.0) {
        if (m_repeatMode == RepeatMode::One) {
            m_positionSeconds.store(0.0, std::memory_order_relaxed);
        } else {
            m_trackTransitionPending.store(true, std::memory_order_release);
        }
    }

    // Apply DSP chain if enabled
    if (m_abWetMode.load(std::memory_order_relaxed) && m_currentPreset.enabled) {
        applyDSPChain(output, numFrames, numChannels);
    }

    // Position callback (throttled)
    if (m_positionCallbackCounter.fetch_add(1, std::memory_order_relaxed) % 10 == 0 && m_onPositionChanged) {
        m_onPositionChanged(boundedPos);
    }
}

// === Internal Helpers ===

void AuditionEngine::loadCurrentTrack(bool startPlayback) {
    std::string filePath;
    double lastPosition = 0.0;
    bool isTimeline = false;
    std::string title;

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_currentIndex < 0 || m_currentIndex >= static_cast<int32_t>(m_queue.size())) {
            m_currentSource.store(std::shared_ptr<ClipSource>(nullptr), std::memory_order_release);
            return;
        }

        const auto& item = m_queue[static_cast<size_t>(m_currentIndex)];
        filePath = item.filePath;
        lastPosition = item.lastPosition;
        isTimeline = item.isFromTimeline;
        title = item.title;
    }

    loadCurrentTrackImpl(filePath, lastPosition, isTimeline, title, startPlayback);
}

void AuditionEngine::loadCurrentTrackImpl(const std::string& filePath, double lastPosition,
                                           bool isTimeline, const std::string& title,
                                           bool startPlayback) {
    if (isTimeline) {
        // TODO: Render timeline track to temp buffer
        Log::info("[AuditionEngine] Loading timeline track: " + title);
        return;
    }

    // Increment generation so any in-flight decode from a previous click is discarded
    const uint64_t gen = m_loadGeneration.fetch_add(1, std::memory_order_relaxed) + 1;

    Log::info("[AuditionEngine] Queuing async decode: " + filePath + " (gen=" + std::to_string(gen) + ")");

    {
        std::lock_guard<std::mutex> lock(m_decodeWorkerMutex);
        m_pendingDecodeJob = DecodeJob{filePath, lastPosition, false, title, startPlayback, gen};
    }
    m_decodeWorkerCV.notify_one();
}

void AuditionEngine::cancelPendingDecodes() {
    m_loadGeneration.fetch_add(1, std::memory_order_acq_rel);
    {
        std::lock_guard<std::mutex> lock(m_decodeWorkerMutex);
        m_pendingDecodeJob.reset();
    }
    m_decodeWorkerCV.notify_one();
}

void AuditionEngine::decodeWorkerLoop() {
    while (true) {
        DecodeJob job;
        {
            std::unique_lock<std::mutex> lock(m_decodeWorkerMutex);
            m_decodeWorkerCV.wait(lock, [this] { return m_pendingDecodeJob.has_value() || !m_decodeWorkerRunning; });
            if (!m_decodeWorkerRunning) {
                return;
            }
            job = std::move(*m_pendingDecodeJob);
            m_pendingDecodeJob.reset();
        }

        if (m_loadGeneration.load(std::memory_order_acquire) != job.generation) {
            continue;
        }

        std::vector<float> decodedData;
        uint32_t sr = 0;
        uint32_t ch = 0;

        if (!decodeAudioFile(job.filePath, decodedData, sr, ch)) {
            Log::error("[AuditionEngine] Failed to decode file: " + job.filePath);
            continue;
        }

        // Discard stale results (user clicked something else)
        if (m_loadGeneration.load(std::memory_order_acquire) != job.generation) {
            Log::info("[AuditionEngine] Discarding stale decode (gen=" + std::to_string(job.generation) + ")");
            continue;
        }

        // Create AudioBufferData
        auto bufferData = std::make_shared<AudioBufferData>();
        bufferData->interleavedData = std::move(decodedData);
        bufferData->sampleRate = sr;
        bufferData->numChannels = ch;
        bufferData->numFrames = (ch > 0) ? bufferData->interleavedData.size() / ch : 0;

        try {
            ClipSourceID id{0};
            auto newSource = std::make_shared<ClipSource>(id, "AuditionSource");
            newSource->setFilePath(job.filePath);
            newSource->setBuffer(bufferData);

            if (m_loadGeneration.load(std::memory_order_acquire) != job.generation) {
                Log::info("[AuditionEngine] Discarding stale decode before publish (gen=" +
                          std::to_string(job.generation) + ")");
                continue;
            }

            Log::info("[AuditionEngine] Source loaded: " + std::to_string(bufferData->durationSeconds()) + "s");

            // Atomic publish to RT thread
            m_currentSource.store(newSource, std::memory_order_release);

        } catch (const std::exception& e) {
            Log::error("[AuditionEngine] Exception creating source: " + std::string(e.what()));
            m_currentSource.store(std::shared_ptr<ClipSource>(nullptr), std::memory_order_release);
            m_cachedDurationSeconds.store(0.0, std::memory_order_release);
            continue;
        }

        const double duration = bufferData->durationSeconds();
        const double clampedStart =
            (duration > 0.0) ? std::clamp(job.lastPosition, 0.0, duration) : std::max(0.0, job.lastPosition);
        m_positionSeconds.store(clampedStart, std::memory_order_release);
        m_cachedDurationSeconds.store(duration, std::memory_order_release);

        if (m_loadGeneration.load(std::memory_order_acquire) != job.generation) {
            Log::info("[AuditionEngine] Discarding stale decode before callbacks (gen=" +
                      std::to_string(job.generation) + ")");
            continue;
        }

        std::function<void(const AuditionQueueItem&)> onTrackChanged;
        std::function<void(bool)> onPlaybackStateChanged;
        {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            onTrackChanged = m_onTrackChanged;
            onPlaybackStateChanged = m_onPlaybackStateChanged;
        }

        if (onTrackChanged) {
            AuditionQueueItem item;
            item.filePath = job.filePath;
            item.title = job.title;
            onTrackChanged(item);
        }

        if (job.startPlayback && !m_isPlaying.load(std::memory_order_relaxed)) {
            m_isPlaying.store(true, std::memory_order_release);
            if (onPlaybackStateChanged) {
                onPlaybackStateChanged(true);
            }
        }
    }
}

std::shared_ptr<ClipSource> AuditionEngine::getCurrentSource() const {
    return m_currentSource.load(std::memory_order_acquire);
}

void AuditionEngine::applyDSPChain(float* buffer, uint32_t numFrames, uint32_t numChannels) {
    if (!m_currentPreset.enabled) {
        m_dspState.reset();
        return;
    }

    const float sampleRate = static_cast<float>(m_sampleRate.load());
    const float dt = 1.0f / sampleRate;

    // === 1. Coefficients Calculation ===

    // -- Loudness Gain --
    float targetGain = 1.0f;
    if (m_currentPreset.targetLufs < 0.0f) {
        // -14 LUFS target (approximate gain calc)
        targetGain = std::pow(10.0f, (m_currentPreset.targetLufs + 14.0f) / 20.0f);
        targetGain = std::clamp(targetGain, 0.1f, 4.0f);
    }

    // -- High/Low Cuts (1-pole) --
    float highCutAlpha = 1.0f;
    if (m_currentPreset.highCutHz < 20000.0f) {
        float wc = 2.0f * 3.14159f * m_currentPreset.highCutHz;
        highCutAlpha = wc / (wc + sampleRate);
    }

    // Low-cut (highpass via subtraction of lowpass)
    // We want a Low Pass filter at the cutoff freq, then subtract it.
    // LPF coefficient alpha = wc / (wc + sampleRate)
    float lowCutAlpha = 0.0f;
    if (m_currentPreset.lowCutHz > 20.0f) {
        float wc = 2.0f * 3.14159f * m_currentPreset.lowCutHz;
        lowCutAlpha = wc / (wc + sampleRate);
    }

    // -- Shelving Filters (Biquad) --
    // We'll calculate coeffs for Low Shelf (Bass) and High Shelf (Treble)
    /*
       RBJ Audio EQ Cookbook - Low Shelf
       A = 10^(dB/40)
       w0 = 2*pi*f0/Fs
       alpha = sin(w0)/2 * sqrt(2) (Q=0.707)
    */
    auto calcShelfCoeffs = [sampleRate](float db, float freq, bool isLowShelf, float& b0, float& b1, float& b2,
                                        float& a1, float& a2) {
        if (std::abs(db) < 0.1f) {
            b0 = 1.0f;
            b1 = 0.0f;
            b2 = 0.0f;
            a1 = 0.0f;
            a2 = 0.0f;
            return;
        }

        float A = std::pow(10.0f, db / 40.0f);
        float w0 = 2.0f * 3.14159f * freq / sampleRate;
        float alpha = std::sin(w0) / 2.0f * 1.414f; // Q ~= 0.707
        float cosw0 = std::cos(w0);
        float a0_norm;

        if (isLowShelf) {
            // Low Shelf
            float Am1 = A - 1;
            float Ap1 = A + 1;
            float sqA = 2.0f * std::sqrt(A) * alpha;

            b0 = A * ((Ap1) - (Am1)*cosw0 + sqA);
            b1 = 2.0f * A * ((Am1) - (Ap1)*cosw0);
            b2 = A * ((Ap1) - (Am1)*cosw0 - sqA);
            a0_norm = (Ap1) + (Am1)*cosw0 + sqA;
            a1 = -2.0f * ((Am1) + (Ap1)*cosw0);
            a2 = (Ap1) + (Am1)*cosw0 - sqA;
        } else {
            // High Shelf
            float Am1 = A - 1;
            float Ap1 = A + 1;
            float sqA = 2.0f * std::sqrt(A) * alpha;

            b0 = A * ((Ap1) + (Am1)*cosw0 + sqA);
            b1 = -2.0f * A * ((Am1) + (Ap1)*cosw0);
            b2 = A * ((Ap1) + (Am1)*cosw0 - sqA);
            a0_norm = (Ap1) - (Am1)*cosw0 + sqA;
            a1 = 2.0f * ((Am1) - (Ap1)*cosw0);
            a2 = (Ap1) - (Am1)*cosw0 - sqA;
        }

        // Normalize
        float invA0 = 1.0f / a0_norm;
        b0 *= invA0;
        b1 *= invA0;
        b2 *= invA0;
        a1 *= invA0;
        a2 *= invA0;
    };

    // Bass Boost (Low Shelf @ 100Hz)
    float ls_b0, ls_b1, ls_b2, ls_a1, ls_a2;
    calcShelfCoeffs(m_currentPreset.bassBoostDb, 100.0f, true, ls_b0, ls_b1, ls_b2, ls_a1, ls_a2);

    // Treble Shelf (High Shelf @ 10kHz)
    float hs_b0, hs_b1, hs_b2, hs_a1, hs_a2;
    calcShelfCoeffs(m_currentPreset.trebleShelfDb, 10000.0f, false, hs_b0, hs_b1, hs_b2, hs_a1, hs_a2);

    // -- Dynamics (Limiter/Compressor) --
    float compRatio = m_currentPreset.compressionRatio;
    float limitCeiling = std::pow(10.0f, m_currentPreset.limiterCeilingDb / 20.0f);
    // Attack/Release coefficients
    float attCoef = std::exp(-dt / (m_currentPreset.limiterAttackMs * 0.001f));
    float relCoef = std::exp(-dt / (m_currentPreset.limiterReleaseMs * 0.001f));

    // -- Lo-fi --
    float lofiAmount = m_currentPreset.lofiAmount;
    int decimationFactor = 1;
    float bitDepthLevels = 65536.0f; // 16-bit
    if (lofiAmount > 0.0f) {
        decimationFactor = 1 + static_cast<int>(lofiAmount * 7.0f);
        float bits = 16.0f - lofiAmount * 10.0f; // 16 -> 6 bits
        bitDepthLevels = std::pow(2.0f, bits);
    }

    // === 2. Process Loop ===

    for (uint32_t i = 0; i < numFrames; ++i) {
        float left = buffer[i * numChannels] * targetGain;
        float right = (numChannels > 1) ? buffer[i * numChannels + 1] * targetGain : left;

        // 1. High/Low Cut Filters (1-pole)
        if (highCutAlpha < 1.0f) {
            m_dspState.lpStateL = highCutAlpha * left + (1.0f - highCutAlpha) * m_dspState.lpStateL;
            m_dspState.lpStateR = highCutAlpha * right + (1.0f - highCutAlpha) * m_dspState.lpStateR;
            left = m_dspState.lpStateL;
            right = m_dspState.lpStateR;
        }

        if (lowCutAlpha > 0.0f) {
            // Correct simple HP:
            // y[n] = x[n] - lp[n] where lp[n] is heavy filtered.
            // But we want cut > 20Hz.
            // Let's rely on standard: val = val - (val * lowCutAlpha)... wait

            // Let's use the robust HighPass logic from before but with correct members
            // Previous was: float newHpL = lowCutAlpha * (hpStateL + left - lpStateL);
            // That looked like a mistake in previous code (mix of variables).
            // Simple HP: y = x - (x processed by LP).
            static float hp_lp_L = 0, hp_lp_R = 0; // Local state for HP implementation
            hp_lp_L = lowCutAlpha * left + (1.0f - lowCutAlpha) * hp_lp_L;
            hp_lp_R = lowCutAlpha * right + (1.0f - lowCutAlpha) * hp_lp_R;
            left -= hp_lp_L; // This creates a High Pass
            right -= hp_lp_R;
        }

        // 2. Shelving EQ (Biquad)
        auto processBiquad = [](float input, float b0, float b1, float b2, float a1, float a2, float& z1, float& z2) {
            float out = input * b0 + z1;
            z1 = input * b1 + z2 - out * a1;
            z2 = input * b2 - out * a2;
            return out;
        };

        // Bass Boost
        if (std::abs(m_currentPreset.bassBoostDb) > 0.1f) {
            left = processBiquad(left, ls_b0, ls_b1, ls_b2, ls_a1, ls_a2, m_dspState.lowShelf.z1_L,
                                 m_dspState.lowShelf.z2_L);
            right = processBiquad(right, ls_b0, ls_b1, ls_b2, ls_a1, ls_a2, m_dspState.lowShelf.z1_R,
                                  m_dspState.lowShelf.z2_R);
        }

        // Treble Shelf
        if (std::abs(m_currentPreset.trebleShelfDb) > 0.1f) {
            left = processBiquad(left, hs_b0, hs_b1, hs_b2, hs_a1, hs_a2, m_dspState.highShelf.z1_L,
                                 m_dspState.highShelf.z2_L);
            right = processBiquad(right, hs_b0, hs_b1, hs_b2, hs_a1, hs_a2, m_dspState.highShelf.z1_R,
                                  m_dspState.highShelf.z2_R);
        }

        // 3. Dynamics (Limiter/Compressor)
        // Detect peak level
        float maxAbs = std::max(std::abs(left), std::abs(right));

        // Envelope follower (release implies decay, attack implies rise)
        if (maxAbs > m_dspState.envFollower) {
            m_dspState.envFollower = attCoef * m_dspState.envFollower + (1.0f - attCoef) * maxAbs;
        } else {
            m_dspState.envFollower = relCoef * m_dspState.envFollower + (1.0f - relCoef) * maxAbs;
        }

        // Calculate gain reduction
        float gainReduction = 1.0f;
        float threshold = limitCeiling / compRatio; // Soft knee-ish area

        // Hard limiter logic + Soft compression
        if (m_dspState.envFollower > limitCeiling) {
            // Hard limit behavior for peaks
            gainReduction = limitCeiling / m_dspState.envFollower;
        } else if (compRatio > 1.0f && m_dspState.envFollower > 0.5f) {
            // Soft compression for "glued" feel
            // Simple ratio logic
            float excess = m_dspState.envFollower - 0.5f;
            if (excess > 0) {
                float compressed = 0.5f + excess / compRatio;
                gainReduction = compressed / m_dspState.envFollower;
            }
        }

        left *= gainReduction;
        right *= gainReduction;

        // 4. Lo-fi
        if (lofiAmount > 0.0f) {
            if (m_dspState.lofiCounter == 0) {
                m_dspState.lofiHoldL = left;
                m_dspState.lofiHoldR = right;
            }
            m_dspState.lofiCounter = (m_dspState.lofiCounter + 1) % decimationFactor;
            left = m_dspState.lofiHoldL;
            right = m_dspState.lofiHoldR;

            // Bit transform
            left = std::round(left * bitDepthLevels) / bitDepthLevels;
            right = std::round(right * bitDepthLevels) / bitDepthLevels;
        }

        // Output with final safety clamp
        buffer[i * numChannels] = std::clamp(left, -1.0f, 1.0f);
        if (numChannels > 1) {
            buffer[i * numChannels + 1] = std::clamp(right, -1.0f, 1.0f);
        }
    }
}

void AuditionEngine::handleDeferredTrackTransition() {
    if (m_trackTransitionPending.exchange(false, std::memory_order_acq_rel)) {
        nextTrack();
    }
}

void AuditionEngine::notifyTrackChanged() {
    std::optional<AuditionQueueItem> item;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_currentIndex >= 0 && m_currentIndex < static_cast<int32_t>(m_queue.size())) {
            item = m_queue[static_cast<size_t>(m_currentIndex)];
        }
    }
    std::function<void(const AuditionQueueItem&)> onTrackChanged;
    {
        std::lock_guard<std::mutex> callbackLock(m_callbackMutex);
        onTrackChanged = m_onTrackChanged;
    }
    if (item && onTrackChanged) {
        onTrackChanged(*item);
    }
}

} // namespace Audio
} // namespace Aestra
