// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "PreviewEngine.h"
#include "NomadLog.h"
#include "MiniAudioDecoder.h"
#include "PathUtils.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <immintrin.h>
#include <chrono>

#ifdef _WIN32
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#endif

namespace Nomad {
namespace Audio {

PreviewEngine::PreviewEngine()
    : m_activeVoiceRaw(nullptr)
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

    // Clean up any remaining voice
    m_activeVoiceOwner.reset();

    // Clear zombies immediately on destruction (no race possible as audio thread is gone)
    m_zombies.clear();
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
    voice->seekRequestSeconds.store(-1.0, std::memory_order_release);
    voice->fadeOutActive = false;
    voice->bufferReady.store(true, std::memory_order_release);
    voice->playing.store(true, std::memory_order_release);

    // RT-Safe Swap:
    PreviewVoice* oldRaw = m_activeVoiceRaw.exchange(voice.get(), std::memory_order_acq_rel);
    std::shared_ptr<PreviewVoice> oldOwner = m_activeVoiceOwner;
    m_activeVoiceOwner = voice;

    if (oldOwner) {
        std::lock_guard<std::mutex> lock(m_workerMutex);
        // Add timestamp to zombie for deferred destruction
        m_zombies.emplace_back(ZombieVoice{oldOwner, std::chrono::steady_clock::now()});
        m_workerCV.notify_one();
    }

