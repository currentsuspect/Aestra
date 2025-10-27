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
    
    // Render background
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
    }
}

void AudioVisualizer::onUpdate(double deltaTime) {
    animationTime_ += static_cast<float>(deltaTime);
    
    // Get current raw values from audio thread
    float leftPeakCurrent = leftPeak_.load();
    float rightPeakCurrent = rightPeak_.load();
    float leftRMSCurrent = leftRMS_.load();
    float rightRMSCurrent = rightRMS_.load();
    
    // Get previous smoothed values
    float leftPeakPrev = leftPeakSmoothed_.load();
    float rightPeakPrev = rightPeakSmoothed_.load();
    float leftRMSPrev = leftRMSSmoothed_.load();
    float rightRMSPrev = rightRMSSmoothed_.load();
    
    // Ballistics: Instant attack, very fast decay
    // If new value is higher: instant attack (use new value)
    // If new value is lower: very fast decay (blend with smoothing)
    float attackFactor = 1.0f;      // Instant attack
    float releaseFactor = 0.5f;     // Very fast release (50% retention, 50% decay)
    
    // Peak ballistics
    float leftPeakNew = (leftPeakCurrent > leftPeakPrev) 
        ? leftPeakCurrent  // Instant attack
        : (releaseFactor * leftPeakPrev + (1.0f - releaseFactor) * leftPeakCurrent);  // Smooth decay
    
    float rightPeakNew = (rightPeakCurrent > rightPeakPrev)
        ? rightPeakCurrent  // Instant attack
        : (releaseFactor * rightPeakPrev + (1.0f - releaseFactor) * rightPeakCurrent);  // Smooth decay
    
    // RMS ballistics (smoother than peaks)
    float leftRMSNew = (leftRMSCurrent > leftRMSPrev)
        ? (0.7f * leftRMSPrev + 0.3f * leftRMSCurrent)  // Fast attack (30% new)
        : (releaseFactor * leftRMSPrev + (1.0f - releaseFactor) * leftRMSCurrent);  // Smooth decay
    
    float rightRMSNew = (rightRMSCurrent > rightRMSPrev)
        ? (0.7f * rightRMSPrev + 0.3f * rightRMSCurrent)  // Fast attack (30% new)
        : (releaseFactor * rightRMSPrev + (1.0f - releaseFactor) * rightRMSCurrent);  // Smooth decay
    
    // Update smoothed values
    leftPeakSmoothed_.store(leftPeakNew);
    rightPeakSmoothed_.store(rightPeakNew);
    leftRMSSmoothed_.store(leftRMSNew);
    rightRMSSmoothed_.store(rightRMSNew);
    
    // Update peak hold: rise instantly to match RMS, decay very fast when RMS drops
    float peakHoldDecay = 0.90f; // 90% retention per frame (very fast decay - almost instant)
    float leftPeakHoldCurrent = leftPeakHold_.load();
    float rightPeakHoldCurrent = rightPeakHold_.load();
    
    // If RMS is higher than peak hold: instant rise
    // If RMS is lower: apply decay
    if (leftRMSNew > leftPeakHoldCurrent) {
        leftPeakHold_.store(leftRMSNew);
    } else {
        leftPeakHold_.store(leftPeakHoldCurrent * peakHoldDecay);
    }
    
    if (rightRMSNew > rightPeakHoldCurrent) {
        rightPeakHold_.store(rightRMSNew);
    } else {
        rightPeakHold_.store(rightPeakHoldCurrent * peakHoldDecay);
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
    
    // Liminal Dark v2.0 background gradient - top: #121214 → bottom: #18181b
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
    NUIColor waveColor = NUIColor::lerp(primaryColor_, secondaryColor_, t);  // #00bcd4 → #ff4081
    
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
    float meterHeight = bounds.height - 8;
    float meterWidth = (bounds.width - 8) / 2; // Two very slim meters side by side
    
    // Use smoothed values for fluid animation
    float leftRMSSmooth = leftRMSSmoothed_.load();
    float rightRMSSmooth = rightRMSSmoothed_.load();
    float leftPeakHold = leftPeakHold_.load();   // Use peak hold, not smoothed peak
    float rightPeakHold = rightPeakHold_.load(); // Use peak hold, not smoothed peak
    
    // Left channel meter (very slim) - CYAN
    NUIRect leftMeter(bounds.x + 2, bounds.y + 4, meterWidth, meterHeight);
    renderLevelBar(renderer, leftMeter, leftRMSSmooth, leftPeakHold, primaryColor_);
    
    // Right channel meter (very slim) - MAGENTA
    NUIRect rightMeter(bounds.x + 4 + meterWidth, bounds.y + 4, meterWidth, meterHeight);
    renderLevelBar(renderer, rightMeter, rightRMSSmooth, rightPeakHold, secondaryColor_);
    
    // Add small channel labels
    renderer.drawText("L", NUIPoint(leftMeter.x + leftMeter.width / 2 - 3, bounds.y + 1), 8, textColor_.withAlpha(0.8f));
    renderer.drawText("R", NUIPoint(rightMeter.x + rightMeter.width / 2 - 3, bounds.y + 1), 8, textColor_.withAlpha(0.8f));
    
    // Add peak indicators with proper scaling
    if (showPeakHold_) {
        float scaledLeftPeak = std::min(1.0f, leftPeakHold_ * 2.0f); // Scale 0.5f -> 1.0f
        float scaledRightPeak = std::min(1.0f, rightPeakHold_ * 2.0f); // Scale 0.5f -> 1.0f
        float leftPeakHeight = scaledLeftPeak * leftMeter.height;
        float rightPeakHeight = scaledRightPeak * rightMeter.height;
        
        // Left peak
        renderer.drawLine(
            NUIPoint(leftMeter.x, leftMeter.y + leftMeter.height - leftPeakHeight),
            NUIPoint(leftMeter.x + leftMeter.width, leftMeter.y + leftMeter.height - leftPeakHeight),
            2, primaryColor_.withAlpha(0.8f)
        );
        
        // Right peak
        renderer.drawLine(
            NUIPoint(rightMeter.x, rightMeter.y + rightMeter.height - rightPeakHeight),
            NUIPoint(rightMeter.x + rightMeter.width, rightMeter.y + rightMeter.height - rightPeakHeight),
            2, secondaryColor_.withAlpha(0.8f)
        );
    }
}

void AudioVisualizer::renderLevelBar(NUIRenderer& renderer, const NUIRect& bounds, float level, float peak, const NUIColor& color) {
    // Background
    renderer.fillRoundedRect(bounds, 4, backgroundColor_.darkened(0.2f));
    
    // Aggressive scaling for better visibility
    // Most music peaks around 0.1-0.3 RMS, so we scale it to fill the meter
    float scaledLevel = std::min(1.0f, level * 5.0f); // Increased scaling from 3.0 to 5.0
    float levelHeight = scaledLevel * bounds.height;
    
    // Draw the level bar (colored - cyan/magenta)
    if (levelHeight > 1.0f) { // Only draw if visible
        NUIRect levelRect(bounds.x, bounds.y + bounds.height - levelHeight, bounds.width, levelHeight);
        renderer.fillRoundedRect(levelRect, 4, color);
    }
    
    // Peak hold indicator (white line at the maximum)
    if (showPeakHold_ && peak > 0.001f) {
        float scaledPeak = std::min(1.0f, peak * 5.0f); // Same scaling as level
        float peakHeight = scaledPeak * bounds.height;
        NUIRect peakRect(bounds.x, bounds.y + bounds.height - peakHeight, bounds.width, 2);
        renderer.fillRoundedRect(peakRect, 1, NUIColor::white());
    }
    
    // No scale markers - clean look
}

void AudioVisualizer::renderSpectrumBar(NUIRenderer& renderer, const NUIRect& bounds, float magnitude, const NUIColor& color) {
    float barHeight = magnitude * bounds.height;
    NUIRect barRect(bounds.x, bounds.y + bounds.height - barHeight, bounds.width, barHeight);
    renderer.fillRoundedRect(barRect, 2, color);
}

} // namespace NomadUI
