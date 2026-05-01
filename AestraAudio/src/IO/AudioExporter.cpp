// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "AudioExporter.h"
#include "AestraLog.h"

#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <random>
#include <mutex>

namespace Aestra {
namespace Audio {

static constexpr uint32_t RENDER_BLOCK_FRAMES = 4096;

AudioExporter::AudioExporter(AudioEngine& engine, TrackManager& trackManager)
    : m_engine(engine)
    , m_trackManager(trackManager)
{
}

AudioExporter::Result AudioExporter::render(const Config& config) {
    Result result;
    result.outputPath = config.outputPath;

    if (config.outputPath.empty()) {
        result.errorMessage = "No output path specified";
        return result;
    }
    if (config.sampleRate == 0) {
        result.errorMessage = "Invalid sample rate";
        return result;
    }
    if (config.numChannels == 0 || config.numChannels > 64) {
        result.errorMessage = "Invalid channel count";
        return result;
    }

    // Compute render duration from actual playlist
    double startBeat = 0.0;
    double durationBeats = computeRenderDurationBeats(config, startBeat);
    if (durationBeats <= 0.0) {
        switch (config.scope) {
            case RenderScope::FullSong:
                result.errorMessage = "Nothing to render (empty timeline)";
                break;
            case RenderScope::LoopRegion:
                result.errorMessage = "Nothing to render (invalid or empty loop region)";
                break;
            case RenderScope::Selection:
                result.errorMessage = "Nothing to render (no selection range)";
                break;
        }
        return result;
    }

    // Add tail
    double tailBeats = 0.0;
    if (config.tailSeconds > 0.0) {
        double bpm = m_engine.getBPM();
        if (bpm > 0.0) {
            double secondsPerBeat = 60.0 / bpm;
            tailBeats = config.tailSeconds / secondsPerBeat;
        }
    }
    durationBeats += tailBeats;

    // Convert beats to samples
    double bpm = m_engine.getBPM();
    double sampleRate = static_cast<double>(config.sampleRate);
    if (bpm <= 0.0) bpm = 120.0;
    double samplesPerBeat = (sampleRate * 60.0) / bpm;
    uint64_t startSample = static_cast<uint64_t>(startBeat * samplesPerBeat);
    uint64_t totalFrames = static_cast<uint64_t>(durationBeats * samplesPerBeat);

    if (totalFrames == 0) {
        result.errorMessage = "Nothing to render (zero frames)";
        return result;
    }
    int bitsPerSample = static_cast<int>(config.bitDepth);
    uint64_t blockAlign64 = static_cast<uint64_t>(config.numChannels) * static_cast<uint64_t>(bitsPerSample / 8);
    if (blockAlign64 == 0 || blockAlign64 > std::numeric_limits<uint16_t>::max() ||
        totalFrames > std::numeric_limits<uint32_t>::max() / blockAlign64 ||
        totalFrames > std::numeric_limits<uint64_t>::max() / blockAlign64 ||
        36ull + totalFrames * blockAlign64 > std::numeric_limits<uint32_t>::max()) {
        result.errorMessage = "WAV export is too large for RIFF/WAV; use a shorter range or lower format settings";
        return result;
    }

    // Open output file
    std::ofstream file(config.outputPath, std::ios::binary);
    if (!file) {
        result.errorMessage = "Cannot open output file: " + config.outputPath;
        return result;
    }

    // Write placeholder WAV header
    if (!writeWavHeader(file, config, totalFrames)) {
        result.errorMessage = "Failed to write WAV header";
        return result;
    }

    // Setup render state
    m_isRendering.store(true, std::memory_order_release);
    m_cancelled.store(false, std::memory_order_release);
    m_peakLevel.store(0.0f, std::memory_order_release);
    m_lastProgressTime = std::chrono::steady_clock::now();
    m_progressInterval = config.progressInterval;

    struct RenderGuard {
        AudioExporter& exporter;
        explicit RenderGuard(AudioExporter& e) : exporter(e) {}
        ~RenderGuard() { exporter.m_isRendering.store(false, std::memory_order_release); }
    } guard(*this);

    Log::info("[Export] Starting render: " + config.outputPath);
    Log::info("[Export] Duration: " + std::to_string(durationBeats) + " beats (" +
              std::to_string(static_cast<double>(totalFrames) / sampleRate) + "s), " +
              "SampleRate: " + std::to_string(config.sampleRate) + ", " +
              "BitDepth: " + bitDepthToString(config.bitDepth));

    // Pre-allocate render buffers
    m_renderBufferD.resize(static_cast<size_t>(RENDER_BLOCK_FRAMES) * config.numChannels);
    m_renderBufferF.resize(static_cast<size_t>(RENDER_BLOCK_FRAMES) * config.numChannels);

    // Save original engine state
    uint32_t originalSampleRate = m_engine.getSampleRate();
    bool wasPlaying = m_engine.isTransportPlaying();
    uint64_t savedSamplePos = m_engine.getGlobalSamplePos();

    // Stop transport for safe offline rendering
    if (wasPlaying) {
        m_engine.setTransportPlaying(false);
        for (int i = 0; i < 20 && m_engine.isTransportPlaying(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    // Set engine to export sample rate
    m_engine.setSampleRate(config.sampleRate);
    m_trackManager.setOutputSampleRate(sampleRate);

    // Offline export should follow the exact live engine path to avoid render-path
    // mismatches between playback and export.
    const bool wasMetronomeEnabled = m_engine.isMetronomeEnabled();
    const bool wasAuditionEnabled = m_engine.isAuditionModeEnabled();
    m_engine.setMetronomeEnabled(false);
    m_engine.setAuditionModeEnabled(false);
    m_engine.setGlobalSamplePos(startSample);
    m_engine.setTransportPlaying(true);

    // Zero the render buffer before the first block so stale DSP state from
    // prior playback cannot bleed into the export render.
    std::fill(m_renderBufferF.begin(), m_renderBufferF.end(), 0.0f);

    // Render loop using AudioRenderer::renderBlock (same path as bounceRangeToWav)
    uint64_t framesRemaining = totalFrames;
    result.framesRendered = 0;
    while (framesRemaining > 0 && !shouldCancel()) {
        uint32_t framesThisBlock = static_cast<uint32_t>(
            std::min<uint64_t>(RENDER_BLOCK_FRAMES, framesRemaining));

        m_engine.processBlock(m_renderBufferF.data(), nullptr, framesThisBlock, 0.0);

        // Track peak level
        float blockPeak = calculatePeakDb(m_renderBufferF.data(), framesThisBlock, config.numChannels);
        m_peakLevel.store(std::max(m_peakLevel.load(std::memory_order_relaxed), blockPeak),
                          std::memory_order_relaxed);

        // Write samples
        bool writeOk = false;
        switch (config.bitDepth) {
            case BitDepth::PCM_16:
                writeOk = writeSamples<int16_t>(file, m_renderBufferF.data(), framesThisBlock, config.numChannels);
                break;
            case BitDepth::PCM_24:
                writeOk = writeSamples<int32_t>(file, m_renderBufferF.data(), framesThisBlock, config.numChannels);
                break;
            case BitDepth::Float_32:
                writeOk = writeSamples<float>(file, m_renderBufferF.data(), framesThisBlock, config.numChannels);
                break;
        }

        if (!writeOk) {
            result.errorMessage = "Failed to write audio data";
            break;
        }

        // Advance position
        framesRemaining -= framesThisBlock;
        result.framesRendered += framesThisBlock;

        // Update progress
        float progress = static_cast<float>(result.framesRendered) / static_cast<float>(totalFrames);
        updateProgress(progress);
    }

    // Restore engine state
    m_engine.setTransportPlaying(false);
    m_engine.setSampleRate(originalSampleRate);
    m_trackManager.setOutputSampleRate(static_cast<double>(originalSampleRate));
    m_engine.setMetronomeEnabled(wasMetronomeEnabled);
    m_engine.setAuditionModeEnabled(wasAuditionEnabled);
    m_engine.setGlobalSamplePos(savedSamplePos);
    if (wasPlaying) {
        m_engine.setTransportPlaying(true);
    }

    // Check if render completed successfully
    if (framesRemaining == 0 && result.framesRendered > 0) {
        result.success = true;
    }

    if (!result.success) {
        if (shouldCancel()) {
            result.errorMessage = "Render cancelled by user";
        } else {
            result.errorMessage = "Render failed during audio processing";
        }
    }

    if (result.errorMessage.empty()) {
        // Rewrite WAV header with actual frame count
        file.seekp(0, std::ios::beg);
        writeWavHeader(file, config, result.framesRendered);
        file.close();

        result.success = true;
        result.durationSeconds = static_cast<double>(result.framesRendered) / config.sampleRate;
        float peak = m_peakLevel.load(std::memory_order_relaxed);
        result.peakDb = peak > 0.0f ? 20.0 * std::log10(peak) : -96.0;

        Log::info("[Export] Render complete: " + std::to_string(result.framesRendered) +
                  " frames, peak: " + std::to_string(result.peakDb) + " dB");
    } else {
        file.close();
        std::remove(config.outputPath.c_str());
    }

    updateProgress(1.0f);
    return result;
}

double AudioExporter::computeRenderDurationBeats(const Config& config, double& outStartBeat) {
    auto& playlist = m_trackManager.getPlaylistModel();
    double totalBeats = playlist.getTotalDurationBeats();

    switch (config.scope) {
        case RenderScope::FullSong:
            outStartBeat = 0.0;
            return totalBeats > 0.0 ? totalBeats : 0.0;

        case RenderScope::LoopRegion: {
            double loopStart = m_engine.getLoopStartBeat();
            double loopEnd = m_engine.getLoopEndBeat();
            if (loopEnd > loopStart) {
                outStartBeat = loopStart;
                return loopEnd - loopStart;
            }
            return 0.0;
        }

        case RenderScope::Selection: {
            double bpm = m_engine.getBPM();
            if (bpm <= 0.0) bpm = 120.0;
            double secondsPerBeat = 60.0 / bpm;
            outStartBeat = config.startTimeSeconds / secondsPerBeat;
            double endBeat = config.endTimeSeconds / secondsPerBeat;
            double dur = endBeat - outStartBeat;
            return dur > 0.0 ? dur : 0.0;
        }
    }
    return 0.0;
}

bool AudioExporter::writeWavHeader(std::ofstream& file, const Config& config, uint64_t totalFrames) {
    if (!file) return false;

    int bitsPerSample = static_cast<int>(config.bitDepth);
    int bytesPerSample = bitsPerSample / 8;

    uint64_t byteRate64 = static_cast<uint64_t>(config.sampleRate) * config.numChannels * bytesPerSample;
    uint64_t blockAlign64 = static_cast<uint64_t>(config.numChannels) * bytesPerSample;
    if (config.numChannels == 0 || blockAlign64 == 0 ||
        blockAlign64 > std::numeric_limits<uint16_t>::max() ||
        byteRate64 > std::numeric_limits<uint32_t>::max() ||
        totalFrames > std::numeric_limits<uint64_t>::max() / blockAlign64) {
        return false;
    }

    uint64_t dataSize64 = totalFrames * blockAlign64;
    if (dataSize64 > std::numeric_limits<uint32_t>::max() ||
        36ull + dataSize64 > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    uint32_t byteRate = static_cast<uint32_t>(byteRate64);
    uint16_t blockAlign = static_cast<uint16_t>(blockAlign64);
    uint32_t dataSize = static_cast<uint32_t>(dataSize64);
    uint32_t fileSize = 36 + dataSize;

    uint16_t audioFormat = (config.bitDepth == BitDepth::Float_32) ? 3 : 1;

    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char*>(&fileSize), 4);
    file.write("WAVE", 4);

    file.write("fmt ", 4);
    uint32_t fmtSize = 16;
    file.write(reinterpret_cast<const char*>(&fmtSize), 4);
    file.write(reinterpret_cast<const char*>(&audioFormat), 2);

    uint16_t numChannels = static_cast<uint16_t>(config.numChannels);
    file.write(reinterpret_cast<const char*>(&numChannels), 2);
    file.write(reinterpret_cast<const char*>(&config.sampleRate), 4);
    file.write(reinterpret_cast<const char*>(&byteRate), 4);
    file.write(reinterpret_cast<const char*>(&blockAlign), 2);
    file.write(reinterpret_cast<const char*>(&bitsPerSample), 2);

    file.write("data", 4);
    file.write(reinterpret_cast<const char*>(&dataSize), 4);

    return file.good();
}

template<typename SampleType>
bool AudioExporter::writeSamples(std::ofstream& file, const float* buffer,
                                  size_t frames, uint32_t channels) {
    if constexpr (std::is_same_v<SampleType, float>) {
        file.write(reinterpret_cast<const char*>(buffer),
                   frames * channels * sizeof(float));
    } else if constexpr (std::is_same_v<SampleType, int16_t>) {
        std::vector<int16_t> converted(frames * channels);
        for (size_t i = 0; i < frames * channels; ++i) {
            float sample = std::clamp(buffer[i], -1.0f, 1.0f);
            converted[i] = static_cast<int16_t>(sample * 32767.0f);
        }
        file.write(reinterpret_cast<const char*>(converted.data()),
                   converted.size() * sizeof(int16_t));
    } else if constexpr (std::is_same_v<SampleType, int32_t>) {
        std::vector<uint8_t> converted(frames * channels * 3);
        for (size_t i = 0; i < frames * channels; ++i) {
            float sample = std::clamp(buffer[i], -1.0f, 1.0f);
            const int32_t packed24 = static_cast<int32_t>(sample * 8388607.0f);
            converted[i * 3 + 0] = static_cast<uint8_t>(packed24 & 0xFF);
            converted[i * 3 + 1] = static_cast<uint8_t>((packed24 >> 8) & 0xFF);
            converted[i * 3 + 2] = static_cast<uint8_t>((packed24 >> 16) & 0xFF);
        }
        file.write(reinterpret_cast<const char*>(converted.data()), converted.size());
    }

    return file.good();
}

void AudioExporter::updateProgress(float percent) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - m_lastProgressTime;
    if (elapsed >= m_progressInterval) {
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
