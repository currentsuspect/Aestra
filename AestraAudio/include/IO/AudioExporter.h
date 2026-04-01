// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/AudioEngine.h"
#include "../Models/TrackManager.h"
#include <string>
#include <functional>
#include <atomic>
#include <chrono>
#include <mutex>

namespace Aestra {
namespace Audio {

/**
 * @brief Offline audio export/render engine (v2.0)
 *
 * Renders the project to a WAV file using AudioRenderer::renderBlock() —
 * the same rendering path used by bounceRangeToWav() and real-time playback.
 *
 * Key differences from v1:
 * - Uses AudioRenderer::renderBlock() instead of AudioEngine::processBlock()
 * - Duration is computed from the actual playlist timeline
 * - Position advances correctly between blocks
 * - Master output stage (DC block, soft clip, dither) matches playback
 * - Supports FullSong, LoopRegion, and Selection scopes
 *
 * Usage:
 *   AudioExporter exporter(engine, trackManager);
 *   exporter.setProgressCallback([](float pct) { updateUI(pct); });
 *
 *   AudioExporter::Config config;
 *   config.outputPath = "song.wav";
 *   config.sampleRate = 48000;
 *   config.bitDepth = BitDepth::PCM_24;
 *
 *   auto result = exporter.render(config);
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

        // Time range in beats (used for FullSong and LoopRegion)
        double startBeat = 0.0;
        double endBeat = 0.0;

        // Time range in seconds (used if scope is Selection)
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

    void updateProgress(float percent);
    bool shouldCancel();

    float calculatePeakDb(const float* buffer, size_t frames, uint32_t channels);

    // Convert float buffer to target bit depth and write
    template<typename SampleType>
    bool writeSamples(std::ofstream& file, const float* buffer,
                      size_t frames, uint32_t channels);

    // Master output processing (matches playback path)
    void applyMasterOutputStage(float* buffer, uint32_t numFrames);

    // Compute render duration in beats from config + playlist
    double computeRenderDurationBeats(const Config& config, double& outStartBeat);

private:
    AudioEngine& m_engine;
    TrackManager& m_trackManager;

    std::atomic<bool> m_isRendering{false};
    std::atomic<bool> m_cancelled{false};

    ProgressCallback m_progressCallback;
    CancelCheck m_cancelCheck;

    std::chrono::steady_clock::time_point m_lastProgressTime;

    // Render buffers (double for internal, float for output)
    std::vector<double> m_renderBufferD;
    std::vector<float> m_renderBufferF;

    // Peak tracking
    std::atomic<float> m_peakLevel{0.0f};

    // Master output state (per-render)
    struct DCBlockerD {
        double x1{0.0};
        double y1{0.0};
        static constexpr double R = 0.9997;
        inline double process(double x) {
            double y = x - x1 + R * y1;
            x1 = x;
            y1 = y;
            return y;
        }
    };
    DCBlockerD m_dcBlockerL;
    DCBlockerD m_dcBlockerR;
    std::mt19937 m_ditherRng;
};

} // namespace Audio
} // namespace Aestra