    Log::info("PreviewEngine: Playing '" + path + "' (" + std::to_string(sampleRate) + " Hz, " +
              std::to_string(voice->durationSeconds) + " sec)");
    return PreviewResult::Success;
}

void PreviewEngine::decodeAsync(const std::string& path, std::shared_ptr<PreviewVoice> voice) {
    uint64_t thisGeneration = m_decodeGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    
    {
        std::lock_guard<std::mutex> lock(m_workerMutex);
        m_pendingJob = DecodeJob{path, voice, thisGeneration};
    }
    m_workerCV.notify_one();
}

void PreviewEngine::workerLoop() {
    while (true) {
        DecodeJob job;
        {
            std::unique_lock<std::mutex> lock(m_workerMutex);

            // Wait condition: Job pending OR zombies need checking OR stop requested
            // We wake up every 100ms to check zombies even if no job
            auto timeout = std::chrono::milliseconds(100);

            bool signaled = m_workerCV.wait_for(lock, timeout, [this] {
                return m_pendingJob.has_value() || !m_workerRunning; 
            });
            
            if (!m_workerRunning) break;
            
            // Clean up zombies
            if (!m_zombies.empty()) {
                auto now = std::chrono::steady_clock::now();
                auto it = m_zombies.begin();
                while (it != m_zombies.end()) {
                    // Keep zombies alive for 2 seconds to ensure RT thread is done
                    auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->deathTime);
                    if (age.count() >= 2) {
                        it = m_zombies.erase(it); // Safe destruction
                    } else {
                        ++it;
                    }
                }
            }

            if (m_pendingJob) {
                job = *m_pendingJob;
                m_pendingJob.reset();
            } else {
                continue; // Just maintenance
            }
        }
        
        // 1. Early Generation Check
        if (m_decodeGeneration.load(std::memory_order_acquire) != job.generation) {
             if (job.voice) job.voice->playing.store(false, std::memory_order_release);
             continue;
        }

        // 2. Decode
        uint32_t sr = 0, ch = 0;
        auto buffer = loadBuffer(job.path, sr, ch);
        
        // 3. Late Generation Check
        if (m_decodeGeneration.load(std::memory_order_acquire) != job.generation) {
             if (job.voice) job.voice->playing.store(false, std::memory_order_release);
             continue;
        }
        
        // 4. Update Voice
        auto voice = job.voice;
        PreviewVoice* currentRaw = m_activeVoiceRaw.load(std::memory_order_acquire);
        if (currentRaw != voice.get()) {
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
    stop();
    
    auto cachedBuffer = SamplePool::getInstance().tryGetCached(path);
    if (cachedBuffer && cachedBuffer->ready.load(std::memory_order_acquire)) {
        return startVoiceWithBuffer(cachedBuffer, path, gainDb, maxSeconds);
    }
    
    auto voice = std::make_shared<PreviewVoice>();
    voice->path = path;
    voice->gain = dbToLinear(gainDb + m_globalGainDb.load(std::memory_order_relaxed));
    voice->maxPlaySeconds = maxSeconds;
    voice->phaseFrames = 0.0;
    voice->elapsedSeconds = 0.0;
    voice->fadeInPos = 0.0;
    voice->fadeOutPos = 0.0;
    voice->stopRequested.store(false, std::memory_order_release);
    voice->seekRequestSeconds.store(-1.0, std::memory_order_release);
    voice->fadeOutActive = false;
    voice->bufferReady.store(false, std::memory_order_release);
    voice->playing.store(true, std::memory_order_release);
    
    // RT-Safe update with zombie queuing
    PreviewVoice* oldRaw = m_activeVoiceRaw.exchange(voice.get(), std::memory_order_acq_rel);
    std::shared_ptr<PreviewVoice> oldOwner = m_activeVoiceOwner;
    m_activeVoiceOwner = voice;

    if (oldOwner) {
        std::lock_guard<std::mutex> lock(m_workerMutex);
        m_zombies.emplace_back(ZombieVoice{oldOwner, std::chrono::steady_clock::now()});
        m_workerCV.notify_one();
    }
    
    decodeAsync(path, voice);
    
    Log::info("PreviewEngine: Async decode started for '" + path + "'");
    return PreviewResult::Pending;
}

void PreviewEngine::stop() {
    PreviewVoice* voice = m_activeVoiceRaw.load(std::memory_order_acquire);
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
    // Lock-free read
    PreviewVoice* voice = m_activeVoiceRaw.load(std::memory_order_acquire);

    // If voice is null or stopped, return
    if (!voice || !voice->playing.load(std::memory_order_acquire) || !interleavedOutput) {
        return;
    }
    
    // If buffer not ready, output silence (or nothing)
    if (!voice->bufferReady.load(std::memory_order_acquire)) {
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
    
    // Check Seek
    double seekReq = voice->seekRequestSeconds.exchange(-1.0, std::memory_order_acq_rel);
    if (seekReq >= 0.0) {
        if (seekReq > voice->durationSeconds) seekReq = voice->durationSeconds;
        voice->phaseFrames = seekReq * voice->sampleRate;
        voice->elapsedSeconds = seekReq;
        voice->stopRequested.store(false, std::memory_order_release);
        voice->fadeOutActive = false;
        voice->fadeOutPos = 0.0;
        voice->fadeInPos = 0.0;
    }

    const double ratio = voice->sampleRate / streamRate;
    const uint64_t totalFrames = buffer->numFrames;
    const float* data = buffer->data.data();
    double phase = voice->phaseFrames;
    const float gain = voice->gain;
    const uint32_t srcChannels = voice->channels;

    // --- Processing Loop ---
    // (Note: SIMD optimizations temporarily removed to focus on safety logic correctness)
    
    uint32_t i = 0;
    for (; i < numFrames; ++i) {
        if (static_cast<uint64_t>(phase) >= totalFrames - 1) {
            voice->stopRequested.store(true, std::memory_order_release);
            voice->fadeOutActive = true;
            break;
        }
        
        uint64_t idx = static_cast<uint64_t>(phase);
        float frac = static_cast<float>(phase - idx);

        float outL, outR;
        
        // Dynamic gain (Fade In/Out)
        float currentGain = gain;
        if (voice->fadeOutActive) {
             float f = 1.0f - (static_cast<float>(voice->fadeOutPos) / fadeOutSamples);
             if (f < 0.0f) f = 0.0f;
             currentGain *= f;
             voice->fadeOutPos += 1.0;
             if (f <= 0.0f) {
                 voice->playing.store(false, std::memory_order_release);
             }
        } else if (voice->fadeInPos < fadeInSamples) {
             float f = static_cast<float>(voice->fadeInPos) / fadeInSamples;
             currentGain *= f;
             voice->fadeInPos += 1.0;
        }
        
        // Linear Interpolation (Fast) for Preview
        // Cubic is better but Linear is safer for raw pointer logic verification
        auto getSample = [&](int64_t index, uint32_t channel) -> float {
            if (index < 0) index = 0;
            if (index >= totalFrames) index = totalFrames - 1u;
            return (srcChannels == 1) ? data[index] : data[index * 2 + channel];
        };

        float l0 = getSample((int64_t)idx, 0);
        float l1 = getSample((int64_t)idx + 1, 0);
        outL = l0 + frac * (l1 - l0);

        if (srcChannels == 1) {
            outR = outL;
        } else {
            float r0 = getSample((int64_t)idx, 1);
            float r1 = getSample((int64_t)idx + 1, 1);
            outR = r0 + frac * (r1 - r0);
        }

        interleavedOutput[i * 2] += outL * currentGain;
        interleavedOutput[i * 2 + 1] += outR * currentGain;

        phase += ratio;
    }

    voice->phaseFrames = phase;
    voice->elapsedSeconds += static_cast<double>(numFrames) / streamRate;
    if (voice->maxPlaySeconds > 0.0 && voice->elapsedSeconds >= voice->maxPlaySeconds) {
        voice->stopRequested.store(true, std::memory_order_release);
        voice->fadeOutActive = true;
    }

    // Auto-release logic handled by UI thread (m_activeVoiceRaw check) or explicit stop()
}

bool PreviewEngine::isPlaying() const {
    PreviewVoice* voice = m_activeVoiceRaw.load(std::memory_order_acquire);
    return voice && voice->playing.load(std::memory_order_acquire);
}

bool PreviewEngine::isBufferReady() const {
    PreviewVoice* voice = m_activeVoiceRaw.load(std::memory_order_acquire);
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

void PreviewEngine::seek(double seconds) {
    PreviewVoice* voice = m_activeVoiceRaw.load(std::memory_order_acquire);
    if (voice) {
        voice->seekRequestSeconds.store(seconds, std::memory_order_release);
    }
}

double PreviewEngine::getPlaybackPosition() const {
    PreviewVoice* voice = m_activeVoiceRaw.load(std::memory_order_acquire);
    return voice ? voice->elapsedSeconds : 0.0;
}

} // namespace Audio
} // namespace Nomad
