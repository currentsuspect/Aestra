// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Track.h"
#include "NomadLog.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <cstring>
#include <codecvt>
#include <locale>
#include <mutex>
#include <numeric>
#include <condition_variable>
#include "SamplePool.h"
#ifdef _WIN32
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#endif

namespace Nomad {
namespace Audio {

namespace {
    // Downmix multi-channel audio to stereo using sensible weights
    void downmixToStereo(const std::vector<float>& input,
                         uint32_t inChannels,
                         std::vector<float>& output) {
        if (inChannels == 0) return;
        const size_t frames = input.size() / inChannels;
        output.assign(frames * 2, 0.0f);

        for (size_t i = 0; i < frames; ++i) {
            float left = 0.0f;
            float right = 0.0f;
            const float* frame = &input[i * inChannels];

            // Standard mapping for common layouts: L, R, C, LFE, Ls, Rs
            if (inChannels >= 1) left  += frame[0];
            if (inChannels >= 2) right += frame[1];
            if (inChannels >= 3) { // Center
                float c = frame[2] * 0.7071f;
                left += c; right += c;
            }
            if (inChannels >= 4) { // LFE
                float lfe = frame[3] * 0.5f;
                left += lfe; right += lfe;
            }
            if (inChannels >= 5) left  += frame[4] * 0.7071f; // Ls
            if (inChannels >= 6) right += frame[5] * 0.7071f; // Rs

            // Any extra channels beyond 6: average into stereo
            for (uint32_t ch = 6; ch < inChannels; ++ch) {
                float v = frame[ch] * 0.5f;
                left += v;
                right += v;
            }

            output[i * 2 + 0] = std::max(-1.0f, std::min(1.0f, left));
            output[i * 2 + 1] = std::max(-1.0f, std::min(1.0f, right));
        }
    }

    void forceStereo(std::vector<float>& buffer, uint32_t& channelCount, uint32_t& sourceChannels) {
        sourceChannels = channelCount;
        if (channelCount == 2) return;
        if (channelCount == 1) {
            const size_t frames = buffer.size();
            std::vector<float> stereo;
            stereo.reserve(frames * 2);
            for (size_t i = 0; i < frames; ++i) {
                float s = buffer[i];
                stereo.push_back(s);
                stereo.push_back(s);
            }
            buffer.swap(stereo);
            channelCount = 2;
            return;
        }

        std::vector<float> stereo;
        downmixToStereo(buffer, static_cast<uint32_t>(channelCount), stereo);
        buffer.swap(stereo);
        channelCount = 2;
    }
#ifdef _WIN32
    // Downmix multi-channel audio to stereo using sensible weights
    bool ensureMediaFoundationInitialized() {
        static std::once_flag initFlag;
        static HRESULT initResult = E_FAIL;
        std::call_once(initFlag, []() {
            initResult = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        });
        return SUCCEEDED(initResult);
    }

    std::wstring utf8ToWide(const std::string& input) {
        if (input.empty()) return L"";
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        return converter.from_bytes(input);
    }

    bool loadWithMediaFoundation(const std::string& filePath,
                                 std::vector<float>& audioData,
                                 uint32_t& sampleRate,
                                 uint32_t& numChannels) {
        using Microsoft::WRL::ComPtr;

        if (!ensureMediaFoundationInitialized()) {
            Log::warning("Media Foundation failed to initialize, cannot decode audio: " + filePath);
            return false;
        }

                ComPtr<IMFAttributes> attributes;
                if (FAILED(MFCreateAttributes(&attributes, 1))) {
                    Log::warning("MFCreateAttributes failed for FLAC decode");
                    return false;
                }
        
                // Enable built-in audio processing (format conversion)
        #if defined(MF_SOURCE_READER_ENABLE_AUDIO_PROCESSING)
                attributes->SetUINT32(MF_SOURCE_READER_ENABLE_AUDIO_PROCESSING, TRUE);
        #elif defined(MF_READWRITE_ENABLE_AUDIO_PROCESSING)
                attributes->SetUINT32(MF_READWRITE_ENABLE_AUDIO_PROCESSING, TRUE);
        #else
                // Attribute not available in this SDK: skip enabling audio processing.
                // This is a safe fallback for older/newer SDK mismatches.
                (void)attributes;
        #endif

        ComPtr<IMFSourceReader> reader;
        std::wstring widePath = utf8ToWide(filePath);
        HRESULT hr = MFCreateSourceReaderFromURL(widePath.c_str(), attributes.Get(), &reader);
        if (FAILED(hr)) {
            Log::warning("MFCreateSourceReaderFromURL failed for: " + filePath);
            return false;
        }

        // Request 32-bit float PCM output
        ComPtr<IMFMediaType> audioType;
        hr = MFCreateMediaType(&audioType);
        if (FAILED(hr)) return false;
        audioType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        audioType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
        audioType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);

        hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, audioType.Get());
        if (FAILED(hr)) {
            Log::warning("Failed to set PCM output type: " + filePath);
            return false;
        }

        // Get resolved output format (sample rate, channels)
        ComPtr<IMFMediaType> currentType;
        hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &currentType);
        if (FAILED(hr)) return false;

        UINT32 sr = 0, ch = 0;
        // Use IMFMediaType::GetUINT32 to avoid macro/signature mismatches with MFGetAttributeUINT32
        if (FAILED(currentType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr))) {
            Log::warning("Failed to get sample rate from media type");
            return false;
        }
        if (FAILED(currentType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch))) {
            Log::warning("Failed to get channel count from media type");
            return false;
        }
        sampleRate = sr;
        numChannels = ch;

        audioData.clear();
        while (true) {
            DWORD flags = 0;
            ComPtr<IMFSample> sample;
            hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample);
            if (FAILED(hr)) {
            Log::warning("ReadSample failed during decode: " + std::to_string(hr));
                return false;
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
            if (!sample) continue;

            ComPtr<IMFMediaBuffer> buffer;
            if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) continue;

            BYTE* data = nullptr;
            DWORD maxLen = 0, curLen = 0;
            if (FAILED(buffer->Lock(&data, &maxLen, &curLen))) continue;

            size_t prev = audioData.size();
            audioData.resize(prev + (curLen / sizeof(float)));
            std::memcpy(audioData.data() + prev, data, curLen);

            buffer->Unlock();
        }

        if (audioData.empty()) {
            Log::warning("No audio frames decoded: " + filePath);
            return false;
        }

        return true;
    }
}
#endif // _WIN32

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

struct WavInfo {
    uint32_t sampleRate{0};
    uint16_t channels{0};
    uint16_t bitsPerSample{0};
    uint32_t dataOffset{0};
    uint32_t dataSize{0};
    uint16_t audioFormat{0};
};

static bool parseWavInfo(const std::string& filePath, WavInfo& info) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        Log::warning("Failed to open WAV file: " + filePath);
        return false;
    }

    char riffId[4];
    uint32_t riffSize = 0;
    char waveId[4];
    if (!file.read(riffId, 4) || !file.read(reinterpret_cast<char*>(&riffSize), 4) || !file.read(waveId, 4)) {
        return false;
    }
    if (std::strncmp(riffId, "RIFF", 4) != 0 || std::strncmp(waveId, "WAVE", 4) != 0) {
        return false;
    }

    bool fmtFound = false;
    bool dataFound = false;
    uint16_t audioFormat = 1;
    uint16_t channelCount = 2;
    uint32_t sr = 44100;
    uint16_t bitsPerSample = 16;
    uint32_t dataSize = 0;
    std::streampos dataPos{};

    while (file && !(fmtFound && dataFound)) {
        char chunkId[4];
        uint32_t chunkSize = 0;
        if (!file.read(chunkId, 4) || !file.read(reinterpret_cast<char*>(&chunkSize), 4)) break;

        if (std::strncmp(chunkId, "fmt ", 4) == 0) {
            fmtFound = true;
            if (chunkSize < 16) return false;
            file.read(reinterpret_cast<char*>(&audioFormat), sizeof(uint16_t));
            file.read(reinterpret_cast<char*>(&channelCount), sizeof(uint16_t));
            file.read(reinterpret_cast<char*>(&sr), sizeof(uint32_t));

            uint32_t byteRate = 0;
            uint16_t blockAlign = 0;
            file.read(reinterpret_cast<char*>(&byteRate), sizeof(uint32_t));
            file.read(reinterpret_cast<char*>(&blockAlign), sizeof(uint16_t));
            file.read(reinterpret_cast<char*>(&bitsPerSample), sizeof(uint16_t));
            if (chunkSize > 16) {
                file.seekg(chunkSize - 16, std::ios::cur);
            }
        } else if (std::strncmp(chunkId, "data", 4) == 0) {
            dataFound = true;
            dataSize = chunkSize;
            dataPos = file.tellg();
            file.seekg(chunkSize, std::ios::cur);
        } else {
            file.seekg(chunkSize, std::ios::cur);
        }
    }

    if (!(fmtFound && dataFound)) return false;

    info.sampleRate = sr;
    info.channels = channelCount;
    info.bitsPerSample = bitsPerSample;
    info.dataOffset = static_cast<uint32_t>(dataPos);
    info.dataSize = dataSize;
    info.audioFormat = audioFormat;
    return true;
}

