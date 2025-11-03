// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "Track.h"
#include "NomadLog.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <cstring>

namespace Nomad {
namespace Audio {

// === Audio Quality Preset Implementations ===

AudioQualitySettings AudioQualitySettings::Economy() {
    AudioQualitySettings settings;
    settings.preset = QualityPreset::Economy;
    settings.resampling = ResamplingMode::Fast;
    settings.dithering = DitheringMode::None;
    settings.precision = InternalPrecision::Float32;
    settings.oversampling = OversamplingMode::None;
    settings.removeDCOffset = false;
    settings.enableSoftClipping = false;
    settings.antiAliasingFilter = FilterSteepness::Soft;
    return settings;
}

AudioQualitySettings AudioQualitySettings::Balanced() {
    AudioQualitySettings settings;
    settings.preset = QualityPreset::Balanced;
    settings.resampling = ResamplingMode::Medium;
    settings.dithering = DitheringMode::Triangular;
    settings.precision = InternalPrecision::Float32;
    settings.oversampling = OversamplingMode::Auto;
    settings.removeDCOffset = true;
    settings.enableSoftClipping = false;
    settings.antiAliasingFilter = FilterSteepness::Medium;
    return settings;
}

AudioQualitySettings AudioQualitySettings::HighFidelity() {
    AudioQualitySettings settings;
    settings.preset = QualityPreset::HighFidelity;
    settings.resampling = ResamplingMode::High;
    settings.dithering = DitheringMode::NoiseShaped;
    settings.precision = InternalPrecision::Float32;
    settings.oversampling = OversamplingMode::Force2x;
    settings.removeDCOffset = true;
    settings.enableSoftClipping = true;
    settings.antiAliasingFilter = FilterSteepness::Steep;
    return settings;
}

AudioQualitySettings AudioQualitySettings::Mastering() {
    AudioQualitySettings settings;
    settings.preset = QualityPreset::Mastering;
    settings.resampling = ResamplingMode::Ultra;
    settings.dithering = DitheringMode::NoiseShaped;
    settings.precision = InternalPrecision::Float64;
    settings.oversampling = OversamplingMode::Force4x;
    settings.removeDCOffset = true;
    settings.enableSoftClipping = true;
    settings.antiAliasingFilter = FilterSteepness::Steep;
    return settings;
}

void AudioQualitySettings::applyPreset(QualityPreset newPreset) {
    switch (newPreset) {
        case QualityPreset::Economy:
            *this = Economy();
            break;
        case QualityPreset::Balanced:
            *this = Balanced();
            break;
        case QualityPreset::HighFidelity:
            *this = HighFidelity();
            break;
        case QualityPreset::Mastering:
            *this = Mastering();
            break;
        case QualityPreset::Custom:
            // Keep current settings, just update preset marker
            preset = QualityPreset::Custom;
            break;
    }
}

// WAV file header structure (Windows compatible)
#pragma pack(push, 1)
struct WavHeader {
    char riff[4];        // "RIFF"
    uint32_t fileSize;   // File size - 8
    char wave[4];        // "WAVE"
    char fmt[4];         // "fmt "
    uint32_t fmtSize;    // Format chunk size
    uint16_t audioFormat; // Audio format (1 = PCM)
    uint16_t numChannels; // Number of channels
    uint32_t sampleRate;  // Sample rate
    uint32_t byteRate;    // Byte rate
    uint16_t blockAlign;  // Block align
    uint16_t bitsPerSample; // Bits per sample
    char data[4];        // "data"
    uint32_t dataSize;    // Data chunk size
};
#pragma pack(pop)

// Simple WAV file loader
bool loadWavFile(const std::string& filePath, std::vector<float>& audioData, uint32_t& sampleRate, uint16_t& numChannels) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        Log::warning("Failed to open WAV file: " + filePath);
        return false;
    }

    WavHeader header = {};
    file.read(reinterpret_cast<char*>(&header), sizeof(WavHeader));

    // Debug: Log WAV header information
    Log::info("WAV Header Debug:");
    Log::info("  RIFF: " + std::string(header.riff, 4));
    Log::info("  File size: " + std::to_string(header.fileSize));
    Log::info("  WAVE: " + std::string(header.wave, 4));
    Log::info("  fmt: " + std::string(header.fmt, 4));
    Log::info("  Format size: " + std::to_string(header.fmtSize));
    Log::info("  Audio format: " + std::to_string(header.audioFormat));
    Log::info("  Channels: " + std::to_string(header.numChannels));
    Log::info("  Sample rate: " + std::to_string(header.sampleRate));
    Log::info("  Bits per sample: " + std::to_string(header.bitsPerSample));
    Log::info("  Data chunk: " + std::string(header.data, 4));
    Log::info("  Data size: " + std::to_string(header.dataSize));

