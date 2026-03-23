// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "AudioExporter.h"
#include "AestraLog.h"

#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Aestra {
namespace Audio {

//==============================================================================
// Constants
//==============================================================================

static constexpr uint32_t WAV_HEADER_SIZE = 44;
static constexpr uint32_t RENDER_BUFFER_FRAMES = 4096;

//==============================================================================
// Lifecycle
//==============================================================================

AudioExporter::AudioExporter(AudioEngine& engine, TrackManager& trackManager)
    : m_engine(engine)
    , m_trackManager(trackManager)
{
}

//==============================================================================
// Render
//==============================================================================

AudioExporter::Result AudioExporter::render(const Config& config) {
    Result result;
    result.outputPath = config.outputPath;
    
    // Validate config
    if (config.outputPath.empty()) {
        result.errorMessage = "No output path specified";
        return result;
    }
    
    if (config.sampleRate == 0) {
        result.errorMessage = "Invalid sample rate";
        return result;
    }
    
    // Determine render duration
    double renderDuration = 0.0;
    switch (config.scope) {
        case RenderScope::FullSong:
            renderDuration = 60.0 + config.tailSeconds;  // TODO: Get from playlist
            break;
        case RenderScope::LoopRegion:
            // TODO: Get loop region from transport
            renderDuration = 60.0 + config.tailSeconds;  // TODO: Get from playlist
            break;
        case RenderScope::Selection:
            renderDuration = config.endTimeSeconds - config.startTimeSeconds + config.tailSeconds;
            if (renderDuration <= 0) {
                result.errorMessage = "Invalid selection range";
                return result;
            }
            break;
    }
    
    if (renderDuration <= 0) {
        result.errorMessage = "Nothing to render (empty timeline)";
        return result;
    }
    
    // Calculate total frames
    uint64_t totalFrames = static_cast<uint64_t>(renderDuration * config.sampleRate);
    
    // Open output file
    std::ofstream file(config.outputPath, std::ios::binary);
    if (!file) {
        result.errorMessage = "Cannot open output file: " + config.outputPath;
        return result;
    }
    
    // Write WAV header (will be updated with final size)
    if (!writeWavHeader(file, config, totalFrames)) {
        result.errorMessage = "Failed to write WAV header";
        return result;
    }
    
    // Setup render state
    m_isRendering.store(true, std::memory_order_release);
    m_cancelled.store(false, std::memory_order_release);
    m_peakLevel.store(0.0f, std::memory_order_release);
    m_lastProgressTime = std::chrono::steady_clock::now();
    
    struct RenderGuard {
        AudioExporter& exporter;
        explicit RenderGuard(AudioExporter& e) : exporter(e) {}
        ~RenderGuard() { exporter.m_isRendering.store(false, std::memory_order_release); }
    } guard(*this);
    
    Log::info("[Export] Starting render: " + config.outputPath);
    Log::info("[Export] Duration: " + std::to_string(renderDuration) + "s, " +
              "SampleRate: " + std::to_string(config.sampleRate) + ", " +
              "BitDepth: " + std::to_string(static_cast<int>(config.bitDepth)));
    
    // Pre-allocate render buffer
    m_renderBuffer.resize(static_cast<size_t>(RENDER_BUFFER_FRAMES) * config.numChannels);
    
    // Save original engine state
    uint32_t originalSampleRate = m_engine.getSampleRate();
    bool wasPlaying = false;  // TODO: Check transport state
    
    // Setup engine for offline rendering
    m_engine.setSampleRate(config.sampleRate);
    m_trackManager.setOutputSampleRate(static_cast<double>(config.sampleRate));
    
    // Set initial position based on scope
    double currentTime = 0.0;
    if (config.scope == RenderScope::Selection) {
        currentTime = config.startTimeSeconds;
    }
    m_trackManager.setPosition(currentTime);
    
    // Start playback for rendering
    // m_trackManager.play();  // TODO: Start transport
    
    // Render audio
    bool renderOk = writeAudioData(file, config, result);
    
    // Restore engine state
    // m_trackManager.stop();  // TODO: Stop transport
    m_engine.setSampleRate(originalSampleRate);
    m_trackManager.setOutputSampleRate(static_cast<double>(originalSampleRate));
    
    if (!renderOk) {
        if (shouldCancel()) {
            result.errorMessage = "Render cancelled by user";
        } else {
            result.errorMessage = "Render failed during audio processing";
        }
        file.close();
        // Delete partial file
        std::remove(config.outputPath.c_str());
        return result;
    }
    
    // Update WAV header with final size
    file.seekp(0, std::ios::beg);
    writeWavHeader(file, config, result.framesRendered);
    
    file.close();
    
    // Fill result
    result.success = true;
    result.durationSeconds = static_cast<double>(result.framesRendered) / config.sampleRate;
    result.peakDb = 20.0f * std::log10(std::max(m_peakLevel.load(), 1e-10f));
    
    Log::info("[Export] Render complete: " + std::to_string(result.framesRendered) + 
              " frames, peak: " + std::to_string(result.peakDb) + " dB");
    
    updateProgress(1.0f);
    
    return result;
}

//==============================================================================
// Internal
//==============================================================================

bool AudioExporter::writeWavHeader(std::ofstream& file, const Config& config, uint64_t totalFrames) {
    if (!file) return false;
    
    uint32_t byteRate = config.sampleRate * config.numChannels * (static_cast<int>(config.bitDepth) / 8);
    uint32_t blockAlign = config.numChannels * (static_cast<int>(config.bitDepth) / 8);
    uint32_t dataSize = static_cast<uint32_t>(totalFrames * blockAlign);
    uint32_t fileSize = 36 + dataSize;
    
    // RIFF chunk
    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char*>(&fileSize), 4);
    file.write("WAVE", 4);
    