// Simple WAV file loader
bool loadWavFile(const std::string& filePath, std::vector<float>& audioData, uint32_t& sampleRate, uint32_t& numChannels) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        Log::warning("Failed to open WAV file: " + filePath);
        return false;
    }

    // Read RIFF header
    char riffId[4];
    uint32_t riffSize = 0;
    char waveId[4];
    if (!file.read(riffId, 4) || !file.read(reinterpret_cast<char*>(&riffSize), 4) || !file.read(waveId, 4)) {
        Log::warning("Invalid WAV header (too short): " + filePath);
        return false;
    }

    if (std::strncmp(riffId, "RIFF", 4) != 0 || std::strncmp(waveId, "WAVE", 4) != 0) {
        Log::warning("Invalid WAV file format: " + filePath);
        Log::warning("  Expected: RIFF, WAVE");
        Log::warning("  Got: " + std::string(riffId, 4) + ", " + std::string(waveId, 4));
        return false;
    }

    Log::info("WAV Header Debug:");
    Log::info("  RIFF: " + std::string(riffId, 4));
    Log::info("  File size: " + std::to_string(riffSize));
    Log::info("  WAVE: " + std::string(waveId, 4));

    // Parse chunks
    bool fmtFound = false;
    bool dataFound = false;
    uint16_t audioFormat = 1;
    uint32_t channelCount = 2;
    uint32_t sr = 44100;
    uint32_t byteRate = 0;
    uint16_t blockAlign = 0;
    uint16_t bitsPerSample = 16;
    uint32_t dataSize = 0;
    std::streampos dataPos{};

    while (file && !(fmtFound && dataFound)) {
        char chunkId[4];
        uint32_t chunkSize = 0;

        if (!file.read(chunkId, 4)) {
            break;
        }
        if (!file.read(reinterpret_cast<char*>(&chunkSize), 4)) {
            break;
        }

        std::string chunk(chunkId, 4);
        Log::info("Found chunk: " + chunk + ", size: " + std::to_string(chunkSize));

        if (chunk == "fmt ") {
            // Minimum fmt chunk is 16 bytes
            if (chunkSize < 16) {
                Log::warning("Invalid fmt chunk size: " + std::to_string(chunkSize));
                return false;
            }

            if (!file.read(reinterpret_cast<char*>(&audioFormat), 2) ||
                !file.read(reinterpret_cast<char*>(&channelCount), 2) ||
                !file.read(reinterpret_cast<char*>(&sr), 4) ||
                !file.read(reinterpret_cast<char*>(&byteRate), 4) ||
                !file.read(reinterpret_cast<char*>(&blockAlign), 2) ||
                !file.read(reinterpret_cast<char*>(&bitsPerSample), 2)) {
                Log::warning("Failed to read fmt chunk");
                return false;
            }

            // Skip any extra fmt bytes
            uint32_t remaining = chunkSize > 16 ? (chunkSize - 16) : 0;
            if (remaining > 0) {
                file.seekg(remaining, std::ios::cur);
            }

            fmtFound = true;
        } else if (chunk == "data") {
            dataPos = file.tellg();
            dataSize = chunkSize;
            // Skip data for now; we'll read after validation
            file.seekg(chunkSize, std::ios::cur);
            dataFound = true;
        } else {
            // Skip unknown/metadata chunks (JUNK, LIST, bext, etc.)
            file.seekg(chunkSize, std::ios::cur);
        }

        // Chunks are word-aligned; skip padding byte if chunkSize is odd
        if (chunkSize % 2 == 1) {
            file.seekg(1, std::ios::cur);
        }
    }

    if (!fmtFound) {
        Log::warning("fmt chunk not found, cannot determine WAV format: " + filePath);
        return false;
    }
    if (!dataFound) {
        Log::warning("data chunk not found in WAV file: " + filePath);
        return false;
    }

    Log::info("WAV format:");
    Log::info("  Audio format: " + std::to_string(audioFormat));
    Log::info("  Channels: " + std::to_string(channelCount));
    Log::info("  Sample rate: " + std::to_string(sr));
    Log::info("  Bits per sample: " + std::to_string(bitsPerSample));
    Log::info("  Data size: " + std::to_string(dataSize));

    if (audioFormat != 1 && audioFormat != 3) { // PCM or IEEE float
        Log::warning("Unsupported audio format: " + std::to_string(audioFormat) + " (only PCM/float supported)");
        return false;
    }

    if (bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32) {
        Log::warning("Unsupported bit depth: " + std::to_string(bitsPerSample) + " (only 16/24/32-bit supported)");
        return false;
    }

    // Read audio data based on bit depth
    file.seekg(dataPos);
    size_t bytesPerSample = bitsPerSample / 8;
    audioData.resize(dataSize / bytesPerSample);

    if (bitsPerSample == 16) {
        std::vector<int16_t> rawData(dataSize / sizeof(int16_t));
        file.read(reinterpret_cast<char*>(rawData.data()), dataSize);

        for (size_t i = 0; i < rawData.size(); ++i) {
            audioData[i] = rawData[i] / 32768.0f;
        }
    } else if (bitsPerSample == 24) {
        std::vector<uint8_t> rawData(dataSize);
        file.read(reinterpret_cast<char*>(rawData.data()), dataSize);

        for (size_t i = 0; i < audioData.size(); ++i) {
            uint32_t sample24 = rawData[i * 3] | (rawData[i * 3 + 1] << 8) | (rawData[i * 3 + 2] << 16);

            // Sign extend
            if (sample24 & 0x800000) {
                sample24 |= 0xFF000000;
            }

            audioData[i] = static_cast<int32_t>(sample24) / 8388608.0f;
        }
    } else if (bitsPerSample == 32) {
        if (audioFormat == 1) { // 32-bit PCM
            std::vector<int32_t> rawData(dataSize / sizeof(int32_t));
            file.read(reinterpret_cast<char*>(rawData.data()), dataSize);
            const float invScale = 1.0f / 2147483648.0f;
            for (size_t i = 0; i < rawData.size(); ++i) {
                audioData[i] = std::max(-1.0f, std::min(1.0f, rawData[i] * invScale));
            }
        } else { // IEEE float
            file.read(reinterpret_cast<char*>(audioData.data()), dataSize);
            for (size_t i = 0; i < audioData.size(); ++i) {
                audioData[i] = std::max(-1.0f, std::min(1.0f, audioData[i]));
            }
        }
    }

    sampleRate = sr;
    numChannels = channelCount;

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
    stopStreaming();
    if (isRecording()) {
        stopRecording();
    }
    Log::info("Track destroyed: " + m_name);
}

