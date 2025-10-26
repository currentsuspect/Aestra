#include "Track.h"
#include "NomadLog.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <cstring>

namespace Nomad {
namespace Audio {

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
                m_playbackPhase.store(0.0);
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

// Recording
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
    if (getState() == TrackState::Loaded) {
        Log::info("Playing track: " + m_name);
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
    m_positionSeconds.store(0.0);
    m_playbackPhase.store(0.0);
}

// Position Control
void Track::setPosition(double seconds) {
    // Clamp position to valid range
    double duration = getDuration();
    seconds = (seconds < 0.0) ? 0.0 : (seconds > duration) ? duration : seconds;
    m_positionSeconds.store(seconds);

    // Update playback phase for sample-accurate positioning
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
        case TrackState::Playing:
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
        double newPos = currentPos + (numFrames / static_cast<double>(m_sampleRate));
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
    
    // Calculate sample rate ratio for resampling (track sample rate / output sample rate)
    // Output sample rate is typically 48000 Hz, track might be 44100 Hz, etc.
    // For now, assume output is 48000 Hz (we should get this from the audio manager)
    const double outputSampleRate = 48000.0;
    const double sampleRateRatio = static_cast<double>(m_sampleRate) / outputSampleRate;

    for (uint32_t frame = 0; frame < numFrames; ++frame) {
        uint32_t sampleIndex = static_cast<uint32_t>(phase) * m_numChannels;

        if (sampleIndex + m_numChannels <= totalSamples) {
            outputBuffer[frame * m_numChannels + 0] = m_audioData[sampleIndex + 0];
            outputBuffer[frame * m_numChannels + 1] = m_audioData[sampleIndex + 1];
        } else {
            outputBuffer[frame * m_numChannels + 0] = 0.0f;
            outputBuffer[frame * m_numChannels + 1] = 0.0f;
        }

        phase += sampleRateRatio;
    }

    m_playbackPhase.store(phase);
}

} // namespace Audio
} // namespace Nomad