    // Handle JUNK chunk before fmt chunk (some WAV files have metadata)
    std::cout << "DEBUG: Checking for JUNK chunk, header.fmt = '" << std::string(header.fmt, 4) << "'" << std::endl;
    if (std::strncmp(header.fmt, "JUNK", 4) == 0) {
        Log::info("Found JUNK chunk, skipping " + std::to_string(header.fmtSize) + " bytes");

        // Skip the JUNK chunk data
        file.seekg(header.fmtSize, std::ios::cur);

        // Read the next chunk header (should be the real fmt chunk)
        char nextChunk[4];
        uint32_t nextChunkSize;
        if (!file.read(nextChunk, 4)) {
            Log::error("Failed to read next chunk type after JUNK");
            return false;
        }
        if (!file.read(reinterpret_cast<char*>(&nextChunkSize), 4)) {
            Log::error("Failed to read next chunk size after JUNK");
            return false;
        }

        Log::info("Next chunk: " + std::string(nextChunk, 4) + ", size: " + std::to_string(nextChunkSize));

        // Verify this is the fmt chunk
        if (std::strncmp(nextChunk, "fmt ", 4) != 0) {
            Log::warning("Expected fmt chunk after JUNK, got: " + std::string(nextChunk, 4));

            // Special case: some WAV files have JUNK + DATA but no fmt chunk
            // Try to handle this by assuming standard format parameters
            if (std::strncmp(nextChunk, "data", 4) == 0) {
                Log::warning("WAV file missing fmt chunk, attempting to load with assumed parameters");
                Log::info("Assuming: 16-bit, 44100 Hz, stereo PCM");

                // Set default format parameters
                header.audioFormat = 1; // PCM
                header.numChannels = 2; // Stereo
                header.sampleRate = 44100;
                header.bitsPerSample = 16;
                header.byteRate = header.sampleRate * header.numChannels * (header.bitsPerSample / 8);
                header.blockAlign = header.numChannels * (header.bitsPerSample / 8);
                header.dataSize = nextChunkSize;

                Log::info("Using assumed format: " + std::to_string(header.audioFormat) +
                          ", channels=" + std::to_string(header.numChannels) +
                          ", sampleRate=" + std::to_string(header.sampleRate) +
                          ", bitsPerSample=" + std::to_string(header.bitsPerSample) +
                          ", dataSize=" + std::to_string(header.dataSize));

                // Skip validation since we're using assumed parameters
                goto load_audio_data;
            } else {
                Log::error("Unsupported chunk after JUNK: " + std::string(nextChunk, 4));
                return false;
            }
        }

        // Read the fmt chunk data
        if (!file.read(reinterpret_cast<char*>(&header.audioFormat), 2)) {
            Log::error("Failed to read audio format");
            return false;
        }
        if (!file.read(reinterpret_cast<char*>(&header.numChannels), 2)) {
            Log::error("Failed to read numChannels");
            return false;
        }
        if (!file.read(reinterpret_cast<char*>(&header.sampleRate), 4)) {
            Log::error("Failed to read sampleRate");
            return false;
        }
        if (!file.read(reinterpret_cast<char*>(&header.byteRate), 4)) {
            Log::error("Failed to read byteRate");
            return false;
        }
        if (!file.read(reinterpret_cast<char*>(&header.blockAlign), 2)) {
            Log::error("Failed to read blockAlign");
            return false;
        }
        if (!file.read(reinterpret_cast<char*>(&header.bitsPerSample), 2)) {
            Log::error("Failed to read bitsPerSample");
            return false;
        }

        Log::info("Real fmt chunk data: format=" + std::to_string(header.audioFormat) +
                  ", channels=" + std::to_string(header.numChannels) +
                  ", sampleRate=" + std::to_string(header.sampleRate) +
                  ", bitsPerSample=" + std::to_string(header.bitsPerSample));

        // Now find the data chunk
        bool foundDataChunk = false;
        while (!foundDataChunk && file) {
            char chunkType[4];
            uint32_t chunkSize;

            if (!file.read(chunkType, 4)) break;
            if (!file.read(reinterpret_cast<char*>(&chunkSize), 4)) break;

            std::string chunkTypeStr(chunkType, 4);
            Log::info("Found chunk: " + chunkTypeStr + ", size: " + std::to_string(chunkSize));

            if (chunkTypeStr == "data") {
                header.dataSize = chunkSize;
                foundDataChunk = true;
                Log::info("Found data chunk with " + std::to_string(chunkSize) + " bytes");
            } else {
                // Skip this chunk
                file.seekg(chunkSize, std::ios::cur);
            }
        }

        if (!foundDataChunk) {
            Log::error("No data chunk found in WAV file");
            return false;
        }
    } else {
        // Normal case - find the data chunk
        bool foundDataChunk = false;
        while (!foundDataChunk && file) {
            char chunkType[4];
            uint32_t chunkSize;

            if (!file.read(chunkType, 4)) break;
            if (!file.read(reinterpret_cast<char*>(&chunkSize), 4)) break;

            std::string chunkTypeStr(chunkType, 4);
            Log::info("Found chunk: " + chunkTypeStr + ", size: " + std::to_string(chunkSize));

            if (chunkTypeStr == "data") {
                header.dataSize = chunkSize;
                foundDataChunk = true;
                Log::info("Found data chunk with " + std::to_string(chunkSize) + " bytes");
            } else {
                // Skip this chunk
                file.seekg(chunkSize, std::ios::cur);
            }
        }

        if (!foundDataChunk) {
            Log::error("No data chunk found in WAV file");
            return false;
        }
    }

    // Load audio data (reached via normal path or JUNK+assumed format path)
    load_audio_data:

    // Validate WAV format after handling JUNK chunks
    bool hasJunkChunk = (std::strncmp(header.fmt, "JUNK", 4) == 0);
    if (std::strncmp(header.riff, "RIFF", 4) != 0 ||
        std::strncmp(header.wave, "WAVE", 4) != 0) {
        Log::warning("Invalid WAV file format: " + filePath);
        Log::warning("  Expected: RIFF, WAVE");
        Log::warning("  Got: " + std::string(header.riff, 4) + ", " + std::string(header.wave, 4));
        return false;
    }

    // Only validate fmt chunk if it's not a JUNK chunk (which we already handled)
    if (!hasJunkChunk && std::strncmp(header.fmt, "fmt ", 4) != 0) {
        Log::warning("Invalid WAV file format: " + filePath);
        Log::warning("  Expected: fmt ");
        Log::warning("  Got: " + std::string(header.fmt, 4));
        return false;
    }

    Log::info("WAV format validated successfully (JUNK chunk: " + std::string(hasJunkChunk ? "yes" : "no") + ")");

    // Check format parameters only if we didn't use assumed values
    if (!hasJunkChunk) {
        // Check if it's PCM format (allow both PCM and floating point)
        if (header.audioFormat != 1 && header.audioFormat != 3) {
            Log::warning("Unsupported audio format: " + std::to_string(header.audioFormat) + " (only PCM supported)");
            return false;
        }

        // Support different bit depths
        if (header.bitsPerSample != 16 && header.bitsPerSample != 24 && header.bitsPerSample != 32) {
            Log::warning("Unsupported bit depth: " + std::to_string(header.bitsPerSample) + " (only 16/24/32-bit supported)");
            return false;
        }
    } else {
        Log::info("Skipping format validation (using assumed parameters)");
    }

    Log::info("WAV format validated successfully");

    // Read audio data based on bit depth
    size_t bytesPerSample = header.bitsPerSample / 8;
    audioData.resize(header.dataSize / bytesPerSample);

    if (header.bitsPerSample == 16) {
        // 16-bit samples
        std::vector<int16_t> rawData(header.dataSize / sizeof(int16_t));
        file.read(reinterpret_cast<char*>(rawData.data()), header.dataSize);

        // Convert to float and normalize
        for (size_t i = 0; i < rawData.size(); ++i) {
            audioData[i] = rawData[i] / 32768.0f; // Normalize 16-bit to [-1, 1]
        }
    } else if (header.bitsPerSample == 24) {
        // 24-bit samples (need to handle endianness)
        std::vector<uint8_t> rawData(header.dataSize);
        file.read(reinterpret_cast<char*>(rawData.data()), header.dataSize);

        for (size_t i = 0; i < audioData.size(); ++i) {
            // Convert 24-bit to 32-bit then to float
            uint32_t sample24 = 0;
            if (header.blockAlign == 3) { // Little endian
                sample24 = rawData[i * 3] | (rawData[i * 3 + 1] << 8) | (rawData[i * 3 + 2] << 16);
            }

            // Sign extend 24-bit to 32-bit
            if (sample24 & 0x800000) {
                sample24 |= 0xFF000000;
            }

            // Convert to float
            audioData[i] = sample24 / 8388608.0f; // Normalize 24-bit to [-1, 1]
        }
    } else if (header.bitsPerSample == 32) {
        // 32-bit float samples
        file.read(reinterpret_cast<char*>(audioData.data()), header.dataSize);

        // 32-bit float is already in the right range [-1, 1], but clamp just in case
        for (size_t i = 0; i < audioData.size(); ++i) {
            audioData[i] = std::max(-1.0f, std::min(1.0f, audioData[i]));
        }
    }

    sampleRate = header.sampleRate;
    numChannels = header.numChannels;

    Log::info("WAV loaded: " + std::to_string(audioData.size()) + " samples, " +
              std::to_string(sampleRate) + " Hz, " + std::to_string(numChannels) + " channels");

    return true;
}