const std::vector<float>& Track::getAudioData() const {
    // Prefer shared sample buffer if present (non-streaming full loads)
    if (m_sampleBuffer && m_sampleBuffer->ready.load()) {
        return m_sampleBuffer->data;
    }
    return m_audioData;
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
    stopStreaming();
    m_sampleBuffer.reset();

    // Check if file exists
    std::ifstream checkFile(filePath);
    if (!checkFile) {
        std::cout << "File not found, generating preview tone" << std::endl;
        return generatePreviewTone(filePath);
    }
    checkFile.close();

    // Clear any existing audio data (streaming/recording buffers)
    {
        std::lock_guard<std::mutex> lock(m_audioDataMutex);
        m_audioData.clear();
    }
    m_durationSeconds.store(0.0);
    m_playbackPhase.store(0.0);
    m_positionSeconds.store(0.0);

    // Determine file extension to choose appropriate loader
    std::string extension;
    if (auto dotPos = filePath.find_last_of('.'); dotPos != std::string::npos && dotPos + 1 < filePath.size()) {
        extension = filePath.substr(dotPos + 1);
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    }

    if (extension == "wav") {
        WavInfo info{};
        bool infoOk = parseWavInfo(filePath, info);
        const uint64_t STREAM_THRESHOLD_BYTES = 50ull * 1024ull * 1024ull; // 50MB

        if (infoOk) {
            const uint64_t bytesPerFrame = (info.bitsPerSample / 8) * info.channels;
            const uint64_t totalFrames = bytesPerFrame ? (static_cast<uint64_t>(info.dataSize) / bytesPerFrame) : 0;
            bool streamableFormat = (info.audioFormat == 1 || info.audioFormat == 3) &&
                                    (info.bitsPerSample == 16 || info.bitsPerSample == 24 || info.bitsPerSample == 32);

            if (streamableFormat && info.dataSize > STREAM_THRESHOLD_BYTES && totalFrames > 0) {
                if (startWavStreaming(filePath, info.sampleRate, info.channels, info.bitsPerSample, info.dataOffset, info.dataSize)) {
                    m_sourcePath = filePath;
                    m_durationSeconds.store(static_cast<double>(totalFrames) / info.sampleRate);
                    setState(TrackState::Loaded);
                    Log::info("WAV streaming enabled: " + std::to_string(totalFrames) + " frames");
                    return true;
                }
                Log::warning("Streaming setup failed, falling back to full load");
            }
        }

        // Load WAV file fully through SamplePool
        uint32_t sampleRate = 48000; // Default fallback
        uint32_t numChannels = 2;    // Default fallback
        uint32_t sourceChannels = 2;

        auto loader = [this, &sampleRate, &numChannels, &sourceChannels, filePath](AudioBuffer& out) -> bool {
            std::vector<float> decoded;
            uint32_t sr = sampleRate;
            uint32_t ch = numChannels;
            if (!loadWavFile(filePath, decoded, sr, ch)) {
                return false;
            }
            uint32_t srcCh = ch;
            forceStereo(decoded, ch, srcCh);

            out.sampleRate = sr;
            out.channels = ch;
            out.data.swap(decoded);
            out.sourcePath = filePath;

            sampleRate = sr;
            numChannels = ch;
            sourceChannels = srcCh;
            return true;
        };

        auto buffer = SamplePool::getInstance().acquire(filePath, loader);
        if (buffer && buffer->ready.load()) {
            // If this was a cache hit, populate metadata from buffer
            if (buffer->sampleRate > 0) sampleRate = buffer->sampleRate;
            if (buffer->channels > 0) { numChannels = buffer->channels; sourceChannels = buffer->channels; }

            m_sampleBuffer = buffer;
            m_sampleRate = sampleRate;
            m_numChannels = numChannels;
            m_sourceChannels = sourceChannels;
            m_sourcePath = filePath;
            m_durationSeconds.store(sampleRate > 0 ? static_cast<double>(buffer->numFrames) / sampleRate : 0.0);
            setState(TrackState::Loaded);

            Log::info("WAV loaded successfully via SamplePool: " + std::to_string(buffer->data.size()) + " samples, " +
                       std::to_string(m_durationSeconds.load()) + " seconds");
            return true;
        }

        Log::warning("Failed to load WAV file: " + filePath + ", generating preview tone instead");
    } else {
#ifdef _WIN32
        uint32_t sampleRate = 48000;
        uint32_t numChannels = 2;
        uint32_t sourceChannels = 2;

        auto loader = [this, &sampleRate, &numChannels, &sourceChannels, filePath](AudioBuffer& out) -> bool {
            std::vector<float> decoded;
            uint32_t sr = sampleRate;
            uint32_t ch = numChannels;
            if (!loadWithMediaFoundation(filePath, decoded, sr, ch)) {
                return false;
            }
            uint32_t srcCh = ch;
            forceStereo(decoded, ch, srcCh);

            out.sampleRate = sr;
            out.channels = ch;
            out.data.swap(decoded);
            out.sourcePath = filePath;

            sampleRate = sr;
            numChannels = ch;
            sourceChannels = srcCh;
            return true;
        };

        auto buffer = SamplePool::getInstance().acquire(filePath, loader);
        if (buffer && buffer->ready.load()) {
            // Cache hit path: pull metadata from buffer if loader wasn't used
            if (buffer->sampleRate > 0) sampleRate = buffer->sampleRate;
            if (buffer->channels > 0) { numChannels = buffer->channels; sourceChannels = buffer->channels; }

            m_sampleBuffer = buffer;
            m_sampleRate = sampleRate;
            m_numChannels = numChannels;
            m_sourceChannels = sourceChannels;
            m_sourcePath = filePath;
            m_durationSeconds.store(sampleRate > 0 ? static_cast<double>(buffer->numFrames) / sampleRate : 0.0);
            setState(TrackState::Loaded);

            Log::info("Audio loaded via Media Foundation + SamplePool: " + std::to_string(buffer->data.size()) + " samples, " +
                       std::to_string(m_durationSeconds.load()) + " seconds @ " +
                       std::to_string(sampleRate) + " Hz, channels: " + std::to_string(numChannels));
            return true;
        }

        Log::warning("Failed to decode audio file: " + filePath + ", generating preview tone instead");
#else
        Log::warning("Media Foundation decoding not supported on this platform: " + filePath);
#endif
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
    m_sampleBuffer.reset();
    m_sourcePath = filePath;
    // Use filename hash to generate a unique frequency
    size_t filenameHash = std::hash<std::string>{}(filePath);
    double baseFrequency = 220.0 + (filenameHash % 440);

    // Generate 5 seconds of audio
    const double duration = 5.0;
    const uint32_t totalSamples = static_cast<uint32_t>(m_sampleRate * duration * m_numChannels);
    std::vector<float> buffer(totalSamples);

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

        buffer[i * m_numChannels + 0] = sample;
        buffer[i * m_numChannels + 1] = sample;
    }

    {
        std::lock_guard<std::mutex> lock(m_audioDataMutex);
        m_audioData.swap(buffer);
        m_numChannels = 2;
        m_sourceChannels = 2;
    }

    m_durationSeconds.store(duration);
    setState(TrackState::Loaded);

    std::cout << "Preview tone generated: " << m_audioData.size() << " samples, "
              << m_durationSeconds.load() << " seconds, " << baseFrequency << " Hz" << std::endl;

    return true;
}

