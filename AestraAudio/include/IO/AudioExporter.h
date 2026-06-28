// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/AudioEngine.h"
#include "../Models/TrackManager.h"
#include "IO/AudioExportQuantization.h"
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
 * Renders the project to a WAV file using AudioEngine::processBlock() for
 * master bus output, or AudioRenderer::renderBlock() for isolated track output.
 *
 * Key features:
 * - Master-stage processing (safety limiter, hard clamp, LUFS, true-peak) on full mix
 * - Isolated-track bounce via isolatedTrackIndex (no master stage)
 * - TPDF dither on PCM_16/PCM_24 quantization
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

    /**
     * @brief Immutable export configuration used for an offline render pass.
     */
    struct Config {
        /** @brief Output file path in WAV format. */
        std::string outputPath;

        /** @brief Export scope that determines which project range is rendered. */
        RenderScope scope = RenderScope::FullSong;

        /** @brief Start beat for full-song and loop-region exports. */
        double startBeat = 0.0;
        /** @brief End beat for full-song and loop-region exports. */
        double endBeat = 0.0;

        /** @brief Selection start time in seconds when exporting a time selection. */
        double startTimeSeconds = 0.0;
        /** @brief Selection end time in seconds when exporting a time selection. */
        double endTimeSeconds = 0.0;

        /** @brief Target sample rate for the rendered file. */
        uint32_t sampleRate = 48000;
        /** @brief Output sample representation written to disk. */
        BitDepth bitDepth = BitDepth::PCM_24;
        /** @brief Number of output channels. Stereo is the current default. */
        uint32_t numChannels = 2;  // Stereo output

        /** @brief Extra seconds appended to capture effect tails after the musical range. */
        double tailSeconds = 2.0;  // Extra time for tails

        /** @brief Minimum interval between progress callback updates. */
        std::chrono::milliseconds progressInterval{100};

        // === True Peak Validation (Phase 2) ===
        /**
         * @brief When true, the rendered output is measured with the engine's
         *        ITU-R BS.1770-4 inspired true-peak meter and validated against
         *        @ref truePeakCeilingdBTP. Default off for backward compatibility.
         */
        bool validateTruePeak = false;

        /** @brief Maximum allowed true peak in dBTP (Spotify spec is -1.0). */
        float truePeakCeilingdBTP = -1.0f;

        /**
         * @brief When true, exceeding @ref truePeakCeilingdBTP causes the export
         *        to fail (and the partial WAV is removed). When false, the breach
         *        is logged as a warning and the export still succeeds.
         */
        bool failOnTruePeakExceeded = false;
    };

    /**
     * @brief Final result returned by an export attempt.
     */
    struct Result {
        /** @brief True when rendering and file writing completed successfully. */
        bool success = false;
        /** @brief Failure description when @ref success is false. */
        std::string errorMessage;
        /** @brief Path to the exported file. */
        std::string outputPath;
        /** @brief Total number of audio frames written to the file. */
        uint64_t framesRendered = 0;
        /** @brief Duration of the rendered file in seconds. */
        double durationSeconds = 0.0;
        /** @brief Measured peak output level in decibels full scale. */
        double peakDb = -96.0;  // Measured peak level

        /** @brief Measured true peak (max channel) in dBTP across the rendered range. */
        float maxTruePeakdBTP = -200.0f;

        /** @brief Whether the configured true-peak ceiling was exceeded. */
        bool truePeakCeilingExceeded = false;

        /**
         * @brief Convenience predicate for success checks.
         * @return True when the render completed successfully.
         */
        bool ok() const { return success; }
    };

    // =============================================================================
    // Lifecycle
    // =============================================================================

    /**
     * @brief Create an exporter bound to the live audio engine and track manager.
     * @param engine Audio engine that owns the offline renderer path.
     * @param trackManager Track manager used to resolve playlist and pattern state.
     */
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

    /**
     * @brief Set the callback that receives progress updates during export.
     * @param cb Callback receiving normalized progress in the range [0, 1].
     */
    void setProgressCallback(ProgressCallback cb) { m_progressCallback = cb; }
    /**
     * @brief Set a polling callback used to cancel a render from another thread.
     * @param cb Callback returning true when the current render should stop.
     */
    void setCancelCheck(CancelCheck cb) { m_cancelCheck = cb; }

    // =============================================================================
    // Render
    // =============================================================================

    /**
     * @brief Render the project to an audio file using the full live engine path (processBlock).
     *
     * This is the authoritative offline render implementation with master-stage processing,
     * dithering, and gain smoothing. @ref bounceRangeToWav delegates to this method.
     *
     * @param config Export configuration
     * @return Result with success status and metadata
     *
     * This is a blocking call that renders the entire project.
     * For UI responsiveness, run this on a background thread.
     */
    Result render(const Config& config);

    /**
     * @brief Convenience helper matching AudioEngine::bounceRangeToWav signature.
     *
     * This static helper creates an AudioExporter instance and renders the specified
     * range. For master bounce (trackId == -1) it uses the authoritative offline path
     * (processBlock with master-stage processing and dithering). For isolated track
     * bounce (trackId >= 0) it renders only that track via AudioRenderer::renderBlock
     * without master-stage processing.
     *
     * Used by AudioEngine::bounceRangeToWav to consolidate all offline render
     * authorities through a single exporter path.
     *
     * @param engine Audio engine reference
     * @param trackManager Track manager reference
     * @param startBeat Start position in beats
     * @param endBeat End position in beats
     * @param outputPath Output WAV file path
     * @param trackId Track ID for isolated bounce, or -1 for master output
     * @return Result with success status
     */
    static Result bounceToWav(AudioEngine& engine, TrackManager& trackManager,
                              double startBeat, double endBeat,
                              const std::string& outputPath, int32_t trackId = -1);

    /**
     * @brief Cancel an in-progress render
     * Call this from another thread to stop the render.
     */
    void cancel() { m_cancelled.store(true, std::memory_order_release); }

    /**
     * @brief Check if a render is currently in progress
     * @return True while @ref render is still running.
     */
    bool isRendering() const { return m_isRendering.load(std::memory_order_acquire); }

    // =============================================================================
    // Utility
    // =============================================================================

    /**
     * @brief Get the default export filename based on project name
     * @param projectPath Project file path used to derive a default WAV filename.
     * @return Suggested export filename.
     */
    static std::string getDefaultExportName(const std::string& projectPath);

    /**
     * @brief Get supported bit depths for UI selection
     * @return Ordered list of bit depths exposed by the exporter UI.
     */
    static std::vector<BitDepth> getSupportedBitDepths();

    /**
     * @brief Convert bit depth to string for display
     * @param depth Bit-depth enum to stringify.
     * @return Human-readable label for the supplied bit depth.
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
    std::chrono::milliseconds m_progressInterval{100};

    // Render buffer (float for output)
    std::vector<float> m_renderBufferF;

    // Peak tracking
    std::atomic<float> m_peakLevel{0.0f};

    // Dither state for PCM quantization
    ExportQuantization::TpdfDither m_exportDither;
};

} // namespace Audio
} // namespace Aestra