    // fmt chunk
    file.write("fmt ", 4);
    uint32_t fmtSize = 16;
    file.write(reinterpret_cast<const char*>(&fmtSize), 4);
    
    uint16_t audioFormat = (config.bitDepth == BitDepth::Float_32) ? 3 : 1;  // 3 = IEEE float, 1 = PCM
    file.write(reinterpret_cast<const char*>(&audioFormat), 2);
    
    uint16_t numChannels = static_cast<uint16_t>(config.numChannels);
    file.write(reinterpret_cast<const char*>(&numChannels), 2);
    
    file.write(reinterpret_cast<const char*>(&config.sampleRate), 4);
    file.write(reinterpret_cast<const char*>(&byteRate), 4);
    file.write(reinterpret_cast<const char*>(&blockAlign), 2);
    
    uint16_t bitsPerSample = static_cast<uint16_t>(config.bitDepth);
    file.write(reinterpret_cast<const char*>(&bitsPerSample), 2);
    
    // data chunk
    file.write("data", 4);
    file.write(reinterpret_cast<const char*>(&dataSize), 4);
    
    return file.good();
}

bool AudioExporter::writeAudioData(std::ofstream& file, const Config& config, Result& result) {
    uint64_t framesRemaining = static_cast<uint64_t>(
        (result.durationSeconds > 0 ? result.durationSeconds : 60.0) * config.sampleRate
    );
    
    uint64_t totalFrames = framesRemaining;
    result.framesRendered = 0;
    
    while (framesRemaining > 0 && !shouldCancel()) {
        uint32_t framesToRender = static_cast<uint32_t>(
            std::min<uint64_t>(RENDER_BUFFER_FRAMES, framesRemaining)
        );
        
        // Process audio through engine
        m_engine.processBlock(m_renderBuffer.data(), nullptr, framesToRender, 0.0);
        
        // Track peak level
        m_peakLevel.store(std::max(m_peakLevel.load(), 
            std::abs(calculatePeakDb(m_renderBuffer.data(), framesToRender, config.numChannels))), 
            std::memory_order_relaxed);
        
        // Write samples based on bit depth
        bool writeOk = false;
        switch (config.bitDepth) {
            case BitDepth::PCM_16:
                writeOk = writeSamples<int16_t>(file, m_renderBuffer, framesToRender, config.numChannels);
                break;
            case BitDepth::PCM_24:
                // 24-bit requires special handling
                writeOk = writeSamples<int32_t>(file, m_renderBuffer, framesToRender, config.numChannels);
                break;
            case BitDepth::Float_32:
                writeOk = writeSamples<float>(file, m_renderBuffer, framesToRender, config.numChannels);
                break;
        }
        
        if (!writeOk) {
            return false;
        }
        
        framesRemaining -= framesToRender;
        result.framesRendered += framesToRender;
        
        // Update progress
        float progress = static_cast<float>(result.framesRendered) / static_cast<float>(totalFrames);
        updateProgress(progress);
    }
    
    return !shouldCancel();
}

