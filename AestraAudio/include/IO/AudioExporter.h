// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/AudioEngine.h"
#include "../Models/TrackManager.h"
#include <string>
#include <functional>
#include <atomic>
#include <chrono>

namespace Aestra {
namespace Audio {

/**
 * @brief Offline audio export/render engine (v1.0)
 * 
 * Renders the project to an audio file using the same signal path as realtime playback.
 * Supports WAV output with configurable sample rate and bit depth.
 * 
 * Usage:
 *   AudioExporter exporter(engine, trackManager);
 *   exporter.setProgressCallback([](float pct) { updateUI(pct); });
 *   
 *   AudioExporter::Config config;
 *   config.outputPath = "song.wav";
 *   config.sampleRate = 48000;
 *   config.bitDepth = 24;
 *   config.durationSeconds = trackManager.getMaxTimelineExtent();
 *   
 *   bool ok = exporter.render(config);
 */
class AudioExporter {
public:
    // =============================================================================
    // Types
    // =============================================================================
    
    enum class BitDepth {
        PCM_16 = 16,
        PCM_24 = 24,
        Float_32 = 32  // IEEE float
    };
    
    enum class RenderScope {
        FullSong,       // Render entire timeline
        LoopRegion,     // Render loop region only
        Selection       // Render selected time range
    };
    
    struct Config {
        // Output file path (WAV format)
        std::string outputPath;
        
        // Render scope
        RenderScope scope = RenderScope::FullSong;
        
        // Time range (used if scope is Selection)
        double startTimeSeconds = 0.0;
        double endTimeSeconds = 0.0;
        
        // Audio format
        uint32_t sampleRate = 48000;
        BitDepth bitDepth = BitDepth::PCM_24;
        uint32_t numChannels = 2;  // Stereo output
        
        // Tail handling (reverb/delay tails)
        double tailSeconds = 2.0;  // Extra time for tails
        
        // Progress update interval
        std::chrono::milliseconds progressInterval{100};
    };
    
    struct Result {
        bool success = false;
        std::string errorMessage;
        std::string outputPath;
        uint64_t framesRendered = 0;
        double durationSeconds = 0.0;
        double peakDb = -96.0;  // Measured peak level
        
        bool ok() const { return success; }
    };
    
    // =============================================================================
    // Lifecycle
    // =============================================================================
    
    AudioExporter(AudioEngine& engine, TrackManager& trackManager);
    ~AudioExporter() = default;
    
    // Non-copyable
    AudioExporter(const AudioExporter&) = delete;
    AudioExporter& operator=(const AudioExporter&) = delete;
    
    // =============================================================================
    // Configuration
    // =============================================================================
    
    using ProgressCallback = std::function<void(float percent)>;
    using CancelCheck = std::function<bool()>;  // Return true to cancel
    
    void setProgressCallback(ProgressCallback cb) { m_progressCallback = cb; }
    void setCancelCheck(CancelCheck cb) { m_cancelCheck = cb; }
    
    // =============================================================================
    // Render
    // =============================================================================
    
    /**
     * @brief Render the project to an audio file
     * @param config Export configuration
     * @return Result with success status and metadata
     * 
     * This is a blocking call that renders the entire project.
     * For UI responsiveness, run this on a background thread.
     */
    Result render(const Config& config);
    
    /**
     * @brief Cancel an in-progress render
     * Call this from another thread to stop the render.
     */
    void cancel() { m_cancelled.store(true, std::memory_order_release); }
    
    /**
     * @brief Check if a render is currently in progress
     */
    bool isRendering() const { return m_isRendering.load(std::memory_order_acquire); }
    
    // =============================================================================
    // Utility
    // =============================================================================
    
    /**
     * @brief Get the default export filename based on project name
     */
    static std::string getDefaultExportName(const std::string& projectPath);
    
    /**
     * @brief Get supported bit depths for UI selection
     */
    static std::vector<BitDepth> getSupportedBitDepths();
    
    /**
     * @brief Convert bit depth to string for display
     */
    static std::string bitDepthToString(BitDepth depth);
    
private:
    // =============================================================================
    // Internal
    // =============================================================================
    
    bool writeWavHeader(std::ofstream& file, const Config& config, uint64_t totalFrames);
    bool writeAudioData(std::ofstream& file, const Config& config, Result& result);
    
    void updateProgress(float percent);
    bool shouldCancel();
    
    float calculatePeakDb(const float* buffer, size_t frames, uint32_t channels);
    
    // Convert float buffer to target bit depth and write
    template<typename SampleType>
    bool writeSamples(std::ofstream& file, const std::vector<float>& buffer, 
                      size_t frames, uint32_t channels);
    
private:
    AudioEngine& m_engine;
    TrackManager& m_trackManager;
    
    std::atomic<bool> m_isRendering{false};
    std::atomic<bool> m_cancelled{false};
    
    ProgressCallback m_progressCallback;
    CancelCheck m_cancelCheck;
    
    std::chrono::steady_clock::time_point m_lastProgressTime;
    
    // Render buffer
    std::vector<float> m_renderBuffer;
    
    // Peak tracking
    std::atomic<float> m_peakLevel{0.0f};
};

} // namespace Audio
} // namespace Aestra
