// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "PreviewEngine.h"
#include "NomadLog.h"
#include "MiniAudioDecoder.h"
#include "PathUtils.h"
#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef _WIN32
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#endif

namespace Nomad {
namespace Audio {

PreviewEngine::PreviewEngine()
    : m_activeVoice(nullptr)
    , m_outputSampleRate(48000.0)
    , m_globalGainDb(-6.0f)
    , m_decodeGeneration(0) 
    , m_workerRunning(true) // Initialize running state
{
    // Start worker thread
    m_workerThread = std::thread(&PreviewEngine::workerLoop, this);
}

PreviewEngine::~PreviewEngine() {
    stop();
    
    // Stop worker thread
    {
        std::lock_guard<std::mutex> lock(m_workerMutex);
        m_workerRunning = false;
        m_workerCV.notify_all();
    }
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

float PreviewEngine::dbToLinear(float db) const {
    return std::pow(10.0f, db / 20.0f);
}

std::shared_ptr<AudioBuffer> PreviewEngine::loadBuffer(const std::string& path, uint32_t& sampleRate, uint32_t& channels) {
    auto loader = [path, &sampleRate, &channels](AudioBuffer& out) -> bool {
        std::vector<float> decoded;
        uint32_t sr = 0;
        uint32_t ch = 0;

        if (decodeAudioFile(path, decoded, sr, ch)) {
            out.data.swap(decoded);
            out.sampleRate = sr;
            out.channels = ch;
            out.numFrames = out.channels ? out.data.size() / out.channels : 0;
            out.sourcePath = path;
            sampleRate = sr;
            channels = ch;
            return true;
        }
        return false;
    };

    return SamplePool::getInstance().acquire(path, loader);
}

PreviewResult PreviewEngine::startVoiceWithBuffer(std::shared_ptr<AudioBuffer> buffer, 
                                                   const std::string& path, 
                                                   float gainDb, double maxSeconds) {
    uint32_t sampleRate = buffer->sampleRate;
    uint32_t channels = buffer->channels;
    
    auto voice = std::make_shared<PreviewVoice>();
    voice->buffer = buffer;
    voice->path = path;
    voice->sampleRate = sampleRate > 0 ? static_cast<double>(sampleRate) : 48000.0;
    voice->channels = channels == 0 ? 2u : channels;
    voice->durationSeconds = (sampleRate > 0 && buffer->numFrames > 0) 
        ? (static_cast<double>(buffer->numFrames) / sampleRate) : 0.0;
    voice->maxPlaySeconds = maxSeconds;
    voice->gain = dbToLinear(gainDb + m_globalGainDb.load(std::memory_order_relaxed));
    voice->phaseFrames = 0.0;
    voice->elapsedSeconds = 0.0;
    voice->fadeInPos = 0.0;
    voice->fadeOutPos = 0.0;
    voice->stopRequested.store(false, std::memory_order_release);
    voice->fadeOutActive = false;
    voice->bufferReady.store(true, std::memory_order_release);
    voice->playing.store(true, std::memory_order_release);

    std::atomic_store_explicit(&m_activeVoice, voice, std::memory_order_release);
    Log::info("PreviewEngine: Playing '" + path + "' (" + std::to_string(sampleRate) + " Hz, " +
              std::to_string(voice->durationSeconds) + " sec)");
    return PreviewResult::Success;
}

void PreviewEngine::decodeAsync(const std::string& path, std::shared_ptr<PreviewVoice> voice) {
    // Increment generation - any in-flight decodes with older generation will be discarded
    uint64_t thisGeneration = m_decodeGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    
    // Queue job for worker thread
    {
        std::lock_guard<std::mutex> lock(m_workerMutex);
        m_pendingJob = DecodeJob{path, voice, thisGeneration};
        // Always overwrite pending job - we only care about the latest UI interaction
    }
    m_workerCV.notify_one();
}

void PreviewEngine::workerLoop() {
    while (true) {
        DecodeJob job;
        {
            std::unique_lock<std::mutex> lock(m_workerMutex);
            m_workerCV.wait(lock, [this] { 
                return m_pendingJob.has_value() || !m_workerRunning; 
            });
            
            if (!m_workerRunning) break;
            
            job = *m_pendingJob;
            m_pendingJob.reset();
        }
        
        // 1. Early Generation Check (Optimization)
        if (m_decodeGeneration.load(std::memory_order_acquire) != job.generation) {
             if (job.voice) job.voice->playing.store(false, std::memory_order_release);
             continue;
        }

        // 2. Decode
        uint32_t sr = 0, ch = 0;
        auto buffer = loadBuffer(job.path, sr, ch);
        
        // 3. Late Generation Check (Correctness)
        if (m_decodeGeneration.load(std::memory_order_acquire) != job.generation) {
             if (job.voice) job.voice->playing.store(false, std::memory_order_release);
             continue;
        }
        
        // 4. Update Voice
        auto voice = job.voice;
        // Verify voice is still active (redundant with generation but safe)
        auto currentVoice = std::atomic_load_explicit(&m_activeVoice, std::memory_order_acquire);
        if (currentVoice.get() != voice.get()) {
            voice->playing.store(false, std::memory_order_release);
            continue;
        }

        if (voice && buffer && buffer->ready.load(std::memory_order_acquire)) {
            voice->buffer = buffer;
            voice->sampleRate = sr > 0 ? static_cast<double>(sr) : 48000.0;
            voice->channels = ch > 0 ? ch : 2;
            voice->durationSeconds = sr > 0 && buffer->numFrames > 0 
                ? static_cast<double>(buffer->numFrames) / sr : 0.0;
            
            voice->bufferReady.store(true, std::memory_order_release);
            
             Log::info("PreviewEngine: Async decode complete for '" + job.path + "' (" + 
                       std::to_string(sr) + " Hz, " + std::to_string(voice->durationSeconds) + " sec)");
        } else if (voice) {
            Log::warning("PreviewEngine: Async decode failed for " + job.path);
            voice->playing.store(false, std::memory_order_release);
        }
    }
}

PreviewResult PreviewEngine::play(const std::string& path, float gainDb, double maxSeconds) {
    // Stop any currently playing preview
    stop();
    
    // Fast path: Check if buffer is already cached (no filesystem stat)
    auto cachedBuffer = SamplePool::getInstance().tryGetCached(path);
    if (cachedBuffer && cachedBuffer->ready.load(std::memory_order_acquire)) {
        // Cache hit! Instant playback
        return startVoiceWithBuffer(cachedBuffer, path, gainDb, maxSeconds);
    }
    
    // Cache miss: Create voice immediately for pending playback
    auto voice = std::make_shared<PreviewVoice>();
    voice->path = path;
    voice->gain = dbToLinear(gainDb + m_globalGainDb.load(std::memory_order_relaxed));
    voice->maxPlaySeconds = maxSeconds;
    voice->phaseFrames = 0.0;
    voice->elapsedSeconds = 0.0;
    voice->fadeInPos = 0.0;
    voice->fadeOutPos = 0.0;
    voice->stopRequested.store(false, std::memory_order_release);
    voice->fadeOutActive = false;
    voice->bufferReady.store(false, std::memory_order_release);  // Not ready yet
    voice->playing.store(true, std::memory_order_release);       // Voice is active
    
    // Set as active voice immediately (will output silence until buffer ready)
    std::atomic_store_explicit(&m_activeVoice, voice, std::memory_order_release);
    
    // Start async decode (non-blocking)
    decodeAsync(path, voice);
    
    Log::info("PreviewEngine: Async decode started for '" + path + "'");
    return PreviewResult::Pending;
}

void PreviewEngine::stop() {
    auto voice = std::atomic_load_explicit(&m_activeVoice, std::memory_order_acquire);
    if (voice) {
        voice->stopRequested.store(true, std::memory_order_release);
        voice->fadeOutActive = true;
        voice->fadeOutPos = 0.0;
    }
}

void PreviewEngine::setOutputSampleRate(double sr) {
    if (sr <= 0.0) return;
    m_outputSampleRate.store(sr, std::memory_order_relaxed);
}

void PreviewEngine::process(float* interleavedOutput, uint32_t numFrames) {
    auto voice = std::atomic_load_explicit(&m_activeVoice, std::memory_order_acquire);
    if (!voice || !voice->playing.load(std::memory_order_acquire) || !interleavedOutput) {
        return;
    }
    
    // Check if buffer is ready (async decode may still be in progress)
    if (!voice->bufferReady.load(std::memory_order_acquire)) {
        // Buffer not ready yet - output silence
        // (could add a small "loading" indicator sound here if desired)
        return;
    }
    
    auto buffer = voice->buffer;
    if (!buffer || buffer->data.empty() || buffer->sampleRate == 0) {
        return;
    }

    const double streamRate = (m_outputSampleRate.load(std::memory_order_relaxed) > 0.0) 
        ? m_outputSampleRate.load() : 48000.0;
    const double fadeInSamples = streamRate * 0.02;  // 20ms fade-in
    const double fadeOutSamples = streamRate * 0.05; // 50ms fade-out
    const double ratio = voice->sampleRate / streamRate;
    const uint64_t totalFrames = buffer->numFrames;
    const float* data = buffer->data.data();
    double phase = voice->phaseFrames;
    const float gain = voice->gain;
    const uint32_t srcChannels = voice->channels;

    // Cubic Hermite Spline Interpolation Helper
    // P(t) = a*t^3 + b*t^2 + c*t + d
    auto cubic = [](float p0, float p1, float p2, float p3, float t) {
        float a = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
        float b = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
        float c = -0.5f * p0 + 0.5f * p2;
        float d = p1;
        return a*t*t*t + b*t*t + c*t + d;
    };

    // Sample Access Helper
    auto getSample = [&](int64_t index, uint32_t channel) -> float {
        if (index < 0) index = 0;
        if (index >= totalFrames) index = totalFrames - 1u;
        
        // Handle mono/stereo mapping inside access
        if (srcChannels == 1) {
            return data[index]; // Mono source
        } else {
            return data[index * 2 + channel];
        }
    };

    for (uint32_t i = 0; i < numFrames; ++i) {
        if (static_cast<uint64_t>(phase) >= totalFrames - 1) { // -1 for safety with cubic lookahead
            voice->stopRequested.store(true, std::memory_order_release);
            voice->fadeOutActive = true;
            break;
        }
        
        uint64_t idx = static_cast<uint64_t>(phase);
        float frac = static_cast<float>(phase - idx);

        float outL, outR;
        
        // Left Channel (or Mono)
        float l0 = getSample((int64_t)idx - 1, 0);
        float l1 = getSample((int64_t)idx,     0);
        float l2 = getSample((int64_t)idx + 1, 0);
        float l3 = getSample((int64_t)idx + 2, 0);
        outL = cubic(l0, l1, l2, l3, frac);

        if (srcChannels == 1) {
            outR = outL;
        } else {
            // Right Channel
            float r0 = getSample((int64_t)idx - 1, 1);
            float r1 = getSample((int64_t)idx,     1);
            float r2 = getSample((int64_t)idx + 1, 1);
            float r3 = getSample((int64_t)idx + 2, 1);
            outR = cubic(r0, r1, r2, r3, frac);
        }

        float envelope = 1.0f;
        if (voice->fadeInPos < fadeInSamples) {
            envelope = static_cast<float>(voice->fadeInPos / fadeInSamples);
            voice->fadeInPos += 1.0;
        }
        if (voice->stopRequested.load(std::memory_order_acquire) || voice->fadeOutActive) {
            voice->fadeOutActive = true;
            double remaining = std::max(0.0, (fadeOutSamples - voice->fadeOutPos) / fadeOutSamples);
            envelope *= static_cast<float>(remaining);
            voice->fadeOutPos += 1.0;
        }

        interleavedOutput[i * 2] += outL * gain * envelope;
        interleavedOutput[i * 2 + 1] += outR * gain * envelope;

        phase += ratio;
    }

    voice->phaseFrames = phase;
    voice->elapsedSeconds += static_cast<double>(numFrames) / streamRate;
    if (voice->maxPlaySeconds > 0.0 && voice->elapsedSeconds >= voice->maxPlaySeconds) {
        voice->stopRequested.store(true, std::memory_order_release);
        voice->fadeOutActive = true;
    }

    bool finished = voice->fadeOutActive && (voice->fadeOutPos >= fadeOutSamples);
    if (finished) {
        voice->playing.store(false, std::memory_order_release);
        if (m_onComplete) {
            m_onComplete(voice->path);
        }
        // Clear only if still the active voice
        std::shared_ptr<PreviewVoice> expected = voice;
        std::atomic_compare_exchange_strong_explicit(
            &m_activeVoice, &expected, std::shared_ptr<PreviewVoice>(),
            std::memory_order_acq_rel, std::memory_order_relaxed);
    }
}

bool PreviewEngine::isPlaying() const {
    auto voice = std::atomic_load_explicit(&m_activeVoice, std::memory_order_acquire);
    return voice && voice->playing.load(std::memory_order_acquire);
}

bool PreviewEngine::isBufferReady() const {
    auto voice = std::atomic_load_explicit(&m_activeVoice, std::memory_order_acquire);
    return voice && voice->bufferReady.load(std::memory_order_acquire);
}

void PreviewEngine::setOnComplete(std::function<void(const std::string& path)> callback) {
    m_onComplete = std::move(callback);
}

void PreviewEngine::setGlobalPreviewVolume(float gainDb) {
    m_globalGainDb.store(gainDb, std::memory_order_relaxed);
}

float PreviewEngine::getGlobalPreviewVolume() const {
    return m_globalGainDb.load(std::memory_order_relaxed);
}

} // namespace Audio
} // namespace Nomad