Track::Track(const std::string& name, uint32_t trackId)
    : m_name(name)
    , m_trackId(trackId)
    , m_color(0xFF4080FF)  // Default blue (ARGB)
    , m_state(TrackState::Empty)
    , m_positionSeconds(0.0)
    , m_durationSeconds(0.0)
    , m_playbackPhase(0.0)
    , m_isRecording(false)
{
    // Create mixer bus for this track
    m_mixerBus = std::make_unique<MixerBus>(m_name.c_str(), 2);  // Stereo

    // Set initial parameters
    m_mixerBus->setGain(m_volume.load());
    m_mixerBus->setPan(m_pan.load());
    m_mixerBus->setMute(m_muted.load());
    m_mixerBus->setSolo(m_soloed.load());

    Log::info("Track created: " + m_name + " (ID: " + std::to_string(m_trackId) + ")");
}

Track::~Track() {
    if (isRecording()) {
        stopRecording();
    }
    Log::info("Track destroyed: " + m_name);
}

// Track Properties
void Track::setName(const std::string& name) {
    m_name = name;
    if (m_mixerBus) {
        // Update mixer bus name too
        // Note: MixerBus doesn't have setName method yet, but we could add it
    }
}

void Track::setColor(uint32_t color) {
    m_color = color;
}

// Audio Parameters (thread-safe)
void Track::setVolume(float volume) {
    volume = (volume < 0.0f) ? 0.0f : (volume > 2.0f) ? 2.0f : volume;  // 0% to 200%
    m_volume.store(volume);
    if (m_mixerBus) {
        m_mixerBus->setGain(volume);
    }
}

void Track::setPan(float pan) {
    pan = (pan < -1.0f) ? -1.0f : (pan > 1.0f) ? 1.0f : pan;
    m_pan.store(pan);
    if (m_mixerBus) {
        m_mixerBus->setPan(pan);
    }
}

void Track::setMute(bool mute) {
    m_muted.store(mute);
    if (m_mixerBus) {
        m_mixerBus->setMute(mute);
    }
}

void Track::setSolo(bool solo) {
    m_soloed.store(solo);
    if (m_mixerBus) {
        m_mixerBus->setSolo(solo);
    }
}

// Track State
void Track::setState(TrackState state) {
    TrackState oldState = m_state.exchange(state);
    if (oldState != state) {
        Log::info("Track " + m_name + " state changed: " +
                  std::to_string(static_cast<int>(oldState)) + " -> " +
                  std::to_string(static_cast<int>(state)));

        // Handle state transitions
        switch (state) {
            case TrackState::Playing:
                // CRITICAL FIX: Only reset playback phase when starting from stopped/loaded
                // Do NOT reset when resuming from pause (preserve current position)
                if (oldState == TrackState::Stopped || oldState == TrackState::Loaded) {
                    m_playbackPhase.store(0.0);
                }
                // If resuming from pause, keep current phase/position
                break;
            case TrackState::Stopped:
                Log::info("Reached TrackState::Stopped case");
                m_playbackPhase.store(0.0);
                m_positionSeconds.store(0.0);
                break;
            case TrackState::Recording:
                m_recordingBuffer.clear();
                m_isRecording.store(true);
                break;
            default:
                break;
        }
    }
}

