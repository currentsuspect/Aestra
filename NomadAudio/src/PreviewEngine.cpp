// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "PreviewEngine.h"
#include "NomadLog.h"
#include "MiniAudioDecoder.h"
#include "PathUtils.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <immintrin.h>

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
    voice->fadeInPos = 0.0;
    voice->fadeOutPos = 0.0;
    voice->stopRequested.store(false, std::memory_order_release);
    voice->seekRequestSeconds.store(-1.0, std::memory_order_release);
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
    voice->fadeOutPos = 0.0;
    voice->stopRequested.store(false, std::memory_order_release);
    voice->seekRequestSeconds.store(-1.0, std::memory_order_release);
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
    
    // Check Seek Request
    double seekReq = voice->seekRequestSeconds.exchange(-1.0, std::memory_order_acq_rel);
    if (seekReq >= 0.0) {
        // Clamp to duration
        if (seekReq > voice->durationSeconds) seekReq = voice->durationSeconds;
        voice->phaseFrames = seekReq * voice->sampleRate;
        voice->elapsedSeconds = seekReq;
        
        // Reset stop/fade state so seeking back from the end works
        voice->stopRequested.store(false, std::memory_order_release);
        voice->fadeOutActive = false;
        voice->fadeOutPos = 0.0;
        voice->fadeInPos = 0.0; // Smooth transition
    }

    const double ratio = voice->sampleRate / streamRate;
    const uint64_t totalFrames = buffer->numFrames;
    const float* data = buffer->data.data();
    double phase = voice->phaseFrames;
    const float gain = voice->gain;
    const uint32_t srcChannels = voice->channels;

    // SIMD Helper: 4-wide Cubic Hermite Spline
    auto cubicSIMD = [&](__m128 p0, __m128 p1, __m128 p2, __m128 p3, __m128 t) -> __m128 {
        const __m128 vHalf = _mm_set1_ps(0.5f);
        const __m128 vOnePointFive = _mm_set1_ps(1.5f);
        const __m128 vTwo = _mm_set1_ps(2.0f);
        const __m128 vTwoPointFive = _mm_set1_ps(2.5f);

        // a = -0.5*p0 + 1.5*p1 - 1.5*p2 + 0.5*p3
        __m128 a = _mm_mul_ps(p0, _mm_set1_ps(-0.5f));
        a = _mm_add_ps(a, _mm_mul_ps(p1, vOnePointFive));
        a = _mm_sub_ps(a, _mm_mul_ps(p2, vOnePointFive));
        a = _mm_add_ps(a, _mm_mul_ps(p3, vHalf));
        
        // b = p0 - 2.5*p1 + 2.0*p2 - 0.5*p3
        __m128 b = p0;
        b = _mm_sub_ps(b, _mm_mul_ps(p1, vTwoPointFive));
        b = _mm_add_ps(b, _mm_mul_ps(p2, vTwo));
        b = _mm_sub_ps(b, _mm_mul_ps(p3, vHalf));
        
        // c = -0.5*p0 + 0.5*p2
        __m128 c = _mm_mul_ps(p0, _mm_set1_ps(-0.5f));
        c = _mm_add_ps(c, _mm_mul_ps(p2, vHalf));
        
        // d = p1
        __m128 d = p1;
        
        // Result = a*t^3 + b*t^2 + c*t + d
        __m128 t2 = _mm_mul_ps(t, t);
        __m128 t3 = _mm_mul_ps(t2, t);
        
        __m128 res = _mm_mul_ps(a, t3);
        res = _mm_add_ps(res, _mm_mul_ps(b, t2));
        res = _mm_add_ps(res, _mm_mul_ps(c, t));
        res = _mm_add_ps(res, d);
        return res;
    };

    uint32_t i = 0;

    // --- SIMD LOOP (Process 4 frames at a time) ---
    // Safety Limit: ensuring phase + 4*ratio + lookahead(2) is within [0, totalFrames-1]
    const uint64_t safeLimit = totalFrames > 6 ? totalFrames - 5 : 0;
    const __m128 vGain = _mm_set1_ps(gain);

    // Only run SIMD loop if not fading (fades modify gain per-sample)
    bool isFading = (voice->fadeInPos < fadeInSamples) || 
                    (voice->stopRequested.load(std::memory_order_relaxed)) || 
                    voice->fadeOutActive;

    if (!isFading && safeLimit > 0) {
        for (; i + 4 <= numFrames; i += 4) {
             if (static_cast<uint64_t>(phase + ratio * 4.0) >= safeLimit) {
                 break; 
             }

             double ph0 = phase;
             double ph1 = phase + ratio;
             double ph2 = phase + 2.0 * ratio;
             double ph3 = phase + 3.0 * ratio;

             int64_t idx0 = static_cast<int64_t>(ph0);
             int64_t idx1 = static_cast<int64_t>(ph1);
             int64_t idx2 = static_cast<int64_t>(ph2);
             int64_t idx3 = static_cast<int64_t>(ph3);

             float fr0 = static_cast<float>(ph0 - idx0);
             float fr1 = static_cast<float>(ph1 - idx1);
             float fr2 = static_cast<float>(ph2 - idx2);
             float fr3 = static_cast<float>(ph3 - idx3);

             __m128 vFrac = _mm_set_ps(fr3, fr2, fr1, fr0);
             __m128 vOutL, vOutR;

             if (srcChannels == 1) {
                 // Mono Gather
                 float m0_0 = data[idx0 - 1], m0_1 = data[idx0], m0_2 = data[idx0 + 1], m0_3 = data[idx0 + 2];
                 float m1_0 = data[idx1 - 1], m1_1 = data[idx1], m1_2 = data[idx1 + 1], m1_3 = data[idx1 + 2];
                 float m2_0 = data[idx2 - 1], m2_1 = data[idx2], m2_2 = data[idx2 + 1], m2_3 = data[idx2 + 2];
                 float m3_0 = data[idx3 - 1], m3_1 = data[idx3], m3_2 = data[idx3 + 1], m3_3 = data[idx3 + 2];

                 __m128 vP0 = _mm_set_ps(m3_0, m2_0, m1_0, m0_0);
                 __m128 vP1 = _mm_set_ps(m3_1, m2_1, m1_1, m0_1);
                 __m128 vP2 = _mm_set_ps(m3_2, m2_2, m1_2, m0_2);
                 __m128 vP3 = _mm_set_ps(m3_3, m2_3, m1_3, m0_3);

                 vOutL = cubicSIMD(vP0, vP1, vP2, vP3, vFrac);
                 vOutR = vOutL; // Duplicate
             } else {
                 // Stereo Gather
                 // Left
                 float l0_0 = data[(idx0-1)*2], l0_1 = data[idx0*2], l0_2 = data[(idx0+1)*2], l0_3 = data[(idx0+2)*2];
                 float l1_0 = data[(idx1-1)*2], l1_1 = data[idx1*2], l1_2 = data[(idx1+1)*2], l1_3 = data[(idx1+2)*2];
                 float l2_0 = data[(idx2-1)*2], l2_1 = data[idx2*2], l2_2 = data[(idx2+1)*2], l2_3 = data[(idx2+2)*2];
                 float l3_0 = data[(idx3-1)*2], l3_1 = data[idx3*2], l3_2 = data[(idx3+1)*2], l3_3 = data[(idx3+2)*2];
                 
                 __m128 vP0L = _mm_set_ps(l3_0, l2_0, l1_0, l0_0);
                 __m128 vP1L = _mm_set_ps(l3_1, l2_1, l1_1, l0_1);
                 __m128 vP2L = _mm_set_ps(l3_2, l2_2, l1_2, l0_2);
                 __m128 vP3L = _mm_set_ps(l3_3, l2_3, l1_3, l0_3);
                 vOutL = cubicSIMD(vP0L, vP1L, vP2L, vP3L, vFrac);

                 // Right
                 float r0_0 = data[(idx0-1)*2+1], r0_1 = data[idx0*2+1], r0_2 = data[(idx0+1)*2+1], r0_3 = data[(idx0+2)*2+1];
                 float r1_0 = data[(idx1-1)*2+1], r1_1 = data[idx1*2+1], r1_2 = data[(idx1+1)*2+1], r1_3 = data[(idx1+2)*2+1];
                 float r2_0 = data[(idx2-1)*2+1], r2_1 = data[idx2*2+1], r2_2 = data[(idx2+1)*2+1], r2_3 = data[(idx2+2)*2+1];
                 float r3_0 = data[(idx3-1)*2+1], r3_1 = data[idx3*2+1], r3_2 = data[(idx3+1)*2+1], r3_3 = data[(idx3+2)*2+1];
                 
                 __m128 vP0R = _mm_set_ps(r3_0, r2_0, r1_0, r0_0);
                 __m128 vP1R = _mm_set_ps(r3_1, r2_1, r1_1, r0_1);
                 __m128 vP2R = _mm_set_ps(r3_2, r2_2, r1_2, r0_2);
                 __m128 vP3R = _mm_set_ps(r3_3, r2_3, r1_3, r0_3);
                 vOutR = cubicSIMD(vP0R, vP1R, vP2R, vP3R, vFrac);
             }

             // Apply Gain
             vOutL = _mm_mul_ps(vOutL, vGain);
             vOutR = _mm_mul_ps(vOutR, vGain);

             // Store Interleaved (L0 R0 L1 R1...)
             __m128 vLo = _mm_unpacklo_ps(vOutL, vOutR);
             __m128 vHi = _mm_unpackhi_ps(vOutL, vOutR);
             
             // Accumulate (Add to existing output)
             __m128 vDestLo = _mm_loadu_ps(interleavedOutput + i * 2);
             __m128 vDestHi = _mm_loadu_ps(interleavedOutput + i * 2 + 4);
             
             vDestLo = _mm_add_ps(vDestLo, vLo);
             vDestHi = _mm_add_ps(vDestHi, vHi);
             
             _mm_storeu_ps(interleavedOutput + i * 2, vDestLo);
             _mm_storeu_ps(interleavedOutput + i * 2 + 4, vDestHi);

             phase += ratio * 4.0;
        }
    }

    // Scalar Helper
    auto cubic = [](float p0, float p1, float p2, float p3, float t) {
        float a = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
        float b = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
        float c = -0.5f * p0 + 0.5f * p2;
        float d = p1;
        return a*t*t*t + b*t*t + c*t + d;
    };
    
    // Scalar Loop (Cleanup & Fades)
    for (; i < numFrames; ++i) {
        if (static_cast<uint64_t>(phase) >= totalFrames - 1) {
            voice->stopRequested.store(true, std::memory_order_release);
            voice->fadeOutActive = true;
            break;
        }
        
        uint64_t idx = static_cast<uint64_t>(phase);
        float frac = static_cast<float>(phase - idx);

        float outL, outR;
        
        // Calculate dynamic gain (Fade In/Out)
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

void PreviewEngine::seek(double seconds) {
    auto voice = std::atomic_load_explicit(&m_activeVoice, std::memory_order_acquire);
    if (voice) {
        voice->seekRequestSeconds.store(seconds, std::memory_order_release);
    }
}

double PreviewEngine::getPlaybackPosition() const {
    auto voice = std::atomic_load_explicit(&m_activeVoice, std::memory_order_acquire);
    return voice ? voice->elapsedSeconds : 0.0;
}

} // namespace Audio
} // namespace Nomad