template<typename SampleType>
bool AudioExporter::writeSamples(std::ofstream& file, const std::vector<float>& buffer, 
                                  size_t frames, uint32_t channels) {
    if constexpr (std::is_same_v<SampleType, float>) {
        // Float 32 - write directly
        file.write(reinterpret_cast<const char*>(buffer.data()), 
                   frames * channels * sizeof(float));
    } else if constexpr (std::is_same_v<SampleType, int16_t>) {
        // PCM 16-bit - convert and clip
        std::vector<int16_t> converted(frames * channels);
        for (size_t i = 0; i < frames * channels; ++i) {
            float sample = std::clamp(buffer[i], -1.0f, 1.0f);
            converted[i] = static_cast<int16_t>(sample * 32767.0f);
        }
        file.write(reinterpret_cast<const char*>(converted.data()), 
                   converted.size() * sizeof(int16_t));
    } else if constexpr (std::is_same_v<SampleType, int32_t>) {
        // PCM 24-bit stored in 32-bit container
        std::vector<int32_t> converted(frames * channels);
        for (size_t i = 0; i < frames * channels; ++i) {
            float sample = std::clamp(buffer[i], -1.0f, 1.0f);
            // 24-bit range: -8388608 to 8388607
            converted[i] = static_cast<int32_t>(sample * 8388607.0f);
        }
        file.write(reinterpret_cast<const char*>(converted.data()), 
                   converted.size() * sizeof(int32_t));
    }
    
    return file.good();
}

void AudioExporter::updateProgress(float percent) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - m_lastProgressTime;
    
    // Throttle progress updates
    if (elapsed >= std::chrono::milliseconds(100)) {
        m_lastProgressTime = now;
        if (m_progressCallback) {
            m_progressCallback(percent);
        }
    }
}

bool AudioExporter::shouldCancel() {
    if (m_cancelCheck) {
        return m_cancelCheck();
    }
    return m_cancelled.load(std::memory_order_acquire);
}

float AudioExporter::calculatePeakDb(const float* buffer, size_t frames, uint32_t channels) {
    float peak = 0.0f;
    for (size_t i = 0; i < frames * channels; ++i) {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return peak;
}

//==============================================================================
// Utility
//==============================================================================

std::string AudioExporter::getDefaultExportName(const std::string& projectPath) {
    if (projectPath.empty()) {
        return "Aestra_Export.wav";
    }
    
    std::filesystem::path path(projectPath);
    std::string stem = path.stem().string();
    return stem + "_Export.wav";
}

std::vector<AudioExporter::BitDepth> AudioExporter::getSupportedBitDepths() {
    return {BitDepth::PCM_16, BitDepth::PCM_24, BitDepth::Float_32};
}

std::string AudioExporter::bitDepthToString(BitDepth depth) {
    switch (depth) {
        case BitDepth::PCM_16: return "16-bit PCM";
        case BitDepth::PCM_24: return "24-bit PCM";
        case BitDepth::Float_32: return "32-bit Float";
        default: return "Unknown";
    }
}

} // namespace Audio
} // namespace Aestra
