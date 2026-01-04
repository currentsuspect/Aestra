// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PlaylistRuntimeSnapshot.h"
#include "TimeTypes.h"
#include "MixerSIMD.h"
#include "ClipResampler.h"
#include "FastMath.h"
#include <cmath>
#include <cstring>

namespace Nomad {
namespace Audio {

// =============================================================================
// PlaylistMixer - RT-safe audio mixing
// =============================================================================

/**
 * @brief Real-time safe audio mixer for playlist clips
 * 
 * This class handles all audio mixing operations on the RT thread.
 * It is designed to be:
 * - Lock-free (no mutexes)
 * - Allocation-free (uses pre-allocated buffers)
 * - Cache-friendly (linear memory access patterns)
 * 
 * Usage in audio callback:
 * ```
 * void processBlock(float* outL, float* outR, uint32_t numFrames) {
 *     SampleIndex bufferStart = getPlayheadPosition();
 *     const auto* snapshot = m_snapshotManager.getCurrentSnapshot();
 *     PlaylistMixer::process(snapshot, bufferStart, outL, outR, numFrames, 
 *                            m_trackBuffer.data(), m_clipBuffer.data());
 * }
 * ```
 */
class PlaylistMixer {
public:
    /// Maximum supported buffer size (for stack allocation)
    static constexpr SampleCount MAX_BUFFER_SIZE = 8192;
    
    /// Maximum supported channel count
    static constexpr uint32_t MAX_CHANNELS = 8;
    
    /// Global resampling quality for clip playback (thread-safe atomic)
    static inline ClipResamplingQuality s_resamplingQuality = ClipResamplingQuality::High;
    
    /// Set global resampling quality (called from AudioSettingsDialog)
    static void setResamplingQuality(ClipResamplingQuality quality) noexcept {
        s_resamplingQuality = quality;
    }
    
    /// Get current resampling quality
    static ClipResamplingQuality getResamplingQuality() noexcept {
        return s_resamplingQuality;
    }
    
    /**
     * @brief Process a buffer using the playlist snapshot
     * 
     * @param snapshot Current playlist snapshot (read-only)
     * @param bufferStartTime Timeline position of buffer start (samples)
     * @param outputLeft Output buffer for left channel
     * @param outputRight Output buffer for right channel  
     * @param numFrames Number of frames to process
     * @param trackBuffer Pre-allocated track mixing buffer (2 * MAX_BUFFER_SIZE)
     * @param clipBuffer Pre-allocated clip mixing buffer (2 * MAX_BUFFER_SIZE)
     */
    static void process(const PlaylistRuntimeSnapshot* snapshot,
                        SampleIndex bufferStartTime,
                        float* outputLeft,
                        float* outputRight,
                        SampleCount numFrames,
                        float* trackBuffer,
                        float* clipBuffer);
    
    /**
     * @brief Process to interleaved stereo output
     */
    static void processInterleaved(const PlaylistRuntimeSnapshot* snapshot,
                                    SampleIndex bufferStartTime,
                                    float* outputInterleaved,
                                    SampleCount numFrames,
                                    float* trackBuffer,
                                    float* clipBuffer);

private:
    /// Mix a single clip into a track buffer
    static void mixClipIntoBuffer(const ClipRuntimeInfo& clip,
                                   float* leftBuffer,
                                   float* rightBuffer,
                                   SampleCount numFrames,
                                   SampleIndex bufferStartTime,
                                   double projectSampleRate);
    
    /// Apply panning to a stereo signal
    static void applyPan(float pan, float& leftGain, float& rightGain);
    
