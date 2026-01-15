// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "AuditionEngine.h"
#include "ClipSource.h"
#include "ClipResampler.h"  // Sinc64Turbo resampling
#include "MiniAudioDecoder.h"
#include "NomadLog.h"
#include "MetadataParser.h"

#include <algorithm>
#include <random>
#include <chrono>

namespace Nomad {
namespace Audio {

AuditionEngine::AuditionEngine() {
    Log::info("[AuditionEngine] Created");
}

AuditionEngine::~AuditionEngine() {
    stop();
    Log::info("[AuditionEngine] Destroyed");
}

// === Queue Management ===

void AuditionEngine::addToQueue(const std::string& filePath, bool isReference) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    
    AuditionQueueItem item;
    item.id = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    item.filePath = filePath;
    item.isReference = isReference;
    item.isFromTimeline = false;
    
    // Extract metadata using native parser
    AudioMetadata meta = MetadataParser::parse(filePath);
    
    // Apply extracted metadata (with fallbacks)
    item.title = meta.title.empty() ? "" : meta.title;
    item.artist = meta.artist.empty() ? (isReference ? "Reference Track" : "Unknown Artist") : meta.artist;
    item.album = meta.album;
    item.durationSeconds = meta.durationSeconds;
    item.coverArtData = std::move(meta.coverArtData);
    item.coverArtMimeType = meta.coverArtMimeType;
    
    // Fallback: Title from filename if metadata is empty
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
    
    Log::info("[AuditionEngine] Added to queue: " + item.title + " (" + item.artist + ")");
    if (!item.coverArtData.empty()) {
        Log::info("[AuditionEngine] Cover art extracted: " + std::to_string(item.coverArtData.size()) + " bytes");
    }
    
    m_queue.push_back(std::move(item));
    
    // Auto-load if this is the first track, but DO NOT auto-play
    if (m_queue.size() == 1 && m_currentIndex < 0) {
        m_currentIndex = 0;
        loadCurrentTrack();
        // Explicitly ensure we are stopped
        m_isPlaying.store(false);
        if (m_onPlaybackStateChanged) m_onPlaybackStateChanged(false);
    }

