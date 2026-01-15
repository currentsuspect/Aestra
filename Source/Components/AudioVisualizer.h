// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadAudio/include/NomadAudio.h"
#include "../NomadAudio/include/WaveformCache.h"
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>

namespace NomadUI {

/**
 * Audio visualization modes
 */
enum class AudioVisualizationMode {
    Waveform,
    Spectrum,
    LevelMeter,
    VU,
    Oscilloscope,
    CompactMeter,  // New compact mode for FL-style positioning
    CompactWaveform,
    ArrangementWaveform  // Scrolling pre-rendered project waveform
};

/**
 * Audio Visualizer Component
 * 
 * Real-time audio visualization with multiple display modes.
 * Integrates with NomadAudio for live audio data.
 */
class AudioVisualizer : public NUIComponent {
public:
    AudioVisualizer();
    ~AudioVisualizer() override = default;
    
    // Component interface
    void onRender(NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;
    void onResize(int width, int height) override;
    
    // Audio data input
    void setAudioData(const float* leftChannel, const float* rightChannel, size_t numSamples, double sampleRate);
    // Lightweight metering path (safe to call from main thread).
    void setPeakLevels(float leftPeak, float rightPeak, float leftRMS = -1.0f, float rightRMS = -1.0f);
    // Interleaved stereo waveform path (safe to call from main thread).
    void setInterleavedWaveform(const float* interleavedStereo, size_t numFrames);
    void setAudioManager(Nomad::Audio::AudioDeviceManager* manager);
    
    // Visualization settings
    void setMode(AudioVisualizationMode mode);
    void setSensitivity(float sensitivity); // 0.0 to 1.0
    void setDecayRate(float decayRate); // 0.0 to 1.0
    void setColorScheme(const NUIColor& primary, const NUIColor& secondary);
    void setShowStereo(bool showStereo);
    void setShowPeakHold(bool showPeakHold);
    
    // Arrangement waveform (scrolling transport display)
    void setArrangementWaveform(std::shared_ptr<Nomad::Audio::WaveformCache> cache, double duration, double clipStartTime = 0.0, double sampleRate = 48000.0);
    void setTransportPosition(double seconds);
    
    // Properties
    AudioVisualizationMode getMode() const { return mode_; }
    float getSensitivity() const { return sensitivity_; }
    float getDecayRate() const { return decayRate_; }
    bool getShowStereo() const { return showStereo_; }
    bool getShowPeakHold() const { return showPeakHold_; }
    
    // Peak detection
    float getLeftPeak() const { return leftPeak_; }
    float getRightPeak() const { return rightPeak_; }
    float getLeftRMS() const { return leftRMS_; }
    float getRightRMS() const { return rightRMS_; }
    
private:
    void processAudioData(const float* leftChannel, const float* rightChannel, size_t numSamples);
    void updatePeakMeters();
    void renderWaveform(NUIRenderer& renderer);
    void renderSpectrum(NUIRenderer& renderer);
    void renderLevelMeter(NUIRenderer& renderer);
    void renderVU(NUIRenderer& renderer);
    void renderOscilloscope(NUIRenderer& renderer);
    void renderCompactMeter(NUIRenderer& renderer);
    void renderCompactWaveform(NUIRenderer& renderer);
    void renderArrangementWaveform(NUIRenderer& renderer);
    
    void renderLevelBar(NUIRenderer& renderer, const NUIRect& bounds, float level, float peak, const NUIColor& color);
    void renderSpectrumBar(NUIRenderer& renderer, const NUIRect& bounds, float magnitude, const NUIColor& color);
    
    // Audio data
    std::vector<float> leftBuffer_;
    std::vector<float> rightBuffer_;
    std::vector<float> spectrumBuffer_;
    std::mutex audioDataMutex_;
    
    // Peak detection
    std::atomic<float> leftPeak_;
    std::atomic<float> rightPeak_;
    std::atomic<float> leftRMS_;
    std::atomic<float> rightRMS_;
    std::atomic<float> leftPeakHold_;
    std::atomic<float> rightPeakHold_;
    
    // Smoothed values for fluid animation
    std::atomic<float> leftPeakSmoothed_;
    std::atomic<float> rightPeakSmoothed_;
    std::atomic<float> leftRMSSmoothed_;
    std::atomic<float> rightRMSSmoothed_;
    
    // Visualization settings
    AudioVisualizationMode mode_;
    float sensitivity_;
    float decayRate_;
    NUIColor primaryColor_;
    NUIColor secondaryColor_;
    bool showStereo_;
    bool showPeakHold_;
    
    // Display data
    std::vector<float> displayBuffer_;
    size_t displayBufferSize_;
    size_t currentSample_;
    
    // Animation
    float animationTime_;
    float peakDecayTime_;
    float smoothingFactor_;  // Exponential smoothing (0.0 = instant, 1.0 = slow)

    // UI-thread-only peak hold timers and clip indicators
    float leftPeakHoldTimer_{0.0f};
    float rightPeakHoldTimer_{0.0f};
    float leftClipIndicator_{0.0f};
    float rightClipIndicator_{0.0f};
    
    // Audio manager reference
    Nomad::Audio::AudioDeviceManager* audioManager_;
    
    // Theme colors
    NUIColor backgroundColor_;
    NUIColor gridColor_;
    NUIColor textColor_;
    
    // Arrangement waveform (scrolling transport)
    std::shared_ptr<Nomad::Audio::WaveformCache> arrangementWaveform_;
    std::atomic<double> transportPosition_{0.0};
    double projectDuration_{0.0};
    double clipStartTime_{0.0};      // When the clip starts on timeline (seconds)
    double waveformSampleRate_{48000.0}; // Sample rate of the waveform cache
};

} // namespace NomadUI