    /// Linear interpolation sample fetch
    static float sampleWithInterpolation(const float* data, 
                                          double sampleIndex,
                                          uint32_t channel,
                                          uint32_t numChannels,
                                          SampleIndex numFrames);
};

// =============================================================================
// PlaylistMixer Implementation (inline for RT performance)
// =============================================================================

inline void PlaylistMixer::process(const PlaylistRuntimeSnapshot* snapshot,
                                    SampleIndex bufferStartTime,
                                    float* outputLeft,
                                    float* outputRight,
                                    SampleCount numFrames,
                                    float* trackBuffer,
                                    float* clipBuffer) {
    // Clear output (SIMD-accelerated)
    MixerSIMD::clearBuffer(outputLeft, numFrames);
    MixerSIMD::clearBuffer(outputRight, numFrames);
    
    if (!snapshot || snapshot->lanes.empty()) {
        return;
    }
    
    SampleIndex bufferEndTime = bufferStartTime + numFrames;
    bool hasSolo = snapshot->hasSoloLane();
    
    // Process each lane
    for (size_t laneIdx = 0; laneIdx < snapshot->lanes.size(); ++laneIdx) {
        const LaneRuntimeInfo& lane = snapshot->lanes[laneIdx];
        
        // Skip if not audible
        if (!snapshot->isLaneAudible(laneIdx, hasSolo)) {
            continue;
        }
        
        // Clear track buffer (SIMD-accelerated)
        float* trackL = trackBuffer;
        float* trackR = trackBuffer + MAX_BUFFER_SIZE;
        MixerSIMD::clearBuffer(trackL, numFrames);
        MixerSIMD::clearBuffer(trackR, numFrames);
        
        // Get clips that overlap this buffer (using binary search)
        auto [firstClip, lastClip] = lane.getClipRangeForBuffer(bufferStartTime, bufferEndTime);
        
        // Mix overlapping clips into track buffer
        for (size_t i = firstClip; i < lastClip; ++i) {
            const ClipRuntimeInfo& clip = lane.clips[i];
            
            if (clip.muted || !clip.isValid()) {
                continue;
            }
            
            // Double-check overlap (binary search is approximate)
            if (!clip.overlaps(bufferStartTime, bufferEndTime)) {
                continue;
            }
            
            mixClipIntoBuffer(clip, trackL, trackR, numFrames, 
                              bufferStartTime, snapshot->projectSampleRate);
        }
        
        // Apply lane volume and pan, mix into output (SIMD-accelerated)
        float leftGain, rightGain;
        applyPan(lane.pan, leftGain, rightGain);
        leftGain *= lane.volume;
        rightGain *= lane.volume;
        
        MixerSIMD::mixStereoWithGain(trackL, trackR, outputLeft, outputRight,
                                      leftGain, rightGain, numFrames);
    }
}

inline void PlaylistMixer::processInterleaved(const PlaylistRuntimeSnapshot* snapshot,
                                               SampleIndex bufferStartTime,
                                               float* outputInterleaved,
                                               SampleCount numFrames,
                                               float* trackBuffer,
                                               float* clipBuffer) {
    // Use clip buffer as temporary L/R storage
    float* tempL = clipBuffer;
    float* tempR = clipBuffer + MAX_BUFFER_SIZE;
    
    process(snapshot, bufferStartTime, tempL, tempR, numFrames, trackBuffer, clipBuffer);
    
    // Interleave
    for (SampleCount i = 0; i < numFrames; ++i) {
        outputInterleaved[i * 2] = tempL[i];
        outputInterleaved[i * 2 + 1] = tempR[i];
    }
}

inline void PlaylistMixer::mixClipIntoBuffer(const ClipRuntimeInfo& clip,
                                              float* leftBuffer,
                                              float* rightBuffer,
                                              SampleCount numFrames,
                                              SampleIndex bufferStartTime,
                                              double projectSampleRate) {
    if (!clip.audioData || clip.sourceSampleRate == 0) {
        return;
    }
    
    SampleIndex bufferEndTime = bufferStartTime + numFrames;
    SampleIndex clipEnd = clip.getEndTime();
    
    // Calculate the overlap region
    SampleIndex mixStart = std::max(bufferStartTime, clip.startTime);
    SampleIndex mixEnd = std::min(bufferEndTime, clipEnd);
    
    if (mixStart >= mixEnd) {
        return;  // No overlap
    }
    
    SampleCount mixFrames = static_cast<SampleCount>(mixEnd - mixStart);
    
    // Buffer offset (where in the output buffer to start writing)
    SampleCount bufferOffset = static_cast<SampleCount>(mixStart - bufferStartTime);
    
    // Source offset (where in the source audio to start reading)
    SampleIndex clipOffsetFromStart = mixStart - clip.startTime;
    
    // Calculate sample rate conversion ratio
    double srcRatio = static_cast<double>(clip.sourceSampleRate) / projectSampleRate;
    srcRatio *= clip.playbackRate;  // Apply playback rate
    
    // Calculate starting source position
    double sourceStartIndex = static_cast<double>(clip.sourceStart) + 
                               clipOffsetFromStart * srcRatio;
    
    // Prepare pan gains
    float panL, panR;
    applyPan(clip.pan, panL, panR);
    
    const float* srcData = clip.audioData->interleavedData.data();
    uint32_t srcChannels = clip.sourceChannels;
    SampleIndex srcFrames = clip.audioData->numFrames;
    
    // Use ClipResampler for high-quality interpolation
    static thread_local ClipResampler resampler;
    resampler.setQuality(s_resamplingQuality);
    
    // Process each frame
    for (SampleCount i = 0; i < mixFrames; ++i) {
        SampleIndex bufferIdx = bufferOffset + i;
        double sourceIdx = sourceStartIndex + i * srcRatio;
        
        // Calculate gain (including fades)
        SampleIndex timelinePos = mixStart + i;
        float gain = clip.getGainAt(timelinePos);
        
        // Fetch samples with high-quality interpolation
        float sampleL, sampleR;
        
        if (srcChannels >= 2) {
            // Stereo: use optimized stereo path
            resampler.getSampleStereo(srcData, srcFrames, sourceIdx, sampleL, sampleR);
        } else {
            // Mono source: duplicate to stereo
            sampleL = sampleR = resampler.getSample(srcData, srcFrames, srcChannels, sourceIdx, 0);
        }
        
        // Apply gain and pan
        leftBuffer[bufferIdx] += sampleL * gain * panL;
        rightBuffer[bufferIdx] += sampleR * gain * panR;
    }
}

inline void PlaylistMixer::applyPan(float pan, float& leftGain, float& rightGain) {
    // Fast constant power panning using polynomial approximation
    // Avoids expensive std::sin/cos calls in inner loop
    FastMath::fastPan(pan, leftGain, rightGain);
}

inline float PlaylistMixer::sampleWithInterpolation(const float* data,
                                                      double sampleIndex,
                                                      uint32_t channel,
                                                      uint32_t numChannels,
                                                      SampleIndex numFrames) {
    if (sampleIndex < 0 || sampleIndex >= numFrames - 1) {
        // Edge cases: clamp or return silence
        if (sampleIndex < 0) return 0.0f;
        if (sampleIndex >= numFrames) return 0.0f;
        // Last sample
        SampleIndex idx = static_cast<SampleIndex>(sampleIndex);
        return data[idx * numChannels + channel];
    }
    
    // Linear interpolation
    SampleIndex idx0 = static_cast<SampleIndex>(sampleIndex);
    SampleIndex idx1 = idx0 + 1;
    float frac = static_cast<float>(sampleIndex - idx0);
    
    float s0 = data[idx0 * numChannels + channel];
    float s1 = data[idx1 * numChannels + channel];
    
    return s0 + frac * (s1 - s0);
}

} // namespace Audio
} // namespace Nomad