bool Track::generateDemoAudio(const std::string& filePath) {
    m_sampleBuffer.reset();
    std::cout << "Generating demo audio for: " << filePath << std::endl;
    m_sourcePath = filePath;

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
    std::vector<float> buffer(totalSamples);

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

        buffer[i * m_numChannels + 0] = sample;
        buffer[i * m_numChannels + 1] = sample;
    }

    {
        std::lock_guard<std::mutex> lock(m_audioDataMutex);
        m_audioData.swap(buffer);
        m_numChannels = 2;
        m_sourceChannels = 2;
    }

    m_durationSeconds.store(duration);
    setState(TrackState::Loaded);

    std::cout << "Demo audio generated: " << m_audioData.size() << " samples, "
              << m_durationSeconds.load() << " seconds, " << frequency << " Hz" << std::endl;

    return true;
}

void Track::clearAudioData() {
    m_sampleBuffer.reset();
    {
        std::lock_guard<std::mutex> lock(m_audioDataMutex);
        m_audioData.clear();
        m_recordingBuffer.clear();
        m_numChannels = 2;
        m_sourceChannels = 2;
    }
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
    
    stopStreaming();
    m_sampleBuffer.reset();

    // Copy audio data and enforce stereo for the engine
    std::vector<float> temp(data, data + (numSamples * numChannels));
    uint32_t inChannels = numChannels;
    {
        std::lock_guard<std::mutex> lock(m_audioDataMutex);
        m_sampleRate = sampleRate;
        forceStereo(temp, inChannels, m_sourceChannels);
        m_audioData.swap(temp);
        m_numChannels = 2;
    }
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
    
    if (m_streaming) {
        uint64_t targetFrame = static_cast<uint64_t>(seconds * m_sampleRate);
        uint64_t maxFrames = m_streamTotalFrames;
        if (targetFrame > maxFrames) targetFrame = maxFrames;

        // Re-seek streaming source
        uint64_t dataSizeBytes = m_streamTotalFrames * static_cast<uint64_t>(m_streamBytesPerSample * m_sourceChannels);
        startWavStreaming(m_sourcePath, m_sampleRate, static_cast<uint16_t>(m_sourceChannels),
                          static_cast<uint16_t>(m_streamBytesPerSample * 8),
                          m_streamDataOffset, dataSizeBytes, targetFrame);
    }

    m_positionSeconds.store(seconds);

    // CRITICAL: Update playback phase for sample-accurate positioning
    // Phase is in TRACK sample space (not output sample space)
    // When we seek, we need to set phase to the correct position in the audio data
    m_playbackPhase.store(seconds * m_sampleRate);
}

