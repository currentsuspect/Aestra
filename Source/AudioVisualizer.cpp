// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioVisualizer.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <iostream>

namespace NomadUI {

AudioVisualizer::AudioVisualizer()
    : NUIComponent()
    , leftPeak_(0.0f)
    , rightPeak_(0.0f)
    , leftRMS_(0.0f)
    , rightRMS_(0.0f)
    , leftPeakHold_(0.0f)
    , rightPeakHold_(0.0f)
    , leftPeakSmoothed_(0.0f)
    , rightPeakSmoothed_(0.0f)
    , leftRMSSmoothed_(0.0f)
    , rightRMSSmoothed_(0.0f)
    , mode_(AudioVisualizationMode::Waveform)
    , sensitivity_(0.8f)
    , decayRate_(0.95f)
    , primaryColor_(NUIColor(0.0f, 0.737f, 0.831f)) // #00bcd4 - Accent Cyan
    , secondaryColor_(NUIColor(1.0f, 0.251f, 0.506f)) // #ff4081 - Accent Magenta
    , showStereo_(true)
    , showPeakHold_(true)
    , displayBufferSize_(1024)
    , currentSample_(0)
    , animationTime_(0.0f)
    , peakDecayTime_(0.0f)
    , smoothingFactor_(0.85f)  // Smooth but responsive (0.85 = 15% new data, 85% old)
    , audioManager_(nullptr)
{
    setSize(400, 200); // Default size
    
    // Initialize display buffer
    displayBuffer_.resize(displayBufferSize_ * 2); // Stereo
    
    // Load Liminal Dark v2.0 theme colors
    auto& themeManager = NUIThemeManager::getInstance();
    backgroundColor_ = themeManager.getColor("backgroundPrimary");  // #121214 - Deep charcoal with gradient
    gridColor_ = themeManager.getColor("border");                   // #2e2e35 - Grid lines
    textColor_ = themeManager.getColor("textPrimary");              // #e6e6eb - Soft white
}

void AudioVisualizer::onRender(NUIRenderer& renderer) {
    NUIRect bounds = getBounds();
    if (bounds.isEmpty()) return;
    
    // Render background for all modes. Compact meters still want a defined
    // component area; any glow should be kept inside bounds by callers.
    renderer.fillRoundedRect(bounds, 8, backgroundColor_);
    renderer.strokeRoundedRect(bounds, 8, 1, gridColor_);
    
    // Render based on current mode
    switch (mode_) {
        case AudioVisualizationMode::Waveform:
            renderWaveform(renderer);
            break;
        case AudioVisualizationMode::Spectrum:
            renderSpectrum(renderer);
            break;
        case AudioVisualizationMode::LevelMeter:
            renderLevelMeter(renderer);
            break;
        case AudioVisualizationMode::VU:
            renderVU(renderer);
            break;
        case AudioVisualizationMode::Oscilloscope:
            renderOscilloscope(renderer);
            break;
        case AudioVisualizationMode::CompactMeter:
            renderCompactMeter(renderer);
            break;
        case AudioVisualizationMode::CompactWaveform:
            renderCompactWaveform(renderer);
            break;
        case AudioVisualizationMode::ArrangementWaveform:
            renderArrangementWaveform(renderer);
            break;
    }
}

void AudioVisualizer::onUpdate(double deltaTime) {
    animationTime_ += static_cast<float>(deltaTime);

    const float dt = static_cast<float>(std::max(0.0, deltaTime));

    // Get current raw values
    const float leftPeakCurrent = leftPeak_.load();
    const float rightPeakCurrent = rightPeak_.load();
    const float leftRMSCurrent = leftRMS_.load();
    const float rightRMSCurrent = rightRMS_.load();

    // Previous smoothed values
    const float leftPeakPrev = leftPeakSmoothed_.load();
    const float rightPeakPrev = rightPeakSmoothed_.load();
    const float leftRMSPrev = leftRMSSmoothed_.load();
    const float rightRMSPrev = rightRMSSmoothed_.load();

    // Time-based ballistics for consistent feel across FPS
    // Peak should feel punchy/instantaneous (FL-style), especially in compact meters.
    const float peakReleaseSec = 0.12f;   // ~120ms falloff
    const float rmsAttackSec = 0.05f;     // 50ms rise
    const float rmsReleaseSec = 0.25f;    // 250ms falloff

    const float peakReleaseCoeff = std::exp(-dt / peakReleaseSec);
    const float rmsAttackCoeff = std::exp(-dt / rmsAttackSec);
    const float rmsReleaseCoeff = std::exp(-dt / rmsReleaseSec);

    const float leftPeakNew = (leftPeakCurrent >= leftPeakPrev)
        ? leftPeakCurrent
        : leftPeakPrev * peakReleaseCoeff + leftPeakCurrent * (1.0f - peakReleaseCoeff);
    const float rightPeakNew = (rightPeakCurrent >= rightPeakPrev)
        ? rightPeakCurrent
        : rightPeakPrev * peakReleaseCoeff + rightPeakCurrent * (1.0f - peakReleaseCoeff);

    const float leftRMSNew = (leftRMSCurrent >= leftRMSPrev)
        ? leftRMSCurrent * (1.0f - rmsAttackCoeff) + leftRMSPrev * rmsAttackCoeff
        : leftRMSPrev * rmsReleaseCoeff + leftRMSCurrent * (1.0f - rmsReleaseCoeff);
    const float rightRMSNew = (rightRMSCurrent >= rightRMSPrev)
        ? rightRMSCurrent * (1.0f - rmsAttackCoeff) + rightRMSPrev * rmsAttackCoeff
        : rightRMSPrev * rmsReleaseCoeff + rightRMSCurrent * (1.0f - rmsReleaseCoeff);

    leftPeakSmoothed_.store(leftPeakNew);
    rightPeakSmoothed_.store(rightPeakNew);
    leftRMSSmoothed_.store(leftRMSNew);
    rightRMSSmoothed_.store(rightRMSNew);

    // Peak hold with a short hold time, then smooth decay
    const float holdSec = 0.75f;
    const float holdDecaySec = 0.4f;
    const float holdDecayCoeff = std::exp(-dt / holdDecaySec);

    float leftHold = leftPeakHold_.load();
    if (leftPeakCurrent >= leftHold) {
        leftHold = leftPeakCurrent;
        leftPeakHoldTimer_ = 0.0f;
    } else {
        leftPeakHoldTimer_ += dt;
        if (leftPeakHoldTimer_ > holdSec) {
            leftHold *= holdDecayCoeff;
        }
    }
    leftPeakHold_.store(leftHold);

    float rightHold = rightPeakHold_.load();
    if (rightPeakCurrent >= rightHold) {
        rightHold = rightPeakCurrent;
        rightPeakHoldTimer_ = 0.0f;
    } else {
        rightPeakHoldTimer_ += dt;
        if (rightPeakHoldTimer_ > holdSec) {
            rightHold *= holdDecayCoeff;
        }
    }
    rightPeakHold_.store(rightHold);

    // Clip indicators (flash + decay)
    const float clipThreshold = 1.0f;
    const float clipDecaySec = 0.6f;
    const float clipDecayCoeff = std::exp(-dt / clipDecaySec);
    if (leftPeakCurrent >= clipThreshold) {
        leftClipIndicator_ = 1.0f;
    } else {
        leftClipIndicator_ *= clipDecayCoeff;
    }
    if (rightPeakCurrent >= clipThreshold) {
        rightClipIndicator_ = 1.0f;
    } else {
        rightClipIndicator_ *= clipDecayCoeff;
    }

    setDirty(true);
}

void AudioVisualizer::onResize(int width, int height) {
    NUIComponent::onResize(width, height);
    
    // Update display buffer size based on width
    displayBufferSize_ = static_cast<size_t>(width);
    displayBuffer_.resize(displayBufferSize_ * 2);
    currentSample_ = 0;
}

void AudioVisualizer::setAudioData(const float* leftChannel, const float* rightChannel, size_t numSamples, double sampleRate) {
    std::lock_guard<std::mutex> lock(audioDataMutex_);
    
    // Process audio data
    processAudioData(leftChannel, rightChannel, numSamples);
    
    // Update display buffer
    size_t samplesToCopy = std::min(numSamples, displayBufferSize_);
    
    for (size_t i = 0; i < samplesToCopy; ++i) {
        size_t bufferIndex = (currentSample_ + i) % displayBufferSize_;
        displayBuffer_[bufferIndex * 2] = leftChannel[i] * sensitivity_;
        if (rightChannel) {
            displayBuffer_[bufferIndex * 2 + 1] = rightChannel[i] * sensitivity_;
        } else {
            displayBuffer_[bufferIndex * 2 + 1] = leftChannel[i] * sensitivity_;
        }
    }
    
    currentSample_ = (currentSample_ + samplesToCopy) % displayBufferSize_;
}

void AudioVisualizer::setPeakLevels(float leftPeak, float rightPeak, float leftRMS, float rightRMS) {
    leftPeak = std::abs(leftPeak);
    rightPeak = std::abs(rightPeak);

    if (leftRMS < 0.0f) leftRMS = leftPeak;
    if (rightRMS < 0.0f) rightRMS = rightPeak;

    leftPeak_.store(leftPeak);
    rightPeak_.store(rightPeak);
    leftRMS_.store(leftRMS);
    rightRMS_.store(rightRMS);

    leftPeakHold_.store(std::max(leftPeakHold_.load(), leftPeak));
    rightPeakHold_.store(std::max(rightPeakHold_.load(), rightPeak));

    setDirty(true);
}

void AudioVisualizer::setInterleavedWaveform(const float* interleavedStereo, size_t numFrames) {
    if (!interleavedStereo || numFrames == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(audioDataMutex_);

    const size_t framesToCopy = std::min(numFrames, displayBufferSize_);
    float peakL = 0.0f;
    float peakR = 0.0f;
    double sumL = 0.0;
    double sumR = 0.0;

    for (size_t i = 0; i < framesToCopy; ++i) {
        const float Lraw = interleavedStereo[i * 2];
        const float Rraw = interleavedStereo[i * 2 + 1];

        const size_t bufferIndex = (currentSample_ + i) % displayBufferSize_;
        displayBuffer_[bufferIndex * 2]     = Lraw * sensitivity_;
        displayBuffer_[bufferIndex * 2 + 1] = Rraw * sensitivity_;

        const float absL = std::abs(Lraw);
        const float absR = std::abs(Rraw);
        peakL = std::max(peakL, absL);
        peakR = std::max(peakR, absR);
        sumL += static_cast<double>(absL) * absL;
        sumR += static_cast<double>(absR) * absR;
    }

    currentSample_ = (currentSample_ + framesToCopy) % displayBufferSize_;

    if (framesToCopy > 0) {
        const float rmsL = static_cast<float>(std::sqrt(sumL / framesToCopy));
        const float rmsR = static_cast<float>(std::sqrt(sumR / framesToCopy));
        leftPeak_.store(peakL);
        rightPeak_.store(peakR);
        leftRMS_.store(rmsL);
        rightRMS_.store(rmsR);
    }

    setDirty(true);
}

void AudioVisualizer::setAudioManager(Nomad::Audio::AudioDeviceManager* manager) {
    audioManager_ = manager;
}

void AudioVisualizer::setMode(AudioVisualizationMode mode) {
    mode_ = mode;
    setDirty(true);
}

void AudioVisualizer::setSensitivity(float sensitivity) {
    sensitivity_ = std::clamp(sensitivity, 0.0f, 1.0f);
}

void AudioVisualizer::setDecayRate(float decayRate) {
    decayRate_ = std::clamp(decayRate, 0.0f, 1.0f);
}

void AudioVisualizer::setColorScheme(const NUIColor& primary, const NUIColor& secondary) {
    primaryColor_ = primary;
    secondaryColor_ = secondary;
    setDirty(true);
}

void AudioVisualizer::setShowStereo(bool showStereo) {
    showStereo_ = showStereo;
    setDirty(true);
}

void AudioVisualizer::setShowPeakHold(bool showPeakHold) {
    showPeakHold_ = showPeakHold;
    setDirty(true);
}

void AudioVisualizer::setArrangementWaveform(std::shared_ptr<Nomad::Audio::WaveformCache> cache, double duration, double clipStartTime, double sampleRate) {
    arrangementWaveform_ = cache;
    projectDuration_ = duration;
    clipStartTime_ = clipStartTime;
    waveformSampleRate_ = sampleRate;
    setDirty(true);
}

void AudioVisualizer::setTransportPosition(double seconds) {
    transportPosition_.store(seconds, std::memory_order_relaxed);
    setDirty(true);
}

void AudioVisualizer::processAudioData(const float* leftChannel, const float* rightChannel, size_t numSamples) {
    // Calculate peak and RMS values from current audio buffer only
    float leftPeak = 0.0f;
    float rightPeak = 0.0f;
    float leftSum = 0.0f;
    float rightSum = 0.0f;
    
    for (size_t i = 0; i < numSamples; ++i) {
        float leftSample = std::abs(leftChannel[i]);
        leftPeak = std::max(leftPeak, leftSample);
        leftSum += leftSample * leftSample;
        
        if (rightChannel) {
            float rightSample = std::abs(rightChannel[i]);
            rightPeak = std::max(rightPeak, rightSample);
            rightSum += rightSample * rightSample;
        }
    }
    
    // Calculate RMS (Root Mean Square)
    float leftRMS = std::sqrt(leftSum / numSamples);
    float rightRMS = rightChannel ? std::sqrt(rightSum / numSamples) : leftRMS;
    
    // Store raw values (ballistics/smoothing happens in onUpdate)
    leftPeak_.store(leftPeak);
    rightPeak_.store(rightPeak);
    leftRMS_.store(leftRMS);
    rightRMS_.store(rightRMS);
    
    // Update peak hold (instant capture)
    leftPeakHold_.store(std::max(leftPeakHold_.load(), leftPeak));
    rightPeakHold_.store(std::max(rightPeakHold_.load(), rightPeak));
}

void AudioVisualizer::updatePeakMeters() {
    // This is called from the audio callback
    // Peak values are updated in processAudioData
}

void AudioVisualizer::renderWaveform(NUIRenderer& renderer) {
    NUIRect bounds = getBounds();
    float centerY = bounds.y + bounds.height / 2.0f;
    
    // Liminal Dark v2.0 background gradient - top: #121214 â†’ bottom: #18181b
    NUIColor topColor = backgroundColor_;  // #121214 - Deep charcoal
    NUIColor bottomColor = NUIColor(0.094f, 0.094f, 0.106f, 1.0f);  // #18181b - Slightly lighter
    
    // Draw gradient background
    for (int i = 0; i < static_cast<int>(bounds.height); ++i) {
        float t = static_cast<float>(i) / bounds.height;
        NUIColor gradientColor = NUIColor::lerp(topColor, bottomColor, t);
        NUIRect lineRect(bounds.x, bounds.y + i, bounds.width, 1);
        renderer.fillRect(lineRect, gradientColor);
    }
    
    // Render subtle grid lines
    renderer.drawLine(NUIPoint(bounds.x, centerY), NUIPoint(bounds.x + bounds.width, centerY), 1, gridColor_.withAlpha(0.2f));
    
    // Add horizontal grid lines for depth
    for (int i = 1; i <= 4; ++i) {
        float y = centerY + (i * bounds.height / 8.0f);
        renderer.drawLine(NUIPoint(bounds.x, y), NUIPoint(bounds.x + bounds.width, y), 1, gridColor_.withAlpha(0.1f));
        y = centerY - (i * bounds.height / 8.0f);
        renderer.drawLine(NUIPoint(bounds.x, y), NUIPoint(bounds.x + bounds.width, y), 1, gridColor_.withAlpha(0.1f));
    }
    
    // Render waveform
    std::lock_guard<std::mutex> lock(audioDataMutex_);
    
    // Liminal Dark v2.0 waveform gradient - cyan to magenta
    float t = 0.5f + 0.5f * std::sin(animationTime_ * 0.8f);
    NUIColor waveColor = NUIColor::lerp(primaryColor_, secondaryColor_, t);  // #00bcd4 â†’ #ff4081
    
    // Energy-based glow intensity (use smoothed RMS for fluid animation)
    float leftRMSSmooth = leftRMSSmoothed_.load();
    float rightRMSSmooth = rightRMSSmoothed_.load();
    float energy = (leftRMSSmooth + rightRMSSmooth) * 0.5f;
    float glowIntensity = std::clamp(energy * 1.5f, 0.2f, 1.0f);
    
    // Gentle horizontal drift animation
    float driftOffset = std::sin(animationTime_ * 0.3f) * 2.0f;
    
    if (showStereo_) {
        // Stereo offset for ribbon effect
        float stereoOffset = 5.0f;
        
        // Render left channel glow layer
        for (size_t i = 0; i < displayBufferSize_ - 1; ++i) {
            float x1 = bounds.x + (i * bounds.width) / displayBufferSize_ + driftOffset;
            float x2 = bounds.x + ((i + 1) * bounds.width) / displayBufferSize_ + driftOffset;
            float y1 = centerY - displayBuffer_[i * 2] * bounds.height / 2.0f - stereoOffset;
            float y2 = centerY - displayBuffer_[(i + 1) * 2] * bounds.height / 2.0f - stereoOffset;
            
            renderer.drawLine(NUIPoint(x1, y1), NUIPoint(x2, y2), 4, waveColor.withAlpha(0.25f * glowIntensity));
        }
        
        // Render right channel glow layer
        for (size_t i = 0; i < displayBufferSize_ - 1; ++i) {
            float x1 = bounds.x + (i * bounds.width) / displayBufferSize_ + driftOffset;
            float x2 = bounds.x + ((i + 1) * bounds.width) / displayBufferSize_ + driftOffset;
            float y1 = centerY + displayBuffer_[i * 2 + 1] * bounds.height / 2.0f + stereoOffset;
            float y2 = centerY + displayBuffer_[(i + 1) * 2 + 1] * bounds.height / 2.0f + stereoOffset;
            
            renderer.drawLine(NUIPoint(x1, y1), NUIPoint(x2, y2), 4, secondaryColor_.withAlpha(0.25f * glowIntensity));
        }
        
        // Render left channel main crisp line
        for (size_t i = 0; i < displayBufferSize_ - 1; ++i) {
            float x1 = bounds.x + (i * bounds.width) / displayBufferSize_ + driftOffset;
            float x2 = bounds.x + ((i + 1) * bounds.width) / displayBufferSize_ + driftOffset;
            float y1 = centerY - displayBuffer_[i * 2] * bounds.height / 2.0f - stereoOffset;
            float y2 = centerY - displayBuffer_[(i + 1) * 2] * bounds.height / 2.0f - stereoOffset;
            
            renderer.drawLine(NUIPoint(x1, y1), NUIPoint(x2, y2), 2, waveColor.withAlpha(glowIntensity));
        }
        
        // Render right channel main crisp line
        for (size_t i = 0; i < displayBufferSize_ - 1; ++i) {
            float x1 = bounds.x + (i * bounds.width) / displayBufferSize_ + driftOffset;
            float x2 = bounds.x + ((i + 1) * bounds.width) / displayBufferSize_ + driftOffset;
            float y1 = centerY + displayBuffer_[i * 2 + 1] * bounds.height / 2.0f + stereoOffset;
            float y2 = centerY + displayBuffer_[(i + 1) * 2 + 1] * bounds.height / 2.0f + stereoOffset;
            
            renderer.drawLine(NUIPoint(x1, y1), NUIPoint(x2, y2), 2, secondaryColor_.withAlpha(glowIntensity));
        }
        
        // Add connection band between channels when amplitudes are close
        for (size_t i = 0; i < displayBufferSize_ - 1; ++i) {
            float leftAmp = std::abs(displayBuffer_[i * 2]);
            float rightAmp = std::abs(displayBuffer_[i * 2 + 1]);
            float amplitudeDiff = std::abs(leftAmp - rightAmp);
            
            if (amplitudeDiff < 0.1f) { // Close amplitudes
                float x1 = bounds.x + (i * bounds.width) / displayBufferSize_ + driftOffset;
                float x2 = bounds.x + ((i + 1) * bounds.width) / displayBufferSize_ + driftOffset;
                float y1_left = centerY - displayBuffer_[i * 2] * bounds.height / 2.0f - stereoOffset;
                float y1_right = centerY + displayBuffer_[i * 2 + 1] * bounds.height / 2.0f + stereoOffset;
                float midY = (y1_left + y1_right) * 0.5f;
                
                NUIColor blendColor = NUIColor::lerp(waveColor, secondaryColor_, 0.5f);
                renderer.drawLine(NUIPoint(x1, midY), NUIPoint(x2, midY), 1, blendColor.withAlpha(0.3f * glowIntensity));
            }
        }
        
        // Add mirror reflection below center line
        for (size_t i = 0; i < displayBufferSize_ - 1; ++i) {
            float x1 = bounds.x + (i * bounds.width) / displayBufferSize_ + driftOffset;
            float x2 = bounds.x + ((i + 1) * bounds.width) / displayBufferSize_ + driftOffset;
            float y1 = centerY + displayBuffer_[i * 2] * bounds.height / 2.0f - stereoOffset; // Flipped
            float y2 = centerY + displayBuffer_[(i + 1) * 2] * bounds.height / 2.0f - stereoOffset; // Flipped
            
            renderer.drawLine(NUIPoint(x1, y1), NUIPoint(x2, y2), 1, waveColor.withAlpha(0.3f * glowIntensity));
        }
        
    } else {
        // Mono waveform with glow
        for (size_t i = 0; i < displayBufferSize_ - 1; ++i) {
            float x1 = bounds.x + (i * bounds.width) / displayBufferSize_ + driftOffset;
            float x2 = bounds.x + ((i + 1) * bounds.width) / displayBufferSize_ + driftOffset;
            float y1 = centerY - displayBuffer_[i * 2] * bounds.height / 2.0f;
            float y2 = centerY - displayBuffer_[(i + 1) * 2] * bounds.height / 2.0f;
            
            // Glow layer
            renderer.drawLine(NUIPoint(x1, y1), NUIPoint(x2, y2), 4, waveColor.withAlpha(0.25f * glowIntensity));
        }
        
        // Main crisp line
        for (size_t i = 0; i < displayBufferSize_ - 1; ++i) {
            float x1 = bounds.x + (i * bounds.width) / displayBufferSize_ + driftOffset;
            float x2 = bounds.x + ((i + 1) * bounds.width) / displayBufferSize_ + driftOffset;
            float y1 = centerY - displayBuffer_[i * 2] * bounds.height / 2.0f;
            float y2 = centerY - displayBuffer_[(i + 1) * 2] * bounds.height / 2.0f;
            
            renderer.drawLine(NUIPoint(x1, y1), NUIPoint(x2, y2), 2, waveColor.withAlpha(glowIntensity));
        }
    }
    
    // Add time markers (every second)
    float timeMarkerInterval = bounds.width / 10.0f; // 10 markers across width
    for (int i = 0; i <= 10; ++i) {
        float x = bounds.x + (i * timeMarkerInterval);
        renderer.drawLine(NUIPoint(x, bounds.y + bounds.height - 10), 
                         NUIPoint(x, bounds.y + bounds.height - 5), 
                         1, textColor_.withAlpha(0.4f));
    }
    
    // Audio active pulse indicator - Liminal Dark v2.0
    float pulse = 0.5f + 0.5f * std::sin(animationTime_ * 3.5f);
    NUIColor pulseColor = NUIColor(0.620f, 1.0f, 0.380f, pulse * glowIntensity);  // #9eff61 - Accent lime
    float pulseRadius = 5.0f + (glowIntensity * 2.0f);
    renderer.fillCircle(NUIPoint(bounds.x + bounds.width - 15, bounds.y + bounds.height - 15), 
                       pulseRadius, pulseColor);
    
    // Add subtle vignette effect
    NUIColor vignetteColor = NUIColor(0.0f, 0.0f, 0.0f, 0.1f);
    for (int i = 0; i < 20; ++i) {
        float alpha = (static_cast<float>(i) / 20.0f) * 0.1f;
        NUIRect vignetteRect(bounds.x + i, bounds.y + i, bounds.width - (i * 2), bounds.height - (i * 2));
        renderer.strokeRoundedRect(vignetteRect, 8, 1, vignetteColor.withAlpha(alpha));
    }
}

void AudioVisualizer::renderSpectrum(NUIRenderer& renderer) {
    NUIRect bounds = getBounds();
    
    // Simple spectrum visualization (would need FFT in real implementation)
    // For now, just show a placeholder
    renderer.drawText("Spectrum Mode", NUIPoint(bounds.x + 10, bounds.y + 10), 14, textColor_);
    renderer.drawText("(FFT implementation needed)", NUIPoint(bounds.x + 10, bounds.y + 30), 12, textColor_.withAlpha(0.7f));
}

void AudioVisualizer::renderLevelMeter(NUIRenderer& renderer) {
    NUIRect bounds = getBounds();
    
    float meterWidth = bounds.width / 2.0f - 20;
    float meterHeight = bounds.height - 40;
    
    // Left channel meter
    NUIRect leftMeter(bounds.x + 10, bounds.y + 20, meterWidth, meterHeight);
    renderLevelBar(renderer, leftMeter, leftRMS_, leftPeakHold_, primaryColor_);
    
    // Right channel meter
    NUIRect rightMeter(bounds.x + bounds.width - meterWidth - 10, bounds.y + 20, meterWidth, meterHeight);
    renderLevelBar(renderer, rightMeter, rightRMS_, rightPeakHold_, secondaryColor_);
    
    // Labels
    renderer.drawText("L", NUIPoint(leftMeter.x + leftMeter.width / 2 - 4, bounds.y + 10), 12, textColor_);
    renderer.drawText("R", NUIPoint(rightMeter.x + rightMeter.width / 2 - 4, bounds.y + 10), 12, textColor_);
}

void AudioVisualizer::renderVU(NUIRenderer& renderer) {
    NUIRect bounds = getBounds();
    
    // VU meter with color zones
    float meterWidth = bounds.width - 20;
    float meterHeight = bounds.height - 40;
    NUIRect meter(bounds.x + 10, bounds.y + 20, meterWidth, meterHeight);
    
    // Enhanced background with gradient
    NUIColor topBg = backgroundColor_.darkened(0.25f);
    NUIColor bottomBg = backgroundColor_.darkened(0.15f);
    for (int i = 0; i < static_cast<int>(meter.height); ++i) {
        float t = static_cast<float>(i) / meter.height;
        NUIColor gradientColor = NUIColor::lerp(topBg, bottomBg, t);
        NUIRect lineRect(meter.x, meter.y + i, meter.width, 1);
        renderer.fillRect(lineRect, gradientColor);
    }
    
    // Add subtle inner glow
    renderer.strokeRoundedRect(meter, 4, 1, NUIColor::white().withAlpha(0.1f));
    
    // Color zones with enhanced styling
    float greenZone = meterHeight * 0.6f;
    float yellowZone = meterHeight * 0.8f;
    
    // Green zone (0-60%) with glow
    NUIRect greenRect(meter.x, meter.y + meterHeight - greenZone, meter.width, greenZone);
    renderer.fillRoundedRect(greenRect, 4, NUIColor(0.0f, 1.0f, 0.0f, 0.4f));
    renderer.strokeRoundedRect(greenRect, 4, 1, NUIColor(0.0f, 1.0f, 0.0f, 0.6f));
    
    // Yellow zone (60-80%) with glow
    NUIRect yellowRect(meter.x, meter.y + meterHeight - yellowZone, meter.width, yellowZone - greenZone);
    renderer.fillRoundedRect(yellowRect, 4, NUIColor(1.0f, 1.0f, 0.0f, 0.4f));
    renderer.strokeRoundedRect(yellowRect, 4, 1, NUIColor(1.0f, 1.0f, 0.0f, 0.6f));
    
    // Red zone (80-100%) with glow
    NUIRect redRect(meter.x, meter.y + meterHeight - meterHeight, meter.width, meterHeight - yellowZone);
    renderer.fillRoundedRect(redRect, 4, NUIColor(1.0f, 0.0f, 0.0f, 0.4f));
    renderer.strokeRoundedRect(redRect, 4, 1, NUIColor(1.0f, 0.0f, 0.0f, 0.6f));
    
    // Level indicator with energy-based glow
    float level = std::max(leftRMS_, rightRMS_);
    float levelHeight = level * meterHeight;
    float glowIntensity = std::clamp(level * 2.0f, 0.3f, 1.0f);
    
    NUIColor levelColor = NUIColor(0.0f, 1.0f, 0.0f); // Green
    if (level > 0.8f) levelColor = NUIColor(1.0f, 0.0f, 0.0f); // Red
    else if (level > 0.6f) levelColor = NUIColor(1.0f, 1.0f, 0.0f); // Yellow
    
    // Glow layer
    NUIRect glowRect(meter.x, meter.y + meterHeight - levelHeight - 2, meter.width, 8);
    renderer.fillRoundedRect(glowRect, 2, levelColor.withAlpha(0.3f * glowIntensity));
    
    // Main level indicator
    NUIRect levelRect(meter.x, meter.y + meterHeight - levelHeight, meter.width, 4);
    renderer.fillRoundedRect(levelRect, 2, levelColor.withAlpha(glowIntensity));
    
    // Peak hold indicator with pulsing effect
    if (showPeakHold_) {
        float peakHeight = std::max(leftPeakHold_, rightPeakHold_) * meterHeight;
        float pulse = 0.7f + 0.3f * std::sin(animationTime_ * 6.0f);
        NUIRect peakRect(meter.x, meter.y + meterHeight - peakHeight, meter.width, 2);
        renderer.fillRoundedRect(peakRect, 1, NUIColor::white().withAlpha(pulse));
    }
    
    // Add scale markers
    for (int i = 0; i <= 10; ++i) {
        float y = meter.y + meterHeight - (i * meterHeight / 10.0f);
        float value = i * 10.0f;
        std::string label = std::to_string(static_cast<int>(value)) + "%";
        
        auto textSize = renderer.measureText(label, 10);
        renderer.drawText(label, NUIPoint(meter.x - textSize.width - 5, y - 5), 10, textColor_.withAlpha(0.7f));
        
        // Scale line
        renderer.drawLine(NUIPoint(meter.x - 3, y), NUIPoint(meter.x, y), 1, textColor_.withAlpha(0.4f));
    }
}

void AudioVisualizer::renderOscilloscope(NUIRenderer& renderer) {
    NUIRect bounds = getBounds();
    float centerY = bounds.y + bounds.height / 2.0f;
    
    // Enhanced background with subtle gradient
    NUIColor topBg = backgroundColor_.darkened(0.2f);
    NUIColor bottomBg = backgroundColor_.lightened(0.05f);
    for (int i = 0; i < static_cast<int>(bounds.height); ++i) {
        float t = static_cast<float>(i) / bounds.height;
        NUIColor gradientColor = NUIColor::lerp(topBg, bottomBg, t);
        NUIRect lineRect(bounds.x, bounds.y + i, bounds.width, 1);
        renderer.fillRect(lineRect, gradientColor);
    }
    
    // Render enhanced grid with glow
    for (int i = 0; i <= 10; ++i) {
        float x = bounds.x + (i * bounds.width) / 10.0f;
        float alpha = (i % 2 == 0) ? 0.3f : 0.15f; // Alternating intensity
        renderer.drawLine(NUIPoint(x, bounds.y), NUIPoint(x, bounds.y + bounds.height), 1, gridColor_.withAlpha(alpha));
    }
    
    for (int i = 0; i <= 8; ++i) {
        float y = bounds.y + (i * bounds.height) / 8.0f;
        float alpha = (i % 2 == 0) ? 0.3f : 0.15f; // Alternating intensity
        renderer.drawLine(NUIPoint(bounds.x, y), NUIPoint(bounds.x + bounds.width, y), 1, gridColor_.withAlpha(alpha));
    }
    
    // Render center lines with enhanced visibility
    renderer.drawLine(NUIPoint(bounds.x, centerY), NUIPoint(bounds.x + bounds.width, centerY), 2, gridColor_.withAlpha(0.6f));
    renderer.drawLine(NUIPoint(bounds.x + bounds.width / 2, bounds.y), NUIPoint(bounds.x + bounds.width / 2, bounds.y + bounds.height), 2, gridColor_.withAlpha(0.6f));
    
    // Add corner markers for scope feel
    float markerSize = 8.0f;
    // Top-left
    renderer.drawLine(NUIPoint(bounds.x, bounds.y), NUIPoint(bounds.x + markerSize, bounds.y), 2, gridColor_.withAlpha(0.8f));
    renderer.drawLine(NUIPoint(bounds.x, bounds.y), NUIPoint(bounds.x, bounds.y + markerSize), 2, gridColor_.withAlpha(0.8f));
    // Top-right
    renderer.drawLine(NUIPoint(bounds.x + bounds.width - markerSize, bounds.y), NUIPoint(bounds.x + bounds.width, bounds.y), 2, gridColor_.withAlpha(0.8f));
    renderer.drawLine(NUIPoint(bounds.x + bounds.width, bounds.y), NUIPoint(bounds.x + bounds.width, bounds.y + markerSize), 2, gridColor_.withAlpha(0.8f));
    // Bottom-left
    renderer.drawLine(NUIPoint(bounds.x, bounds.y + bounds.height - markerSize), NUIPoint(bounds.x, bounds.y + bounds.height), 2, gridColor_.withAlpha(0.8f));
    renderer.drawLine(NUIPoint(bounds.x, bounds.y + bounds.height), NUIPoint(bounds.x + markerSize, bounds.y + bounds.height), 2, gridColor_.withAlpha(0.8f));
    // Bottom-right
    renderer.drawLine(NUIPoint(bounds.x + bounds.width - markerSize, bounds.y + bounds.height), NUIPoint(bounds.x + bounds.width, bounds.y + bounds.height), 2, gridColor_.withAlpha(0.8f));
    renderer.drawLine(NUIPoint(bounds.x + bounds.width, bounds.y + bounds.height - markerSize), NUIPoint(bounds.x + bounds.width, bounds.y + bounds.height), 2, gridColor_.withAlpha(0.8f));
    
    // Render waveform with enhanced oscilloscope styling
    std::lock_guard<std::mutex> lock(audioDataMutex_);
    
    // Dynamic color blend
    float t = 0.5f + 0.5f * std::sin(animationTime_ * 1.2f);
    NUIColor waveColor = NUIColor::lerp(primaryColor_, secondaryColor_, t);
    
    // Energy-based glow (use smoothed RMS for fluid animation)
    float leftRMSSmooth = leftRMSSmoothed_.load();
    float rightRMSSmooth = rightRMSSmoothed_.load();
    float energy = (leftRMSSmooth + rightRMSSmooth) * 0.5f;
    float glowIntensity = std::clamp(energy * 1.8f, 0.3f, 1.0f);
    
    // Render waveform with oscilloscope-style persistence
    for (size_t i = 0; i < displayBufferSize_ - 1; ++i) {
        float x1 = bounds.x + (i * bounds.width) / displayBufferSize_;
        float x2 = bounds.x + ((i + 1) * bounds.width) / displayBufferSize_;
        float y1 = centerY - displayBuffer_[i * 2] * bounds.height / 2.0f;
        float y2 = centerY - displayBuffer_[(i + 1) * 2] * bounds.height / 2.0f;
        
        // Glow trail
        renderer.drawLine(NUIPoint(x1, y1), NUIPoint(x2, y2), 3, waveColor.withAlpha(0.2f * glowIntensity));
        // Main trace
        renderer.drawLine(NUIPoint(x1, y1), NUIPoint(x2, y2), 1, waveColor.withAlpha(glowIntensity));
    }
    
    // Add trigger level indicator
    float triggerLevel = 0.3f; // Fixed trigger level for demo
    float triggerY = centerY - triggerLevel * bounds.height / 2.0f;
    renderer.drawLine(NUIPoint(bounds.x, triggerY), NUIPoint(bounds.x + bounds.width, triggerY), 1, NUIColor(1.0f, 1.0f, 0.0f, 0.6f));
    
    // Add timebase indicator
    std::string timebaseText = "1ms/div";
    auto textSize = renderer.measureText(timebaseText, 10);
    renderer.drawText(timebaseText, NUIPoint(bounds.x + bounds.width - textSize.width - 5, bounds.y + 5), 10, textColor_.withAlpha(0.7f));
}

void AudioVisualizer::renderCompactMeter(NUIRenderer& renderer) {
    NUIRect bounds = getBounds();
    
    // Very slim vertical meters - side by side
    const float padding = 2.0f;
    const float gap = 2.0f;
    float meterHeight = bounds.height - padding * 2.0f;
    float meterWidth = (bounds.width - padding * 2.0f - gap) / 2.0f;
    
    // Use smoothed RMS for the body and smoothed peak for the fast overlay line.
    // This matches the mixer: energy body + responsive peak marker.
    float leftRMSSmooth = leftRMSSmoothed_.load();
    float rightRMSSmooth = rightRMSSmoothed_.load();
    float leftPeakOverlay = leftPeakSmoothed_.load();
    float rightPeakOverlay = rightPeakSmoothed_.load();
    
    // Left channel meter (very slim) - CYAN
    NUIRect leftMeter(bounds.x + padding, bounds.y + padding, meterWidth, meterHeight);
    renderLevelBar(renderer, leftMeter, leftRMSSmooth, leftPeakOverlay, primaryColor_);
    
    // Right channel meter (very slim) - MAGENTA
    NUIRect rightMeter(bounds.x + padding + meterWidth + gap, bounds.y + padding, meterWidth, meterHeight);
    renderLevelBar(renderer, rightMeter, rightRMSSmooth, rightPeakOverlay, secondaryColor_);

    // Clip flash at the top of each meter
    if (leftClipIndicator_ > 0.02f) {
        NUIRect clipRect(leftMeter.x, leftMeter.y - 1.0f, leftMeter.width, 3.0f);
        renderer.fillRoundedRect(clipRect, 1.0f, NUIColor(1.0f, 0.15f, 0.15f, leftClipIndicator_));
    }
    if (rightClipIndicator_ > 0.02f) {
        NUIRect clipRect(rightMeter.x, rightMeter.y - 1.0f, rightMeter.width, 3.0f);
        renderer.fillRoundedRect(clipRect, 1.0f, NUIColor(1.0f, 0.15f, 0.15f, rightClipIndicator_));
    }
}

void AudioVisualizer::renderCompactWaveform(NUIRenderer& renderer) {
    NUIRect bounds = getBounds();
    if (bounds.isEmpty()) return;

    const float centerY = bounds.y + bounds.height * 0.5f;
    renderer.drawLine(NUIPoint(bounds.x, centerY),
                      NUIPoint(bounds.x + bounds.width, centerY),
                      1.0f, gridColor_.withAlpha(0.25f));

    std::lock_guard<std::mutex> lock(audioDataMutex_);
    if (displayBufferSize_ < 2) return;

    std::vector<NUIPoint> points;
    points.reserve(displayBufferSize_);

    // Auto-gain so the mini scope "goes bonkers" with loud content.
    float maxAbs = 0.0001f;
    for (size_t i = 0; i < displayBufferSize_; ++i) {
        const size_t idx = (currentSample_ + i) % displayBufferSize_;
        const float s = (displayBuffer_[idx * 2] + displayBuffer_[idx * 2 + 1]) * 0.5f;
        maxAbs = std::max(maxAbs, std::abs(s));
    }
    const float autoGain = std::clamp(0.9f / maxAbs, 1.0f, 8.0f);

    const float halfH = bounds.height * 0.45f;
    for (size_t i = 0; i < displayBufferSize_; ++i) {
        const size_t idx = (currentSample_ + i) % displayBufferSize_;
        const float s = (displayBuffer_[idx * 2] + displayBuffer_[idx * 2 + 1]) * 0.5f * autoGain;
        const float x = bounds.x + (static_cast<float>(i) * bounds.width) /
                                      static_cast<float>(displayBufferSize_ - 1);
        const float y = centerY - s * halfH;
        points.emplace_back(x, y);
    }

    const float t = 0.5f + 0.5f * std::sin(animationTime_ * 1.4f);
    NUIColor waveColor = NUIColor::lerp(primaryColor_, secondaryColor_, t).withAlpha(0.9f);

    const float energy = (leftRMSSmoothed_.load() + rightRMSSmoothed_.load()) * 0.5f;
    const float glow = std::clamp(energy * 2.5f, 0.0f, 1.0f);
    if (glow > 0.05f) {
        const float radius = 6.0f;
        NUIRect inner(bounds.x + radius,
                      bounds.y + radius,
                      bounds.width - radius * 2.0f,
                      bounds.height - radius * 2.0f);
        if (inner.width > 1.0f && inner.height > 1.0f) {
            renderer.drawGlow(inner, radius, glow, waveColor);
        }
    }

    renderer.drawPolyline(points.data(), static_cast<int>(points.size()), 1.5f, waveColor);
}

void AudioVisualizer::renderArrangementWaveform(NUIRenderer& renderer) {
    NUIRect bounds = getBounds();
    if (bounds.isEmpty()) return;
    
    const float centerY = bounds.y + bounds.height * 0.5f;
    
    // Draw subtle center line
    renderer.drawLine(NUIPoint(bounds.x, centerY),
                      NUIPoint(bounds.x + bounds.width, centerY),
                      1.0f, gridColor_.withAlpha(0.15f));
    
    // If no arrangement waveform, show placeholder animation
    if (!arrangementWaveform_ || !arrangementWaveform_->isReady() || projectDuration_ <= 0.0) {
        float t = 0.5f + 0.5f * std::sin(animationTime_ * 1.2f);
        NUIColor lineColor = NUIColor::lerp(primaryColor_, secondaryColor_, t).withAlpha(0.3f);
        renderer.drawLine(NUIPoint(bounds.x, centerY), 
                         NUIPoint(bounds.x + bounds.width, centerY), 
                         2.0f, lineColor);
        return;
    }
    
    // === SCROLLING WAVEFORM ===
    // Shows the waveform scrolling left as playback advances.
    
    double position = transportPosition_.load(std::memory_order_relaxed);
    
    // Convert timeline position to clip-relative position
    // The waveform cache is indexed from 0, but the clip may start at a later timeline position
    double clipRelativePos = position - clipStartTime_;
    
    // Ultra-short window = super tight, immediate feel
    double windowDuration = 0.5; // 0.5 seconds for tight scrolling
    
    // === Show PAST audio (clip-relative) ===
    // Window shows what JUST PLAYED, with current position at the RIGHT edge.
    double windowEnd = clipRelativePos;
    double windowStart = clipRelativePos - windowDuration;
    
    // Clamp to valid clip range (0 to clip duration)
    if (windowEnd < 0.0) return; // Haven't reached clip yet
    windowStart = std::max(0.0, windowStart);
    windowEnd = std::max(windowStart + 0.01, windowEnd);
    
    // Convert to samples using the actual waveform sample rate
    Nomad::Audio::SampleIndex startSample = static_cast<Nomad::Audio::SampleIndex>(windowStart * waveformSampleRate_);
    Nomad::Audio::SampleIndex endSample = static_cast<Nomad::Audio::SampleIndex>(windowEnd * waveformSampleRate_);
    
    if (endSample <= startSample) return;
    
    // Get peaks for the visible range
    uint32_t numPixels = static_cast<uint32_t>(bounds.width);
    std::vector<Nomad::Audio::WaveformPeak> peaks;
    arrangementWaveform_->getPeaksForRange(0, startSample, endSample, numPixels, peaks);
    
    if (peaks.empty()) return;
    
    // Animated color blend (cyan/magenta gradient)
    float t = 0.5f + 0.5f * std::sin(animationTime_ * 1.4f);
    NUIColor waveColor = NUIColor::lerp(primaryColor_, secondaryColor_, t);
    
    // Energy-based glow from current audio output
    float energy = (leftRMSSmoothed_.load() + rightRMSSmoothed_.load()) * 0.5f;
    float glowIntensity = std::clamp(energy * 2.5f, 0.5f, 1.0f);
    
    const float halfH = bounds.height * 0.42f;
    
    // Render waveform bars - rightmost bars (newest audio) are brighter
    for (size_t i = 0; i < peaks.size(); ++i) {
        float x = bounds.x + static_cast<float>(i);
        float minY = centerY - peaks[i].min * halfH;
        float maxY = centerY - peaks[i].max * halfH;
        
        // Ensure min < max for drawing
        if (minY > maxY) std::swap(minY, maxY);
        
        // Position-based intensity: right side (current) is brighter
        float normalizedPos = static_cast<float>(i) / static_cast<float>(peaks.size());
        float positionGlow = 0.4f + 0.6f * normalizedPos; // 40% to 100% based on position
        float alpha = glowIntensity * positionGlow;
        
        // Glow layer for rightmost bars (current audio)
        if (normalizedPos > 0.8f) {
            float glowStrength = (normalizedPos - 0.8f) / 0.2f; // 0 to 1 in last 20%
            renderer.drawLine(NUIPoint(x, minY), NUIPoint(x, maxY), 
                             3.0f, waveColor.withAlpha(0.3f * glowStrength * glowIntensity));
        }
        
        // Main waveform bar
        renderer.drawLine(NUIPoint(x, minY), NUIPoint(x, maxY), 
                         1.5f, waveColor.withAlpha(alpha));
    }
}

void AudioVisualizer::renderLevelBar(NUIRenderer& renderer, const NUIRect& bounds, float level, float peak, const NUIColor& color) {
    // Background
    renderer.fillRoundedRect(bounds, 3.0f, backgroundColor_.darkened(0.25f));

    auto linToNorm = [](float lin) -> float {
        const float eps = 1e-6f;
        const float dbMin = -60.0f;
        float v = std::max(lin, eps);
        float db = 20.0f * std::log10(v);
        db = std::clamp(db, dbMin, 0.0f);
        return (db - dbMin) / (-dbMin);
    };

    const float normLevel = linToNorm(level);
    const float levelHeight = normLevel * bounds.height;
    if (levelHeight > 0.5f) {
        const float bottomY = bounds.y + bounds.height;
        const float topY = bottomY - levelHeight;

        const float dbMin = -60.0f;
        const float warnDb = -12.0f;
        const float clipDb = -3.0f;
        const float warnNorm = (warnDb - dbMin) / (-dbMin);
        const float clipNorm = (clipDb - dbMin) / (-dbMin);
        const float warnY = bottomY - warnNorm * bounds.height;
        const float clipY = bottomY - clipNorm * bounds.height;

        // Safe zone colors (channel-tinted)
        const NUIColor safeBottom = color.darkened(0.25f);
        const NUIColor safeTop = color;

        // Warning zone shifts toward yellow/orange
        const NUIColor warnBase = NUIColor::lerp(color, NUIColor(1.0f, 0.85f, 0.25f), 0.8f);
        const NUIColor warnBottom = warnBase.darkened(0.1f);
        const NUIColor warnTop = warnBase.lightened(0.1f);

        // Clip zone is red-hot
        const NUIColor clipBottom = NUIColor(1.0f, 0.3f, 0.2f);
        const NUIColor clipTop = NUIColor(1.0f, 0.05f, 0.05f);

        // Safe segment
        float safeSegTop = std::max(topY, warnY);
        if (safeSegTop < bottomY) {
            NUIRect safeRect(bounds.x, safeSegTop, bounds.width, bottomY - safeSegTop);
            renderer.fillRectGradient(safeRect, safeTop, safeBottom, true);
        }

        // Warn segment
        if (topY < warnY) {
            float warnSegBottom = warnY;
            float warnSegTop = std::max(topY, clipY);
            if (warnSegTop < warnSegBottom) {
                NUIRect warnRect(bounds.x, warnSegTop, bounds.width, warnSegBottom - warnSegTop);
                renderer.fillRectGradient(warnRect, warnTop, warnBottom, true);
            }
        }

        // Clip segment
        if (topY < clipY) {
            NUIRect clipRect(bounds.x, topY, bounds.width, clipY - topY);
            renderer.fillRectGradient(clipRect, clipTop, clipBottom, true);
        }
    }

    // Peak hold indicator (thin white line)
    if (showPeakHold_ && peak > 0.0001f) {
        const float normPeak = linToNorm(peak);
        const float peakY = bounds.y + bounds.height - normPeak * bounds.height;
        renderer.drawLine(NUIPoint(bounds.x, peakY), NUIPoint(bounds.x + bounds.width, peakY),
                          1.5f, NUIColor::white().withAlpha(0.9f));
    }

    // Subtle edge highlight
    renderer.strokeRoundedRect(bounds, 3.0f, 1.0f, gridColor_.withAlpha(0.35f));
}

void AudioVisualizer::renderSpectrumBar(NUIRenderer& renderer, const NUIRect& bounds, float magnitude, const NUIColor& color) {
    float barHeight = magnitude * bounds.height;
    NUIRect barRect(bounds.x, bounds.y + bounds.height - barHeight, bounds.width, barHeight);
    renderer.fillRoundedRect(barRect, 2, color);
}

} // namespace NomadUI