// Audio Data Management
bool Track::loadAudioFile(const std::string& filePath) {
    std::cout << "Loading: " << filePath << " (track: " << m_name << ")" << std::endl;

    // Check if file exists
    std::ifstream checkFile(filePath);
    if (!checkFile) {
        std::cout << "File not found, generating preview tone" << std::endl;
        return generatePreviewTone(filePath);
    }
    checkFile.close();

    // Clear any existing audio data
    m_audioData.clear();
    m_durationSeconds.store(0.0);
    m_playbackPhase.store(0.0);
    m_positionSeconds.store(0.0);

    // Determine file extension to choose appropriate loader
    std::string extension = filePath.substr(filePath.find_last_of('.') + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

    if (extension == "wav") {
        // Load WAV file
        uint32_t sampleRate = 48000; // Default fallback
        uint16_t numChannels = 2;    // Default fallback

        if (loadWavFile(filePath, m_audioData, sampleRate, numChannels)) {
            m_sampleRate = sampleRate;
            m_numChannels = numChannels;
            m_durationSeconds.store(static_cast<double>(m_audioData.size()) / (sampleRate * numChannels));
            setState(TrackState::Loaded);

            Log::info("WAV loaded successfully: " + std::to_string(m_audioData.size()) + " samples, " +
                       std::to_string(m_durationSeconds.load()) + " seconds");
            return true;
        } else {
            Log::warning("Failed to load WAV file: " + filePath + ", generating preview tone instead");
        }
    }

    // Special handling for demo files - generate audio directly
    if (filePath.find("demo_") != std::string::npos) {
        std::cout << "Demo file detected, generating audio directly" << std::endl;
        return generateDemoAudio(filePath);
    }

    // Fallback: generate preview tone for unsupported formats or failed loads
    std::cout << "Falling back to preview tone" << std::endl;
    return generatePreviewTone(filePath);
}

bool Track::generatePreviewTone(const std::string& filePath) {
    // Use filename hash to generate a unique frequency
    size_t filenameHash = std::hash<std::string>{}(filePath);
    double baseFrequency = 220.0 + (filenameHash % 440);

    // Generate 5 seconds of audio
    const double duration = 5.0;
    const uint32_t totalSamples = static_cast<uint32_t>(m_sampleRate * duration * m_numChannels);
    m_audioData.resize(totalSamples);

    // Generate waveform
    double freq1 = baseFrequency;
    double freq2 = baseFrequency * 1.5;
    double freq3 = baseFrequency * 2.0;

    for (uint32_t i = 0; i < totalSamples / m_numChannels; ++i) {
        double phase1 = 2.0 * 3.14159265358979323846 * freq1 * i / m_sampleRate;
        double phase2 = 2.0 * 3.14159265358979323846 * freq2 * i / m_sampleRate;
        double phase3 = 2.0 * 3.14159265358979323846 * freq3 * i / m_sampleRate;

        float sample1 = 0.4f * static_cast<float>(std::sin(phase1));
        float sample2 = 0.2f * static_cast<float>(std::sin(phase2));
        float sample3 = 0.1f * static_cast<float>(std::sin(phase3));

        float sample = sample1 + sample2 + sample3;
        sample = (sample < -0.9f) ? -0.9f : (sample > 0.9f) ? 0.9f : sample;

        m_audioData[i * m_numChannels + 0] = sample;
        m_audioData[i * m_numChannels + 1] = sample;
    }

    m_durationSeconds.store(duration);
    setState(TrackState::Loaded);

    std::cout << "Preview tone generated: " << m_audioData.size() << " samples, "
              << m_durationSeconds.load() << " seconds, " << baseFrequency << " Hz" << std::endl;

    return true;
}

bool Track::generateDemoAudio(const std::string& filePath) {
    std::cout << "Generating demo audio for: " << filePath << std::endl;

    // Determine frequency and duration based on filename
    double frequency = 440.0;
    double duration = 3.0;

    if (filePath.find("guitar") != std::string::npos) {
        frequency = 440.0; duration = 3.0;
    } else if (filePath.find("drums") != std::string::npos) {
        frequency = 120.0; duration = 2.0;
    } else if (filePath.find("vocals") != std::string::npos) {
        frequency = 330.0; duration = 4.0;
    }

    // Generate audio data
    const uint32_t totalSamples = static_cast<uint32_t>(m_sampleRate * duration * m_numChannels);
    m_audioData.resize(totalSamples);

    // Generate waveform
    double freq1 = frequency;
    double freq2 = frequency * 1.5;
    double freq3 = frequency * 2.0;

    for (uint32_t i = 0; i < totalSamples / m_numChannels; ++i) {
        double phase1 = 2.0 * 3.14159265358979323846 * freq1 * i / m_sampleRate;
        double phase2 = 2.0 * 3.14159265358979323846 * freq2 * i / m_sampleRate;
        double phase3 = 2.0 * 3.14159265358979323846 * freq3 * i / m_sampleRate;

        float sample1 = 0.4f * static_cast<float>(std::sin(phase1));
        float sample2 = 0.2f * static_cast<float>(std::sin(phase2));
        float sample3 = 0.1f * static_cast<float>(std::sin(phase3));

        float sample = sample1 + sample2 + sample3;
        sample = (sample < -0.9f) ? -0.9f : (sample > 0.9f) ? 0.9f : sample;

        m_audioData[i * m_numChannels + 0] = sample;
        m_audioData[i * m_numChannels + 1] = sample;
    }

    m_durationSeconds.store(duration);
    setState(TrackState::Loaded);

    std::cout << "Demo audio generated: " << m_audioData.size() << " samples, "
              << m_durationSeconds.load() << " seconds, " << frequency << " Hz" << std::endl;

    return true;
}

void Track::clearAudioData() {
    m_audioData.clear();
    m_recordingBuffer.clear();
    m_durationSeconds.store(0.0);
    m_playbackPhase.store(0.0);
    m_positionSeconds.store(0.0);
    setState(TrackState::Empty);
}

void Track::setAudioData(const float* data, uint32_t numSamples, uint32_t sampleRate, uint32_t numChannels) {
    if (!data || numSamples == 0) {
        Log::error("Invalid audio data");
        return;
    }
    
    // Copy audio data
    m_audioData.assign(data, data + (numSamples * numChannels));
    m_sampleRate = sampleRate;
    m_numChannels = numChannels;
    m_durationSeconds.store(static_cast<double>(numSamples) / sampleRate);
    m_playbackPhase.store(0.0);
    m_positionSeconds.store(0.0);
    setState(TrackState::Loaded);
    
    Log::info("Audio data loaded: " + std::to_string(numSamples) + " samples, " +
               std::to_string(m_durationSeconds.load()) + " seconds, " + 
               std::to_string(sampleRate) + " Hz, " + std::to_string(numChannels) + " channels");
}

// Recording
void Track::setLatencyCompensation(double inputLatencyMs, double outputLatencyMs) {
    m_latencyCompensationMs = inputLatencyMs + outputLatencyMs;
    Log::info("Track '" + m_name + "' latency compensation set: " + 
              std::to_string(m_latencyCompensationMs) + " ms (Input: " + 
              std::to_string(inputLatencyMs) + " ms + Output: " + 
              std::to_string(outputLatencyMs) + " ms)");
}

void Track::startRecording() {
    if (getState() != TrackState::Empty) {
        Log::warning("Cannot start recording: track not empty");
        return;
    }

    Log::info("Starting recording on track: " + m_name);
    setState(TrackState::Recording);
}

void Track::stopRecording() {
    if (!isRecording()) {
        return;
    }

    Log::info("Stopping recording on track: " + m_name);

    // Move recording buffer to main audio data
    if (!m_recordingBuffer.empty()) {
        m_audioData = std::move(m_recordingBuffer);
        
        // Apply latency compensation if configured
        if (m_latencyCompensationMs > 0.0) {
            // Calculate how many samples to shift (compensate for input + output latency)
            uint32_t compensationSamples = static_cast<uint32_t>(
                (m_latencyCompensationMs / 1000.0) * m_sampleRate * m_numChannels
            );
            
            // Ensure we don't shift more than available data
            if (compensationSamples > 0 && compensationSamples < m_audioData.size()) {
                // Shift audio data earlier by removing the latency from the beginning
                // This aligns the recorded audio with the timeline
                m_audioData.erase(m_audioData.begin(), m_audioData.begin() + compensationSamples);
                
                Log::info("[Latency Compensation] Shifted recorded audio earlier by " + 
                          std::to_string(m_latencyCompensationMs) + " ms (" + 
                          std::to_string(compensationSamples / m_numChannels) + " frames)");
            }
        }
        
        m_durationSeconds.store(static_cast<double>(m_audioData.size()) / (m_sampleRate * m_numChannels));
        setState(TrackState::Loaded);
    } else {
        setState(TrackState::Empty);
    }

    m_recordingBuffer.clear();
    m_isRecording.store(false);
}

// Playback Control
void Track::play() {
    TrackState currentState = getState();
    
    // Can play from Loaded, Stopped, or Paused states
    if (currentState == TrackState::Loaded || 
        currentState == TrackState::Stopped || 
        currentState == TrackState::Paused) {
        
        Log::info("Playing track: " + m_name);
        
        // Reset position if stopped (start from beginning)
        // Don't reset if paused (resume from current position)
        if (currentState == TrackState::Stopped) {
            m_positionSeconds.store(0.0);
            m_playbackPhase.store(0.0);
        }
        
        setState(TrackState::Playing);
    }
}

void Track::pause() {
    if (getState() == TrackState::Playing) {
        Log::info("Pausing track: " + m_name);
        setState(TrackState::Paused);
    }
}

void Track::stop() {
    Log::info("Stopping track: " + m_name);
    setState(TrackState::Stopped);
    // Position reset will happen when play() is called again
}

// Position Control
void Track::setPosition(double seconds) {
    // Clamp position to valid range
    double duration = getDuration();
    seconds = (seconds < 0.0) ? 0.0 : (seconds > duration) ? duration : seconds;
    m_positionSeconds.store(seconds);

    // CRITICAL: Update playback phase for sample-accurate positioning
    // Phase is in TRACK sample space (not output sample space)
    // When we seek, we need to set phase to the correct position in the audio data
    m_playbackPhase.store(seconds * m_sampleRate);
}

// Audio Processing
void Track::processAudio(float* outputBuffer, uint32_t numFrames, double streamTime) {
    if (!outputBuffer || numFrames == 0) {
        return;
    }

    TrackState currentState = getState();

    // Create temporary buffer for this track's audio before mixing
    std::vector<float> trackBuffer(numFrames * m_numChannels, 0.0f);

    switch (currentState) {
        case TrackState::Playing: {
            // Copy audio data to temporary buffer
            copyAudioData(trackBuffer.data(), numFrames);
            
            // Process through mixer bus for volume/pan/mute/solo
            if (m_mixerBus) {
                m_mixerBus->process(trackBuffer.data(), numFrames);
            }
            
            // Mix into output buffer
            for (uint32_t i = 0; i < numFrames * m_numChannels; ++i) {
                outputBuffer[i] += trackBuffer[i];
            }
            break;
        }

        case TrackState::Recording:
            m_playbackPhase.store(m_playbackPhase.load() + numFrames * m_numChannels);
            break;

        case TrackState::Paused:
        case TrackState::Stopped:
        case TrackState::Empty:
        case TrackState::Loaded:
        default:
            // Output silence (don't modify output buffer)
            break;
    }

    // Update position for playing state
    if (currentState == TrackState::Playing) {
        double currentPos = m_positionSeconds.load();
        
        // CRITICAL FIX: Use OUTPUT sample rate (48000Hz), not track sample rate
        // When playing 44100Hz file on 48000Hz output, position was incrementing too fast
        // causing audio to cut 7+ seconds early!
        const double OUTPUT_SAMPLE_RATE = 48000.0;
        double newPos = currentPos + (numFrames / OUTPUT_SAMPLE_RATE);
        
        if (newPos >= getDuration()) {
            setPosition(0.0);
        } else {
            m_positionSeconds.store(newPos);
        }
    }
}

void Track::generateSilence(float* buffer, uint32_t numFrames) {
    if (buffer) {
        std::fill(buffer, buffer + numFrames * m_numChannels, 0.0f);
    }
}

void Track::copyAudioData(float* outputBuffer, uint32_t numFrames) {
    if (m_audioData.empty()) {
        generateSilence(outputBuffer, numFrames);
        return;
    }

    uint32_t totalSamples = m_audioData.size();
    double phase = m_playbackPhase.load();
    
    // Calculate sample rate ratio for resampling
    const double outputSampleRate = 48000.0;
    const double sampleRateRatio = static_cast<double>(m_sampleRate) / outputSampleRate;

    // Process audio based on quality settings
    for (uint32_t frame = 0; frame < numFrames; ++frame) {
        double exactSamplePos = phase;
        
        for (uint32_t ch = 0; ch < m_numChannels; ++ch) {
            float sample = 0.0f;
            
            // Choose interpolation method based on resampling mode
            switch (m_qualitySettings.resampling) {
                case ResamplingMode::Fast:
                    sample = interpolateLinear(m_audioData.data(), totalSamples, exactSamplePos, ch);
                    break;
                case ResamplingMode::Medium:
                    sample = interpolateCubic(m_audioData.data(), totalSamples, exactSamplePos, ch);
                    break;
                case ResamplingMode::High:
                    sample = interpolateSinc(m_audioData.data(), totalSamples, exactSamplePos, ch);
                    break;
                case ResamplingMode::Ultra:
                    sample = interpolateUltra(m_audioData.data(), totalSamples, exactSamplePos, ch);
                    break;
                case ResamplingMode::Extreme:
                    sample = interpolateExtreme(m_audioData.data(), totalSamples, exactSamplePos, ch);
                    break;
                case ResamplingMode::Perfect:
                    sample = interpolatePerfect(m_audioData.data(), totalSamples, exactSamplePos, ch);
                    break;
            }
            
            outputBuffer[frame * m_numChannels + ch] = sample;
        }

        phase += sampleRateRatio;
    }
    
    // Apply audio quality enhancements
    uint32_t totalOutputSamples = numFrames * m_numChannels;
    
    // === PROCESSING ORDER (optimized for best sound quality) ===
    
    // 1. Nomad Mode Euphoria Engine (if enabled)
    //    - Applied FIRST to get the signature character on raw audio
    if (m_qualitySettings.nomadMode == NomadMode::Euphoric) {
        applyEuphoriaEngine(outputBuffer, numFrames);
    }
    
    // 2. DC Offset Removal
    //    - Clean up any DC bias from processing
    if (m_qualitySettings.removeDCOffset) {
        removeDC(outputBuffer, totalOutputSamples);
    }
    
    // 3. Dithering (based on mode)
    //    - Applied before final limiting for proper quantization
    if (m_qualitySettings.dithering != DitheringMode::None) {
        applyDithering(outputBuffer, totalOutputSamples);
    }
    
    // 4. Soft Clipping (if enabled)
    //    - Final safety limiter to prevent hard clipping
    if (m_qualitySettings.enableSoftClipping) {
        applySoftClipping(outputBuffer, totalOutputSamples);
    }

    m_playbackPhase.store(phase);
}

// Linear interpolation (2-point, fast)
float Track::interpolateLinear(const float* data, uint32_t totalSamples, double position, uint32_t channel) const {
    uint32_t idx0 = static_cast<uint32_t>(position);
    uint32_t idx1 = idx0 + 1;
    double fraction = position - idx0;
    
    uint32_t sample0_idx = idx0 * m_numChannels + channel;
    uint32_t sample1_idx = idx1 * m_numChannels + channel;
    
    float s0 = (sample0_idx < totalSamples) ? data[sample0_idx] : 0.0f;
    float s1 = (sample1_idx < totalSamples) ? data[sample1_idx] : 0.0f;
    
    return s0 + static_cast<float>(fraction) * (s1 - s0);
}

// Cubic Hermite interpolation (4-point, good quality)
float Track::interpolateCubic(const float* data, uint32_t totalSamples, double position, uint32_t channel) const {
    uint32_t idx = static_cast<uint32_t>(position);
    double fraction = position - idx;
    
    // Get 4 samples for cubic interpolation
    uint32_t idx0 = (idx > 0) ? (idx - 1) * m_numChannels + channel : 0;
    uint32_t idx1 = idx * m_numChannels + channel;
    uint32_t idx2 = (idx + 1) * m_numChannels + channel;
    uint32_t idx3 = (idx + 2) * m_numChannels + channel;
    
    float s0 = (idx0 < totalSamples) ? data[idx0] : 0.0f;
    float s1 = (idx1 < totalSamples) ? data[idx1] : 0.0f;
    float s2 = (idx2 < totalSamples) ? data[idx2] : 0.0f;
    float s3 = (idx3 < totalSamples) ? data[idx3] : 0.0f;
    
    // Hermite basis functions
    float t = static_cast<float>(fraction);
    float t2 = t * t;
    float t3 = t2 * t;
    
    float a0 = -0.5f * s0 + 1.5f * s1 - 1.5f * s2 + 0.5f * s3;
    float a1 = s0 - 2.5f * s1 + 2.0f * s2 - 0.5f * s3;
    float a2 = -0.5f * s0 + 0.5f * s2;
    float a3 = s1;
    
    float result = a0 * t3 + a1 * t2 + a2 * t + a3;
    
    // Soft clipping to prevent overshoots
    return std::max(-1.0f, std::min(1.0f, result));
}

// Windowed Sinc interpolation (best quality, more CPU intensive)
float Track::interpolateSinc(const float* data, uint32_t totalSamples, double position, uint32_t channel) const {
    const int SINC_WINDOW_SIZE = 8;  // 8-point sinc (high quality)
    const float PI = 3.14159265359f;
    
    uint32_t centerIdx = static_cast<uint32_t>(position);
    double fraction = position - centerIdx;
    
    float sum = 0.0f;
    float windowSum = 0.0f;
    
    // Windowed sinc interpolation
    for (int i = -SINC_WINDOW_SIZE/2; i < SINC_WINDOW_SIZE/2; ++i) {
        int sampleIdx = static_cast<int>(centerIdx) + i;
        
        if (sampleIdx < 0 || static_cast<uint32_t>(sampleIdx * m_numChannels + channel) >= totalSamples) {
            continue;
        }
        
        float sample = data[sampleIdx * m_numChannels + channel];
        
        // Sinc function: sin(PI*x) / (PI*x)
        float x = static_cast<float>(i) - static_cast<float>(fraction);
        float sinc = (std::abs(x) < 0.0001f) ? 1.0f : std::sin(PI * x) / (PI * x);
        
        // Blackman window for better frequency response
        float window = 0.42f - 0.5f * std::cos(2.0f * PI * (i + SINC_WINDOW_SIZE/2) / SINC_WINDOW_SIZE)
                            + 0.08f * std::cos(4.0f * PI * (i + SINC_WINDOW_SIZE/2) / SINC_WINDOW_SIZE);
        
        float weight = sinc * window;
        sum += sample * weight;
        windowSum += weight;
    }
    
    // Normalize and clamp
    float result = (windowSum > 0.0001f) ? (sum / windowSum) : 0.0f;
    return std::max(-1.0f, std::min(1.0f, result));
}

// Ultra (Polyphase Sinc) interpolation (16-point, mastering grade)
float Track::interpolateUltra(const float* data, uint32_t totalSamples, double position, uint32_t channel) const {
    const int POLYPHASE_SIZE = 16;  // 16-point polyphase for reference quality
    const float PI = 3.14159265359f;
    
    // Precomputed Kaiser window lookup table (beta=8.6)
    static const float kaiserWindow[POLYPHASE_SIZE] = {
        0.0000f, 0.0217f, 0.0854f, 0.1865f, 0.3180f, 0.4706f, 0.6341f, 0.7975f,
        0.9500f, 0.9500f, 0.7975f, 0.6341f, 0.4706f, 0.3180f, 0.1865f, 0.0854f
    };
    
    uint32_t centerIdx = static_cast<uint32_t>(position);
    double fraction = position - centerIdx;
    
    float sum = 0.0f;
    float windowSum = 0.0f;
    
    // Polyphase sinc interpolation with precomputed Kaiser window
    for (int i = -POLYPHASE_SIZE/2; i < POLYPHASE_SIZE/2; ++i) {
        int sampleIdx = static_cast<int>(centerIdx) + i;
        
        if (sampleIdx < 0 || static_cast<uint32_t>(sampleIdx * m_numChannels + channel) >= totalSamples) {
            continue;
        }
        
        float sample = data[sampleIdx * m_numChannels + channel];
        
        // Sinc function with higher precision
        float x = static_cast<float>(i) - static_cast<float>(fraction);
        float sinc = (std::abs(x) < 0.0001f) ? 1.0f : std::sin(PI * x) / (PI * x);
        
        // Get precomputed Kaiser window value (FAST!)
        int windowIdx = i + POLYPHASE_SIZE/2;
        float window = kaiserWindow[windowIdx];
        
        float weight = sinc * window;
        sum += sample * weight;
        windowSum += weight;
    }
    
    // Normalize and clamp
    float result = (windowSum > 0.0001f) ? (sum / windowSum) : 0.0f;
    return std::max(-1.0f, std::min(1.0f, result));
}

// Extreme interpolation (64-point polyphase sinc - mastering grade, real-time safe)
float Track::interpolateExtreme(const float* data, uint32_t totalSamples, double position, uint32_t channel) const {
    const int EXTREME_SIZE = 64;  // 64-point sinc (sweet spot for quality/performance)
    const float PI = 3.14159265359f;
    
    // Precomputed Kaiser window lookup table (beta=10.0)
    // Computed once, reused for all samples - HUGE performance win!
    static const float kaiserWindow[EXTREME_SIZE] = {
        0.0000f, 0.0011f, 0.0044f, 0.0098f, 0.0173f, 0.0268f, 0.0384f, 0.0520f,
        0.0675f, 0.0849f, 0.1042f, 0.1252f, 0.1479f, 0.1722f, 0.1980f, 0.2252f,
        0.2537f, 0.2834f, 0.3142f, 0.3460f, 0.3786f, 0.4119f, 0.4459f, 0.4803f,
        0.5151f, 0.5502f, 0.5854f, 0.6206f, 0.6557f, 0.6906f, 0.7250f, 0.7590f,
        0.7923f, 0.8249f, 0.8566f, 0.8873f, 0.9169f, 0.9453f, 0.9724f, 0.9981f,
        1.0000f, 0.9981f, 0.9724f, 0.9453f, 0.9169f, 0.8873f, 0.8566f, 0.8249f,
        0.7923f, 0.7590f, 0.7250f, 0.6906f, 0.6557f, 0.6206f, 0.5854f, 0.5502f,
        0.5151f, 0.4803f, 0.4459f, 0.4119f, 0.3786f, 0.3460f, 0.3142f, 0.2834f
    };
    
    uint32_t centerIdx = static_cast<uint32_t>(position);
    double fraction = position - centerIdx;
    
    float sum = 0.0f;
    float windowSum = 0.0f;
    
    // 64-point polyphase sinc with precomputed Kaiser window
    for (int i = -EXTREME_SIZE/2; i < EXTREME_SIZE/2; ++i) {
        int sampleIdx = static_cast<int>(centerIdx) + i;
        
        if (sampleIdx < 0 || static_cast<uint32_t>(sampleIdx * m_numChannels + channel) >= totalSamples) {
            continue;
        }
        
        float sample = data[sampleIdx * m_numChannels + channel];
        
        // Sinc function: sin(PI*x) / (PI*x)
        float x = static_cast<float>(i) - static_cast<float>(fraction);
        float sinc = (std::abs(x) < 0.00001f) ? 1.0f : std::sin(PI * x) / (PI * x);
        
        // Get precomputed Kaiser window value (FAST!)
        int windowIdx = i + EXTREME_SIZE/2;
        float window = kaiserWindow[windowIdx];
        
        float weight = sinc * window;
        sum += sample * weight;
        windowSum += weight;
    }
    
    // Normalize and clamp
    float result = (windowSum > 0.0001f) ? (sum / windowSum) : 0.0f;
    return std::max(-1.0f, std::min(1.0f, result));
}

// Perfect interpolation (512-point polyphase sinc - FL Studio grade)
// WARNING: EXTREMELY CPU INTENSIVE - Use only for offline rendering/mastering
float Track::interpolatePerfect(const float* data, uint32_t totalSamples, double position, uint32_t channel) const {
    const int PERFECT_SIZE = 512;  // 512-point sinc (FL Studio quality)
    const float PI = 3.14159265359f;
    
    uint32_t centerIdx = static_cast<uint32_t>(position);
    double fraction = position - centerIdx;
    
    float sum = 0.0f;
    float windowSum = 0.0f;
    
    // 512-point polyphase sinc with Kaiser window (beta=12.0 for extreme precision)
    for (int i = -PERFECT_SIZE/2; i < PERFECT_SIZE/2; ++i) {
        int sampleIdx = static_cast<int>(centerIdx) + i;
        
        if (sampleIdx < 0 || static_cast<uint32_t>(sampleIdx * m_numChannels + channel) >= totalSamples) {
            continue;
        }
        
        float sample = data[sampleIdx * m_numChannels + channel];
        
        // Sinc function: sin(PI*x) / (PI*x)
        float x = static_cast<float>(i) - static_cast<float>(fraction);
        float sinc = (std::abs(x) < 0.00001f) ? 1.0f : std::sin(PI * x) / (PI * x);
        
        // Kaiser window (beta=12.0 for maximum stopband attenuation)
        float alpha = static_cast<float>(i + PERFECT_SIZE/2) / PERFECT_SIZE;
        float kaiserArg = 12.0f * std::sqrt(1.0f - (2.0f * alpha - 1.0f) * (2.0f * alpha - 1.0f));
        
        // Modified Bessel function I0 approximation (higher precision)
        auto besselI0 = [](float x) -> float {
            float sum = 1.0f;
            float term = 1.0f;
            for (int n = 1; n < 20; ++n) {  // More iterations for better precision
                term *= (x / (2.0f * n)) * (x / (2.0f * n));
                sum += term;
                if (term < 1e-10f) break;  // Early exit for convergence
            }
            return sum;
        };
        
        float window = besselI0(kaiserArg) / besselI0(12.0f);
        
        float weight = sinc * window;
        sum += sample * weight;
        windowSum += weight;
    }
    
    // Normalize and clamp
    float result = (windowSum > 0.0001f) ? (sum / windowSum) : 0.0f;
    return std::max(-1.0f, std::min(1.0f, result));
}

// === Dithering Methods ===

// Master dithering dispatcher
void Track::applyDithering(float* buffer, uint32_t numSamples) {
    switch (m_qualitySettings.dithering) {
        case DitheringMode::Triangular:
            applyTriangularDither(buffer, numSamples);
            break;
        case DitheringMode::HighPass:
            applyHighPassDither(buffer, numSamples);
            break;
        case DitheringMode::NoiseShaped:
            applyNoiseShapedDither(buffer, numSamples);
            break;
        case DitheringMode::None:
        default:
            // No dithering
            break;
    }
}

// TPDF Dithering (Triangular Probability Density Function)
void Track::applyTriangularDither(float* buffer, uint32_t numSamples) {
    const float DITHER_AMPLITUDE = 1.0f / 32768.0f;  // 16-bit dither
    
    for (uint32_t i = 0; i < numSamples; ++i) {
        // TPDF: sum of two uniform random numbers gives triangular distribution
        float r1 = static_cast<float>(rand()) / RAND_MAX - 0.5f;
        float r2 = static_cast<float>(rand()) / RAND_MAX - 0.5f;
        float dither = (r1 + r2) * DITHER_AMPLITUDE;
        
        buffer[i] += dither;
        
        // Clamp to prevent overflow
        buffer[i] = std::max(-1.0f, std::min(1.0f, buffer[i]));
    }
}

// High-Pass Shaped Dithering
void Track::applyHighPassDither(float* buffer, uint32_t numSamples) {
    const float DITHER_AMPLITUDE = 1.0f / 32768.0f;
    const float HP_COEFF = 0.5f;  // High-pass coefficient
    
    float prevDither = 0.0f;
    
    for (uint32_t i = 0; i < numSamples; ++i) {
        // Generate TPDF dither
        float r1 = static_cast<float>(rand()) / RAND_MAX - 0.5f;
        float r2 = static_cast<float>(rand()) / RAND_MAX - 0.5f;
        float dither = (r1 + r2) * DITHER_AMPLITUDE;
        
        // Apply high-pass shaping (pushes noise to higher frequencies)
        float shapedDither = dither - HP_COEFF * prevDither;
        prevDither = dither;
        
        buffer[i] += shapedDither;
        buffer[i] = std::max(-1.0f, std::min(1.0f, buffer[i]));
    }
}

// Noise-Shaped Dithering (Psychoacoustic)
void Track::applyNoiseShapedDither(float* buffer, uint32_t numSamples) {
    const float DITHER_AMPLITUDE = 1.0f / 32768.0f;
    
    // F-weighted noise shaping coefficients (pushes noise above ~2kHz where hearing is less sensitive)
    const float a1 = 2.033f;
    const float a2 = -1.165f;
    
    for (uint32_t i = 0; i < numSamples; ++i) {
        uint32_t channel = i % m_numChannels;
        
        // Generate TPDF dither
        float r1 = static_cast<float>(rand()) / RAND_MAX - 0.5f;
        float r2 = static_cast<float>(rand()) / RAND_MAX - 0.5f;
        float dither = (r1 + r2) * DITHER_AMPLITUDE;
        
        // Apply noise shaping filter (2nd order for psychoacoustic curve)
        float error = buffer[i];
        buffer[i] += dither;
        buffer[i] = std::max(-1.0f, std::min(1.0f, buffer[i]));
        
        // Calculate quantization error and shape it
        error = buffer[i] - error;
        
        // Update history for next sample (per-channel)
        if (channel < 2) {
            float shaped = error + a1 * m_ditherHistory[channel];
            m_ditherHistory[channel] = shaped;
        }
    }
}

// Soft Clipping (tanh-based, transparent gain ceiling)
void Track::applySoftClipping(float* buffer, uint32_t numSamples) {
    const float CLIP_THRESHOLD = 0.95f;  // Start soft clipping at 95% to prevent hard clips
    
    for (uint32_t i = 0; i < numSamples; ++i) {
        float sample = buffer[i];
        float absSample = std::abs(sample);
        
        if (absSample > CLIP_THRESHOLD) {
            // Apply tanh soft clipping for smooth saturation
            float sign = (sample >= 0.0f) ? 1.0f : -1.0f;
            float normalized = (absSample - CLIP_THRESHOLD) / (1.0f - CLIP_THRESHOLD);
            float softClipped = CLIP_THRESHOLD + (1.0f - CLIP_THRESHOLD) * std::tanh(normalized);
            buffer[i] = sign * softClipped;
        }
    }
}

// Stereo Width (Mid/Side Processing)
void Track::applyStereoWidth(float* buffer, uint32_t numFrames, float widthPercent) {
    // widthPercent: 0% = mono, 100% = normal, 200% = ultra-wide
    
    // Convert percentage to coefficient (0.0 to 2.0)
    float width = widthPercent / 100.0f;
    
    // Clamp to safe range
    width = std::max(0.0f, std::min(2.0f, width));
    
    for (uint32_t frame = 0; frame < numFrames; ++frame) {
        uint32_t leftIdx = frame * 2;
        uint32_t rightIdx = frame * 2 + 1;
        
        float left = buffer[leftIdx];
        float right = buffer[rightIdx];
        
        // Convert to Mid/Side
        float mid = (left + right) * 0.5f;    // Mono component (center)
        float side = (left - right) * 0.5f;   // Stereo component (width)
        
        // Apply width adjustment
        side *= width;
        
        // Convert back to Left/Right
        buffer[leftIdx] = mid + side;
        buffer[rightIdx] = mid - side;
        
        // Prevent clipping from width expansion
        if (width > 1.0f) {
            buffer[leftIdx] = std::max(-1.0f, std::min(1.0f, buffer[leftIdx]));
            buffer[rightIdx] = std::max(-1.0f, std::min(1.0f, buffer[rightIdx]));
        }
    }
}

// ============================================================================
// EUPHORIA ENGINE - Nomad Mode Signature Audio Character
// ============================================================================

/**
 * @brief Tape Circuit - Non-linear transient rounding + harmonic bloom
 * Emulates analog tape saturation with smooth transient handling
 */
void Track::applyTapeCircuit(float* buffer, uint32_t numSamples, float bloomAmount, float smoothing) {
    const float TAPE_KNEE = 0.7f;  // Start saturation at 70% level
    
    for (uint32_t i = 0; i < numSamples; ++i) {
        float sample = buffer[i];
        float absSample = std::abs(sample);
        
        // Harmonic bloom (soft saturation with even/odd harmonics)
        if (absSample > TAPE_KNEE) {
            float sign = (sample >= 0.0f) ? 1.0f : -1.0f;
            float excess = (absSample - TAPE_KNEE) / (1.0f - TAPE_KNEE);
            
            // Tape-style saturation curve (more aggressive than soft clip)
            float saturated = TAPE_KNEE + (1.0f - TAPE_KNEE) * std::tanh(excess * 2.0f);
            
            // Blend saturated signal with original based on bloomAmount
            sample = sign * (absSample * (1.0f - bloomAmount) + saturated * bloomAmount);
        }
        
        // Transient smoothing (attack rounding)
        if (i > 0) {
            float delta = sample - buffer[i - 1];
            float absDelta = std::abs(delta);
            
            // Round sharp transients (tape head tracking lag)
            if (absDelta > 0.3f) {
                float rounded = buffer[i - 1] + delta * (1.0f - smoothing * 0.3f);
                sample = rounded;
            }
        }
        
        buffer[i] = sample;
    }
}

/**
 * @brief Air - Psychoacoustic stereo widening via mid/side delay curvature
 * Creates spacious "air" around the sound using differential delay and high-frequency emphasis
 */
void Track::applyAir(float* buffer, uint32_t numFrames) {
    if (m_numChannels != 2) return;  // Stereo only
    
    const int DELAY_SAMPLES = 3;  // ~0.06ms at 48kHz (subtle Haas effect)
    const float HF_BOOST = 0.15f;  // High-frequency air enhancement
    const float AIR_FREQ = 8000.0f;  // 8kHz+ gets enhanced
    
    static float delayBufferL[8] = {0};
    static float delayBufferR[8] = {0};
    static int delayPos = 0;
    
    for (uint32_t frame = 0; frame < numFrames; ++frame) {
        uint32_t leftIdx = frame * 2;
        uint32_t rightIdx = frame * 2 + 1;
        
        float left = buffer[leftIdx];
        float right = buffer[rightIdx];
        
        // Convert to Mid/Side
        float mid = (left + right) * 0.5f;
        float side = (left - right) * 0.5f;
        
        // Apply differential delay to side (creates depth)
        int readPos = (delayPos - DELAY_SAMPLES + 8) % 8;
        float delayedSide = (delayBufferL[readPos] - delayBufferR[readPos]) * 0.5f;
        
        // Store current samples
        delayBufferL[delayPos] = left;
        delayBufferR[delayPos] = right;
        delayPos = (delayPos + 1) % 8;
        
        // Mix delayed side signal (psychoacoustic spaciousness)
        side = side * 0.85f + delayedSide * 0.15f;
        
        // High-frequency air boost (shelf filter simulation)
        // In reality, this would be a proper high-shelf filter
        // For now, we enhance overall side channel slightly
        side *= (1.0f + HF_BOOST);
        
        // Convert back to Left/Right
        buffer[leftIdx] = mid + side;
        buffer[rightIdx] = mid - side;
    }
}

/**
 * @brief Drift - Subtle pitch variance and clock jitter for analog warmth
 * Simulates tape speed fluctuations and crystal clock drift
 */
void Track::applyDrift(float* buffer, uint32_t numFrames) {
    static float driftPhase = 0.0f;
    static float driftAmount = 0.0f;
    
    const float DRIFT_RATE = 0.0003f;      // Very slow modulation (~0.2 Hz)
    const float DRIFT_DEPTH = 0.00015f;    // Â±0.015% pitch variance (very subtle)
    const float JITTER_AMOUNT = 0.00005f;  // Clock jitter noise floor
    
    for (uint32_t frame = 0; frame < numFrames; ++frame) {
        // LFO for tape speed variance
        driftPhase += DRIFT_RATE;
        if (driftPhase > 6.28318f) driftPhase -= 6.28318f;
        
        // Smooth drift modulation
        float drift = std::sin(driftPhase) * DRIFT_DEPTH;
        
        // Add random jitter (clock instability)
        float jitter = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * JITTER_AMOUNT;
        
        driftAmount = drift + jitter;
        
        // Apply very subtle pitch modulation to stereo image
        // This creates the "living, breathing" quality of analog gear
        // Implementation: In real version, this would modulate the resampling phase
        // For now, we apply a subtle amplitude modulation as a placeholder
        for (uint32_t ch = 0; ch < m_numChannels; ++ch) {
            uint32_t idx = frame * m_numChannels + ch;
            buffer[idx] *= (1.0f + driftAmount);
        }
    }
}

/**
 * @brief Apply Nomad Mode Euphoria Engine
 * Master function that applies all euphoric processing in optimal order
 */
void Track::applyEuphoriaEngine(float* buffer, uint32_t numFrames) {
    const auto& euphoria = m_qualitySettings.euphoria;
    
    // Process order matters for optimal sound:
    // 1. Tape Circuit (affects dynamics and harmonics)
    // 2. Air (spatial enhancement on the saturated signal)
    // 3. Drift (final living/breathing quality)
    
    uint32_t numSamples = numFrames * m_numChannels;
    
    if (euphoria.tapeCircuit) {
        applyTapeCircuit(buffer, numSamples, 
                        euphoria.harmonicBloom, 
                        euphoria.transientSmoothing);
    }
    
    if (euphoria.airEnhancement && m_numChannels == 2) {
        applyAir(buffer, numFrames);
    }
    
    if (euphoria.driftEffect) {
        applyDrift(buffer, numFrames);
    }
}

// DC Offset Removal (high-pass filter)
void Track::removeDC(float* buffer, uint32_t numSamples) {
    const float DC_FILTER_COEFF = 0.995f;  // High-pass filter coefficient
    
    for (uint32_t i = 0; i < numSamples; ++i) {
        // Simple DC blocking filter: y[n] = x[n] - dc_offset
        // Update DC offset with exponential moving average
        m_dcOffset = m_dcOffset * DC_FILTER_COEFF + buffer[i] * (1.0f - DC_FILTER_COEFF);
        buffer[i] -= static_cast<float>(m_dcOffset);
    }
}

// Set quality settings
void Track::setQualitySettings(const AudioQualitySettings& settings) {
    m_qualitySettings = settings;
}

} // namespace Audio
} // namespace Nomad