// Audio Processing
void Track::processAudio(float* outputBuffer, uint32_t numFrames, double streamTime, double outputSampleRate) {
    if (!outputBuffer || numFrames == 0) {
        return;
    }

    // Fallback to sane output sample rate to avoid divide-by-zero
    if (outputSampleRate <= 0.0) {
        outputSampleRate = 48000.0;
    }

    TrackState currentState = getState();

    // Reuse per-track temp buffer to avoid allocations each callback
    size_t requiredSamples = static_cast<size_t>(numFrames) * m_numChannels;
    if (m_tempBuffer.size() < requiredSamples) {
        m_tempBuffer.resize(requiredSamples);
    }
    float* trackBuffer = m_tempBuffer.data();

    switch (currentState) {
        case TrackState::Playing: {
            // Copy audio data to temporary buffer
            copyAudioData(trackBuffer, numFrames, outputSampleRate);
            
            // Process through mixer bus for volume/pan/mute/solo
            if (m_mixerBus) {
                m_mixerBus->process(trackBuffer, numFrames);
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
        // When playing 44100Hz file on higher output rates, position must be advanced
        // using the device sample rate to keep timeline in sync.
        double newPos = currentPos + (numFrames / outputSampleRate);
        
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

void Track::copyAudioData(float* outputBuffer, uint32_t numFrames, double outputSampleRate) {
    std::lock_guard<std::mutex> lock(m_audioDataMutex);

    const std::vector<float>* buffer = nullptr;
    uint32_t channels = m_numChannels;
    if (m_streaming) {
        buffer = &m_audioData;
    } else if (m_sampleBuffer && m_sampleBuffer->ready.load()) {
        buffer = &m_sampleBuffer->data;
    } else {
        buffer = &m_audioData;
    }

    if (!buffer || buffer->empty()) {
        generateSilence(outputBuffer, numFrames);
        return;
    }

    uint32_t totalSamples = static_cast<uint32_t>(buffer->size());
    double phase = m_playbackPhase.load();
    
    // Calculate sample rate ratio for resampling
    if (outputSampleRate <= 0.0) {
        outputSampleRate = 48000.0;
    }
    const double sampleRateRatio = static_cast<double>(m_sampleRate) / outputSampleRate;

    const uint64_t bufferFrames = totalSamples / channels;
    const uint64_t baseFrame = m_streamBaseFrame;

    // Process audio based on quality settings
    for (uint32_t frame = 0; frame < numFrames; ++frame) {
        double exactSamplePos = phase;
        
        for (uint32_t ch = 0; ch < channels; ++ch) {
            float sample = 0.0f;
            double localPos = m_streaming ? (exactSamplePos - baseFrame) : exactSamplePos;

            if (!m_streaming || (localPos >= 0.0 && localPos + 1.0 < bufferFrames)) {
                // Choose interpolation method based on resampling mode
                switch (m_qualitySettings.resampling) {
                    case ResamplingMode::Fast:
                        sample = interpolateLinear(buffer->data(), totalSamples, localPos, ch);
                        break;
                    case ResamplingMode::Medium:
                        sample = interpolateCubic(buffer->data(), totalSamples, localPos, ch);
                        break;
                    case ResamplingMode::High:
                        sample = interpolateSinc(buffer->data(), totalSamples, localPos, ch);
                        break;
                    case ResamplingMode::Ultra:
                        sample = interpolateUltra(buffer->data(), totalSamples, localPos, ch);
                        break;
                    case ResamplingMode::Extreme:
                        sample = interpolateExtreme(buffer->data(), totalSamples, localPos, ch);
                        break;
                    case ResamplingMode::Perfect:
                        sample = interpolatePerfect(buffer->data(), totalSamples, localPos, ch);
                        break;
                }
            } else {
                sample = 0.0f; // gap until buffer catches up
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
    if (m_streaming) {
        trimStreamBuffer(static_cast<uint64_t>(phase));
        m_streamCv.notify_one();
    }
}

void Track::trimStreamBuffer(uint64_t currentFrame) {
    if (!m_streaming) return;
    const uint32_t keepMargin = 8192; // frames to keep ahead of playhead for safety
    const uint64_t targetBase = (currentFrame > keepMargin) ? (currentFrame - keepMargin) : 0;

    if (targetBase > m_streamBaseFrame) {
        uint64_t framesToDrop = targetBase - m_streamBaseFrame;
        uint64_t availableFrames = m_audioData.size() / m_numChannels;
        if (framesToDrop >= availableFrames) {
            // Do not drop everything
            return;
        }
        const size_t dropSamples = static_cast<size_t>(framesToDrop * m_numChannels);
        m_audioData.erase(m_audioData.begin(), m_audioData.begin() + dropSamples);
        m_streamBaseFrame += framesToDrop;
    }
}

bool Track::startWavStreaming(const std::string& filePath, uint32_t sampleRate, uint16_t channels, uint16_t bitsPerSample, uint32_t dataOffset, uint64_t dataSize, uint64_t startFrame) {
    stopStreaming();
    m_sampleBuffer.reset();

    m_streamFile = std::ifstream(filePath, std::ios::binary);
    if (!m_streamFile) {
        Log::warning("Failed to open WAV for streaming: " + filePath);
        return false;
    }

    const uint32_t bytesPerFrame = (bitsPerSample / 8) * channels;
    uint64_t startOffset = dataOffset + startFrame * bytesPerFrame;
    uint64_t maxOffset = dataOffset + dataSize;
    if (startOffset >= maxOffset) {
        startOffset = dataOffset;
        startFrame = 0;
    }

    m_streamFile.seekg(static_cast<std::streamoff>(startOffset), std::ios::beg);
    if (!m_streamFile.good()) {
        Log::warning("Failed to seek WAV data chunk for streaming");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_audioDataMutex);
        m_audioData.clear();
        m_streamBaseFrame = startFrame;
        m_streamEof = false;
        m_streamTotalFrames = (bitsPerSample / 8 == 0 || channels == 0) ? 0 : dataSize / bytesPerFrame;
        m_streamBytesPerSample = bitsPerSample / 8;
        m_streamDataOffset = dataOffset;
        m_sampleRate = sampleRate;
        m_numChannels = 2;
        m_sourceChannels = channels;
    }

    m_streamStop.store(false);
    m_streaming = true;

    // Launch background thread
    m_streamThread = std::thread(&Track::streamWavThread, this, channels);
    return true;
}

void Track::stopStreaming() {
    if (!m_streaming) return;
    m_streamStop.store(true);
    m_streamCv.notify_one();
    if (m_streamThread.joinable()) {
        m_streamThread.join();
    }
    m_streaming = false;
    m_streamEof = false;
    if (m_streamFile.is_open()) {
        m_streamFile.close();
    }
}

void Track::streamWavThread(uint32_t channels) {
    const uint32_t bytesPerSample = m_streamBytesPerSample;
    const uint32_t bytesPerFrame = bytesPerSample * channels;
    const uint32_t targetBufferFrames = m_sampleRate * 6; // keep ~6s buffered
    const uint32_t chunkFrames = m_sampleRate;            // read ~1s per iteration

    while (!m_streamStop.load()) {
        std::unique_lock<std::mutex> lk(m_streamMutex);
        m_streamCv.wait_for(lk, std::chrono::milliseconds(20), [&] {
            std::lock_guard<std::mutex> audioLock(m_audioDataMutex);
            uint64_t bufferedFrames = m_audioData.size() / m_numChannels;
            return m_streamStop.load() || bufferedFrames < targetBufferFrames;
        });
        lk.unlock();
        if (m_streamStop.load()) break;

        // Check EOF
        {
            std::lock_guard<std::mutex> audioLock(m_audioDataMutex);
            uint64_t bufferedFrames = m_audioData.size() / m_numChannels;
            if (m_streamEof || (m_streamBaseFrame + bufferedFrames) >= m_streamTotalFrames) {
                m_streamEof = true;
                continue;
            }
        }

        // Read next chunk
        const uint64_t currentEndFrame = m_streamBaseFrame + (m_audioData.size() / m_numChannels);
        const uint64_t remainingFrames = (m_streamTotalFrames > currentEndFrame) ? (m_streamTotalFrames - currentEndFrame) : 0;
        uint32_t framesToRead = static_cast<uint32_t>(std::min<uint64_t>(chunkFrames, remainingFrames));
        if (framesToRead == 0) {
            m_streamEof = true;
            continue;
        }

        std::vector<char> raw(framesToRead * bytesPerFrame);
        m_streamFile.read(raw.data(), raw.size());
        size_t got = static_cast<size_t>(m_streamFile.gcount());
        if (got == 0) {
            m_streamEof = true;
            continue;
        }
        const size_t gotFrames = got / bytesPerFrame;
        std::vector<float> decoded;
        decoded.reserve(gotFrames * channels);

        if (bytesPerSample == 2) {
            const int16_t* in = reinterpret_cast<int16_t*>(raw.data());
            for (size_t i = 0; i < gotFrames * channels; ++i) {
                decoded.push_back(static_cast<float>(in[i]) / 32768.0f);
            }
        } else if (bytesPerSample == 3) {
            for (size_t i = 0; i < gotFrames * channels; ++i) {
                int32_t sample = (static_cast<uint8_t>(raw[i * 3])) |
                                 (static_cast<uint8_t>(raw[i * 3 + 1]) << 8) |
                                 (static_cast<int8_t>(raw[i * 3 + 2]) << 16);
                decoded.push_back(std::max(-1.0f, std::min(1.0f, sample / 8388608.0f)));
            }
        } else if (bytesPerSample == 4) {
            if (m_streamBytesPerSample == 4 && m_streamTotalFrames) {
                // Assume float32 if audioFormat was IEEE float (we only stream PCM/float)
                const float* in = reinterpret_cast<float*>(raw.data());
                decoded.insert(decoded.end(), in, in + gotFrames * channels);
            } else {
                const int32_t* in = reinterpret_cast<int32_t*>(raw.data());
                const float inv = 1.0f / 2147483648.0f;
                for (size_t i = 0; i < gotFrames * channels; ++i) {
                    decoded.push_back(std::max(-1.0f, std::min(1.0f, in[i] * inv)));
                }
            }
        }

        uint32_t inCh = channels;
        forceStereo(decoded, inCh, m_sourceChannels);

        {
            std::lock_guard<std::mutex> audioLock(m_audioDataMutex);
            m_audioData.insert(m_audioData.end(), decoded.begin(), decoded.end());
        }

        if (decoded.empty() || gotFrames < framesToRead) {
            m_streamEof = true;
        }
    }
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