    if (m_onQueueUpdated) {
        m_onQueueUpdated();
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
    stop();
    m_queue.clear();
    m_currentIndex = -1;
    m_currentSource.reset();
    Log::info("[AuditionEngine] Queue cleared");
}

std::optional<AuditionQueueItem> AuditionEngine::getCurrentItem() const {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int32_t>(m_queue.size())) {
        return m_queue[static_cast<size_t>(m_currentIndex)];
    }
    return std::nullopt;
}

void AuditionEngine::nextTrack() {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (m_queue.empty()) return;
    
    if (m_shuffle.load()) {
        // Random next
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
    
    loadCurrentTrack();
}

void AuditionEngine::previousTrack() {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (m_queue.empty()) return;
    
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
    
    loadCurrentTrack();
}

void AuditionEngine::jumpToTrack(size_t index) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (index >= m_queue.size()) return;
    
    m_currentIndex = static_cast<int32_t>(index);
    loadCurrentTrack();
}

// === Transport Control ===

void AuditionEngine::play() {
    if (m_currentIndex < 0 && !m_queue.empty()) {
        jumpToTrack(0);
    }
    
    bool wasPlaying = m_isPlaying.exchange(true);
    if (!wasPlaying && m_onPlaybackStateChanged) {
        m_onPlaybackStateChanged(true);
    }
    
    Log::info("[AuditionEngine] Play");
}

void AuditionEngine::pause() {
    bool wasPlaying = m_isPlaying.exchange(false);
    if (wasPlaying && m_onPlaybackStateChanged) {
        m_onPlaybackStateChanged(false);
    }
    
    Log::info("[AuditionEngine] Pause");
}

void AuditionEngine::stop() {
    m_isPlaying.store(false);
    m_positionSeconds.store(0.0);
    
    if (m_onPlaybackStateChanged) {
        m_onPlaybackStateChanged(false);
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
    if (m_currentSource) {
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
    
    if (m_onPositionChanged) {
        m_onPositionChanged(seconds);
    }
}

double AuditionEngine::getPositionNormalized() const {
    double duration = getDurationSeconds();
    if (duration <= 0.0) return 0.0;
    return m_positionSeconds.load() / duration;
}

double AuditionEngine::getPositionSeconds() const {
    return m_positionSeconds.load();
}

double AuditionEngine::getDurationSeconds() const {
    if (m_currentSource) {
        return m_currentSource->getDurationSeconds();
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
    
    if (!m_isPlaying.load() || !m_currentSource) {
        return;
    }
    
    // Read from source
    const auto* srcBuffer = m_currentSource->getRawBuffer();
    if (!srcBuffer || !srcBuffer->isValid()) return;
    
    double srcRate = static_cast<double>(srcBuffer->sampleRate);
    double dstRate = m_sampleRate.load();
    double currentPos = m_positionSeconds.load();
    double ratio = srcRate / dstRate;
    
    // Safety check for rates
    if (srcRate <= 0.0 || dstRate <= 0.0) return;
    
    // Get interleaved data pointer for Sinc64 interpolation
    const float* interleavedData = srcBuffer->interleavedData.data();
    const int64_t totalFrames = static_cast<int64_t>(srcBuffer->numFrames);
    const uint32_t srcChannels = srcBuffer->numChannels;
    
    // Process frames with SINC64 TURBO (mastering-grade quality)
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
            static ClipResampler resampler;
            resampler.setQuality(ClipResamplingQuality::High);
            
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                uint32_t srcCh = (srcChannels == 1) ? 0 : ch;
                output[i * numChannels + ch] = resampler.getSample(
                    interleavedData, totalFrames, srcChannels, srcPosition, srcCh);
            }
        }
    }
    
    // Apply Master Volume
    float vol = m_volume.load(std::memory_order_relaxed);
    if (vol < 1.0f) {
        for (uint32_t i = 0; i < numFrames * numChannels; ++i) {
            output[i] *= vol;
        }
    }
    
    m_positionSeconds.store(currentPos);
    
    // Check for end of track
    double duration = getDurationSeconds();
    if (currentPos >= duration && duration > 0.0) {
        if (m_repeatMode == RepeatMode::One) {
            seekSeconds(0.0);
        } else {
            nextTrack();
        }
    }
    
    // Apply DSP chain if enabled
    if (m_abWetMode.load() && m_currentPreset.enabled) {
        applyDSPChain(output, numFrames, numChannels);
    }
    
    // Position callback (throttled)
    static int callbackCounter = 0;
    if (++callbackCounter % 10 == 0 && m_onPositionChanged) {
        m_onPositionChanged(currentPos);
    }
}

// === Internal Helpers ===

void AuditionEngine::loadCurrentTrack() {
    if (m_currentIndex < 0 || m_currentIndex >= static_cast<int32_t>(m_queue.size())) {
        m_currentSource.reset();
        return;
    }
    
    const auto& item = m_queue[static_cast<size_t>(m_currentIndex)];
    
    if (item.isFromTimeline) {
        // TODO: Render timeline track to temp buffer
        Log::info("[AuditionEngine] Loading timeline track: " + item.title);
    } else {
        // Load from file
        Log::info("[AuditionEngine] Loading file: " + item.filePath);
        
        // Decode audio file
        std::vector<float> decodedData;
        uint32_t sr = 0;
        uint32_t ch = 0;
        
        if (decodeAudioFile(item.filePath, decodedData, sr, ch)) {
            // Create AudioBufferData
            auto bufferData = std::make_shared<AudioBufferData>();
            bufferData->interleavedData = std::move(decodedData);
            bufferData->sampleRate = sr;
            bufferData->numChannels = ch;
            bufferData->numFrames = (ch > 0) ? bufferData->interleavedData.size() / ch : 0;
            
            // Create ClipSource
            try {
                // Using generic ID 0 for now as we don't use SourceManager here yet
                ClipSourceID id{0}; 
                m_currentSource = std::make_shared<ClipSource>(id, "AuditionSource");
                m_currentSource->setFilePath(item.filePath);
                m_currentSource->setBuffer(bufferData);
                
                Log::info("[AuditionEngine] Source loaded: " + std::to_string(bufferData->durationSeconds()) + "s");
                
            } catch (const std::exception& e) {
                Log::error("[AuditionEngine] Exception creating source: " + std::string(e.what()));
                m_currentSource.reset();
            }
        } else {
            Log::error("[AuditionEngine] Failed to decode file: " + item.filePath);
            m_currentSource.reset();
        }
    }
    
    m_positionSeconds.store(item.lastPosition);
    
    notifyTrackChanged();
    
    // Auto-play when track changes
    if (!m_isPlaying.load()) {
        play();
    }
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
    auto calcShelfCoeffs = [sampleRate](float db, float freq, bool isLowShelf, 
                                      float& b0, float& b1, float& b2, float& a1, float& a2) {
        if (std::abs(db) < 0.1f) {
            b0 = 1.0f; b1 = 0.0f; b2 = 0.0f; a1 = 0.0f; a2 = 0.0f;
            return;
        }
        
        float A = std::pow(10.0f, db / 40.0f);
        float w0 = 2.0f * 3.14159f * freq / sampleRate;
        float alpha = std::sin(w0) / 2.0f * 1.414f; // Q ~= 0.707
        float cosw0 = std::cos(w0);
        float a0_norm;
        
        if (isLowShelf) {
            // Low Shelf
            float Am1 = A - 1; float Ap1 = A + 1;
            float sqA = 2.0f * std::sqrt(A) * alpha;
            
            b0 = A * ((Ap1) - (Am1)*cosw0 + sqA);
            b1 = 2.0f * A * ((Am1) - (Ap1)*cosw0);
            b2 = A * ((Ap1) - (Am1)*cosw0 - sqA);
            a0_norm = (Ap1) + (Am1)*cosw0 + sqA;
            a1 = -2.0f * ((Am1) + (Ap1)*cosw0);
            a2 = (Ap1) + (Am1)*cosw0 - sqA;
        } else {
            // High Shelf
            float Am1 = A - 1; float Ap1 = A + 1;
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
        b0 *= invA0; b1 *= invA0; b2 *= invA0;
        a1 *= invA0; a2 *= invA0;
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
            left = processBiquad(left, ls_b0, ls_b1, ls_b2, ls_a1, ls_a2, m_dspState.lowShelf.z1_L, m_dspState.lowShelf.z2_L);
            right = processBiquad(right, ls_b0, ls_b1, ls_b2, ls_a1, ls_a2, m_dspState.lowShelf.z1_R, m_dspState.lowShelf.z2_R);
        }
        
        // Treble Shelf
        if (std::abs(m_currentPreset.trebleShelfDb) > 0.1f) {
            left = processBiquad(left, hs_b0, hs_b1, hs_b2, hs_a1, hs_a2, m_dspState.highShelf.z1_L, m_dspState.highShelf.z2_L);
            right = processBiquad(right, hs_b0, hs_b1, hs_b2, hs_a1, hs_a2, m_dspState.highShelf.z1_R, m_dspState.highShelf.z2_R);
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

void AuditionEngine::notifyTrackChanged() {
    if (m_onTrackChanged && m_currentIndex >= 0 && 
        m_currentIndex < static_cast<int32_t>(m_queue.size())) {
        m_onTrackChanged(m_queue[static_cast<size_t>(m_currentIndex)]);
    }
}

} // namespace Audio
} // namespace Nomad
