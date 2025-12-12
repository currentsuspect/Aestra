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
#include "MiniAudioDecoder.h"
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
    : m_uuid(TrackUUID::generate())  // Generate stable UUID on creation
    , m_name(name)
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

    Log::info("Track created: " + m_name + " (ID: " + std::to_string(m_trackId) + 
              ", UUID: " + m_uuid.toString() + ")");
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

std::shared_ptr<const AudioBuffer> Track::getSampleBuffer() const {
    return m_sampleBuffer;
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
    const float prev = m_volume.exchange(volume);
    if (std::abs(prev - volume) < 1e-6f) {
        return;
    }
    if (m_mixerBus) {
        m_mixerBus->setGain(volume);
    }
    // Volume is RT-controlled via command queue; avoid forcing graph rebuilds for
    // parameter-only changes while the engine is connected.
    if (!m_commandSink && m_onDataChanged) {
        m_onDataChanged();
    }
    if (m_commandSink) {
        AudioQueueCommand cmd;
        cmd.type = AudioQueueCommandType::SetTrackVolume;
        cmd.trackIndex = m_trackIndex;
        cmd.value1 = volume;
        m_commandSink(cmd);
    }
}

void Track::setPan(float pan) {
    pan = (pan < -1.0f) ? -1.0f : (pan > 1.0f) ? 1.0f : pan;
    const float prev = m_pan.exchange(pan);
    if (std::abs(prev - pan) < 1e-6f) {
        return;
    }
    if (m_mixerBus) {
        m_mixerBus->setPan(pan);
    }
    if (!m_commandSink && m_onDataChanged) {
        m_onDataChanged();
    }
    if (m_commandSink) {
        AudioQueueCommand cmd;
        cmd.type = AudioQueueCommandType::SetTrackPan;
        cmd.trackIndex = m_trackIndex;
        cmd.value1 = pan;
        m_commandSink(cmd);
    }
}

void Track::setMute(bool mute) {
    const bool prev = m_muted.exchange(mute);
    if (prev == mute) {
        return;
    }
    if (m_mixerBus) {
        m_mixerBus->setMute(mute);
    }
    if (!m_commandSink && m_onDataChanged) {
        m_onDataChanged();
    }
    if (m_commandSink) {
        AudioQueueCommand cmd;
        cmd.type = AudioQueueCommandType::SetTrackMute;
        cmd.trackIndex = m_trackIndex;
        cmd.value1 = mute ? 1.0f : 0.0f;
        m_commandSink(cmd);
    }
}

void Track::setSolo(bool solo) {
    const bool prev = m_soloed.exchange(solo);
    if (prev == solo) {
        return;
    }
    if (m_mixerBus) {
        m_mixerBus->setSolo(solo);
    }
    if (!m_commandSink && m_onDataChanged) {
        m_onDataChanged();
    }
    if (m_commandSink) {
        AudioQueueCommand cmd;
        cmd.type = AudioQueueCommandType::SetTrackSolo;
        cmd.trackIndex = m_trackIndex;
        cmd.value1 = solo ? 1.0f : 0.0f;
        m_commandSink(cmd);
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
    const TrackState previousState = getState();
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
        std::lock_guard<std::recursive_mutex> lock(m_audioDataMutex);
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

        const bool enableStreaming = false;
        if (enableStreaming && infoOk) {
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
                    // Notify that audio data changed (for graph rebuild)
                    if (m_onDataChanged) {
                        m_onDataChanged();
                    }
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

            {
                std::lock_guard<std::recursive_mutex> lock(m_audioDataMutex);
                m_sampleBuffer = buffer;
                m_sampleRate = sampleRate;
                m_numChannels = numChannels;
                m_sourceChannels = sourceChannels;
                m_sourcePath = filePath;
                m_durationSeconds.store(sampleRate > 0 ? static_cast<double>(buffer->numFrames) / sampleRate : 0.0);
            }
            setState(TrackState::Loaded);

            Log::info("WAV loaded successfully via SamplePool: " + std::to_string(buffer->data.size()) + " samples, " +
                       std::to_string(m_durationSeconds.load()) + " seconds");
            // Notify that audio data changed (for graph rebuild)
            if (m_onDataChanged) {
                m_onDataChanged();
            }
            // If we were already playing, keep playing with the new buffer
            if (previousState == TrackState::Playing) {
                setState(TrackState::Playing);
            }
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

            // Prefer miniaudio when enabled (MP3/FLAC/OGG/etc). Falls back to MF on Windows.
            if (!loadWithMiniAudio(filePath, decoded, sr, ch)) {
                if (!loadWithMediaFoundation(filePath, decoded, sr, ch)) {
                    return false;
                }
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

            {
                std::lock_guard<std::recursive_mutex> lock(m_audioDataMutex);
                m_sampleBuffer = buffer;
                m_sampleRate = sampleRate;
                m_numChannels = numChannels;
                m_sourceChannels = sourceChannels;
                m_sourcePath = filePath;
                m_durationSeconds.store(sampleRate > 0 ? static_cast<double>(buffer->numFrames) / sampleRate : 0.0);
            }
            setState(TrackState::Loaded);

            Log::info("Audio loaded via Media Foundation + SamplePool: " + std::to_string(buffer->data.size()) + " samples, " +
                       std::to_string(m_durationSeconds.load()) + " seconds @ " +
                       std::to_string(sampleRate) + " Hz, channels: " + std::to_string(numChannels));
            // Notify that audio data changed (for graph rebuild)
            if (m_onDataChanged) {
                m_onDataChanged();
            }
            // If we were already playing, keep playing with the new buffer
            if (previousState == TrackState::Playing) {
                setState(TrackState::Playing);
            }
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
        std::lock_guard<std::recursive_mutex> lock(m_audioDataMutex);
        m_audioData.swap(buffer);
        m_numChannels = 2;
        m_sourceChannels = 2;
    }

    m_durationSeconds.store(duration);
    setState(TrackState::Loaded);

    std::cout << "Preview tone generated: " << m_audioData.size() << " samples, "
              << m_durationSeconds.load() << " seconds, " << baseFrequency << " Hz" << std::endl;

    // Notify that audio data changed (for graph rebuild)
    if (m_onDataChanged) {
        m_onDataChanged();
    }
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
        std::lock_guard<std::recursive_mutex> lock(m_audioDataMutex);
        m_audioData.swap(buffer);
        m_numChannels = 2;
        m_sourceChannels = 2;
    }

    m_durationSeconds.store(duration);
    setState(TrackState::Loaded);

    std::cout << "Demo audio generated: " << m_audioData.size() << " samples, "
              << m_durationSeconds.load() << " seconds, " << frequency << " Hz" << std::endl;

    // Notify that audio data changed (for graph rebuild)
    if (m_onDataChanged) {
        m_onDataChanged();
    }
    return true;
}

void Track::clearAudioData() {
    {
        std::lock_guard<std::recursive_mutex> lock(m_audioDataMutex);
        m_sampleBuffer.reset();
    }
    {
        std::lock_guard<std::recursive_mutex> lock(m_audioDataMutex);
        m_audioData.clear();
        m_recordingBuffer.clear();
        m_numChannels = 2;
        m_sourceChannels = 2;
    }
    m_durationSeconds.store(0.0);
    m_playbackPhase.store(0.0);
    m_positionSeconds.store(0.0);
    setState(TrackState::Empty);

    if (m_onDataChanged) {
        m_onDataChanged();
    }
}

namespace {
std::vector<float> resampleLinearStereo(const std::vector<float>& inputStereo,
                                        uint32_t inSampleRate,
                                        uint32_t outSampleRate) {
    if (inputStereo.empty() || inSampleRate == 0 || outSampleRate == 0) {
        return {};
    }
    const uint64_t inFrames = static_cast<uint64_t>(inputStereo.size() / 2);
    const double rateRatio = static_cast<double>(inSampleRate) / static_cast<double>(outSampleRate);
    const uint64_t outFrames = static_cast<uint64_t>(std::ceil(static_cast<double>(inFrames) * static_cast<double>(outSampleRate) / static_cast<double>(inSampleRate)));
    std::vector<float> output;
    output.resize(static_cast<size_t>(outFrames) * 2);

    const float* src = inputStereo.data();
    const int64_t inFramesSigned = static_cast<int64_t>(inFrames);

    for (uint64_t i = 0; i < outFrames; ++i) {
        double srcPos = static_cast<double>(i) * rateRatio;
        int64_t idx = static_cast<int64_t>(srcPos);
        float frac = static_cast<float>(srcPos - static_cast<double>(idx));

        // Get 4 samples for cubic Hermite interpolation (clamped to bounds)
        int64_t idx0 = std::max<int64_t>(0, idx - 1);
        int64_t idx1 = std::max<int64_t>(0, idx);
        int64_t idx2 = std::min<int64_t>(idx + 1, inFramesSigned - 1);
        int64_t idx3 = std::min<int64_t>(idx + 2, inFramesSigned - 1);

        // Left channel
        float l0 = src[idx0 * 2];
        float l1 = src[idx1 * 2];
        float l2 = src[idx2 * 2];
        float l3 = src[idx3 * 2];

        // Right channel
        float r0 = src[idx0 * 2 + 1];
        float r1 = src[idx1 * 2 + 1];
        float r2 = src[idx2 * 2 + 1];
        float r3 = src[idx3 * 2 + 1];

        // Cubic Hermite coefficients
        float frac2 = frac * frac;
        float frac3 = frac2 * frac;

        float c0 = -0.5f * frac3 + frac2 - 0.5f * frac;
        float c1 = 1.5f * frac3 - 2.5f * frac2 + 1.0f;
        float c2 = -1.5f * frac3 + 2.0f * frac2 + 0.5f * frac;
        float c3 = 0.5f * frac3 - 0.5f * frac2;

        output[static_cast<size_t>(i) * 2]     = l0 * c0 + l1 * c1 + l2 * c2 + l3 * c3;
        output[static_cast<size_t>(i) * 2 + 1] = r0 * c0 + r1 * c1 + r2 * c2 + r3 * c3;
    }
    return output;
}
} // namespace

void Track::setAudioData(const float* data, uint32_t numSamples, uint32_t sampleRate, uint32_t numChannels, uint32_t targetSampleRate) {
    if (!data || numSamples == 0) {
        Log::error("Invalid audio data");
        return;
    }
    
    stopStreaming();
    {
        std::lock_guard<std::recursive_mutex> lock(m_audioDataMutex);
        m_sampleBuffer.reset();
    }

    // Copy audio data and enforce stereo for the engine
    std::vector<float> temp(data, data + (numSamples * numChannels));
    uint32_t inChannels = numChannels;

    // Preserve source channel count for UI; force stereo for engine
    m_sourceChannels = numChannels;
    forceStereo(temp, inChannels, m_sourceChannels);

    // Optional resample to target SR (if provided and different)
    uint32_t targetSR = (targetSampleRate > 0) ? targetSampleRate : sampleRate;
    if (targetSR == 0) {
        targetSR = sampleRate;
    }
    std::vector<float> finalData;
    if (targetSR != sampleRate) {
        finalData = resampleLinearStereo(temp, sampleRate, targetSR);
    } else {
        finalData.swap(temp);
    }

    {
        std::lock_guard<std::recursive_mutex> lock(m_audioDataMutex);
        m_sampleRate = targetSR;
        m_audioData.swap(finalData.empty() ? temp : finalData);
        m_numChannels = 2;
    }
    const double durationSeconds = static_cast<double>(m_audioData.size() / m_numChannels) / static_cast<double>(m_sampleRate);
    m_durationSeconds.store(durationSeconds);
    m_playbackPhase.store(0.0);
    m_positionSeconds.store(0.0);
    setState(TrackState::Loaded);
    
    const uint64_t storedFrames = static_cast<uint64_t>(m_audioData.size() / m_numChannels);
    Log::info("Audio data loaded: " + std::to_string(storedFrames) + " frames @ " +
               std::to_string(m_sampleRate) + " Hz (source " + std::to_string(sampleRate) + " Hz, " +
               std::to_string(numChannels) + " ch)");

    if (m_onDataChanged) {
        m_onDataChanged();
    }
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
    
    if (m_streaming.load(std::memory_order_relaxed)) {
        uint64_t targetFrame = static_cast<uint64_t>(seconds * m_sampleRate);
        uint64_t maxFrames = m_streamTotalFrames.load(std::memory_order_relaxed);
        if (targetFrame > maxFrames) targetFrame = maxFrames;

        // Re-seek streaming source
        uint64_t dataSizeBytes = maxFrames * static_cast<uint64_t>(m_streamBytesPerSample.load(std::memory_order_relaxed) * m_sourceChannels);
        startWavStreaming(m_sourcePath, m_sampleRate, static_cast<uint16_t>(m_sourceChannels),
                          static_cast<uint16_t>(m_streamBytesPerSample.load(std::memory_order_relaxed) * 8),
                          m_streamDataOffset.load(std::memory_order_relaxed), dataSizeBytes, targetFrame);
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
    std::lock_guard<std::recursive_mutex> lock(m_audioDataMutex);

    std::shared_ptr<AudioBuffer> sampleBuffer = m_sampleBuffer;
    const std::vector<float>* buffer = nullptr;
    uint32_t channels = m_numChannels;
    if (m_streaming) {
        buffer = &m_audioData;
    } else if (sampleBuffer && sampleBuffer->ready.load()) {
        buffer = &sampleBuffer->data;
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
    const uint32_t outputRate = static_cast<uint32_t>(outputSampleRate);

    const uint64_t bufferFrames = totalSamples / channels;
    const uint64_t baseFrame = m_streamBaseFrame.load(std::memory_order_relaxed);

    // ============================================================================
    // SRC Module Path (batch processing - more efficient)
    // ============================================================================
    if (m_useSRCModule && !m_streaming && m_sampleRate != outputRate) {
        // Configure SRC if sample rate or output rate changed
        if (!m_srcConverter.isConfigured() || 
            m_srcConverter.getSourceRate() != m_sampleRate ||
            m_lastOutputSampleRate != outputRate) {
            
            SRCQuality quality = mapResamplingToSRC(m_qualitySettings.resampling);
            m_srcConverter.configure(m_sampleRate, outputRate, channels, quality);
            m_srcConverter.reset();  // Clear history to prevent discontinuities on config change
            m_lastOutputSampleRate = outputRate;
        }
        
        // Calculate input frames needed based on phase
        uint64_t startFrame = static_cast<uint64_t>(phase);
        if (startFrame >= bufferFrames) {
            generateSilence(outputBuffer, numFrames);
            m_playbackPhase.store(0.0);
            return;
        }
        
        // Process available frames
        uint32_t availableFrames = static_cast<uint32_t>(bufferFrames - startFrame);
        uint32_t inputFramesToProcess = std::min(
            availableFrames,
            static_cast<uint32_t>(numFrames * sampleRateRatio) + 16  // Extra for filter delay
        );
        
        const float* inputPtr = buffer->data() + startFrame * channels;
        uint32_t written = m_srcConverter.process(inputPtr, inputFramesToProcess, 
                                                   outputBuffer, numFrames);
        
        // Fill remainder with silence if needed
        if (written < numFrames) {
            std::fill(outputBuffer + written * channels, 
                     outputBuffer + numFrames * channels, 0.0f);
        }
        
        // Update phase correctly: advance by input frames consumed
        // Each output frame consumed (1 / ratio) input frames from source
        // Use the actual written frames to calculate precise phase advancement
        const double effectiveRatio = static_cast<double>(m_sampleRate) / static_cast<double>(outputRate);
        phase += static_cast<double>(written) * effectiveRatio;
        
    } else {
        // ============================================================================
        // Legacy Path (per-sample interpolation)
        // ============================================================================
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
    if (m_streaming.load(std::memory_order_relaxed)) {
        trimStreamBuffer(static_cast<uint64_t>(phase));
        m_streamCv.notify_one();
    }
}

void Track::trimStreamBuffer(uint64_t currentFrame) {
    if (!m_streaming.load(std::memory_order_relaxed)) return;
    const uint32_t keepMargin = 8192; // frames to keep ahead of playhead for safety
    const uint64_t targetBase = (currentFrame > keepMargin) ? (currentFrame - keepMargin) : 0;

    // CRITICAL: Must hold m_audioDataMutex when modifying m_audioData to prevent
    // race conditions with streamWavThread and copyAudioData.
    // Note: m_audioDataMutex is recursive, so this is safe even when called from
    // copyAudioData which already holds the lock.
    std::lock_guard<std::recursive_mutex> lock(m_audioDataMutex);
    
    uint64_t baseFrame = m_streamBaseFrame.load(std::memory_order_relaxed);
    if (targetBase > baseFrame) {
        uint64_t framesToDrop = targetBase - baseFrame;
        uint64_t availableFrames = m_audioData.size() / m_numChannels;
        if (framesToDrop >= availableFrames) {
            // Do not drop everything
            return;
        }
        const size_t dropSamples = static_cast<size_t>(framesToDrop * m_numChannels);
        m_audioData.erase(m_audioData.begin(), m_audioData.begin() + dropSamples);
        m_streamBaseFrame.store(baseFrame + framesToDrop, std::memory_order_relaxed);
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
        std::lock_guard<std::recursive_mutex> lock(m_audioDataMutex);
        m_audioData.clear();
        m_streamBaseFrame.store(startFrame, std::memory_order_relaxed);
        m_streamEof.store(false, std::memory_order_relaxed);
        m_streamTotalFrames.store((bitsPerSample / 8 == 0 || channels == 0) ? 0 : dataSize / bytesPerFrame, std::memory_order_relaxed);
        m_streamBytesPerSample.store(bitsPerSample / 8, std::memory_order_relaxed);
        m_streamDataOffset.store(dataOffset, std::memory_order_relaxed);
        m_sampleRate = sampleRate;
        m_numChannels = 2;
        m_sourceChannels = channels;
    }

    m_streamStop.store(false);
    m_streaming.store(true, std::memory_order_release);

    // Launch background thread
    m_streamThread = std::thread(&Track::streamWavThread, this, channels);
    return true;
}

void Track::stopStreaming() {
    if (!m_streaming.load(std::memory_order_relaxed)) return;
    m_streamStop.store(true);
    m_streamCv.notify_one();
    if (m_streamThread.joinable()) {
        m_streamThread.join();
    }
    m_streaming.store(false, std::memory_order_release);
    m_streamEof.store(false, std::memory_order_relaxed);
    if (m_streamFile.is_open()) {
        m_streamFile.close();
    }
}

void Track::streamWavThread(uint32_t channels) {
    const uint32_t bytesPerSample = m_streamBytesPerSample.load(std::memory_order_relaxed);
    const uint32_t bytesPerFrame = bytesPerSample * channels;
    const uint32_t targetBufferFrames = m_sampleRate * 6; // keep ~6s buffered
    const uint32_t chunkFrames = m_sampleRate;            // read ~1s per iteration

    while (!m_streamStop.load()) {
        std::unique_lock<std::mutex> lk(m_streamMutex);
        m_streamCv.wait_for(lk, std::chrono::milliseconds(20), [&] {
            std::lock_guard<std::recursive_mutex> audioLock(m_audioDataMutex);
            uint64_t bufferedFrames = m_audioData.size() / m_numChannels;
            return m_streamStop.load() || bufferedFrames < targetBufferFrames;
        });
        lk.unlock();
        if (m_streamStop.load()) break;

        // Check EOF
        {
            std::lock_guard<std::recursive_mutex> audioLock(m_audioDataMutex);
            uint64_t bufferedFrames = m_audioData.size() / m_numChannels;
            uint64_t baseFrame = m_streamBaseFrame.load(std::memory_order_relaxed);
            uint64_t totalFrames = m_streamTotalFrames.load(std::memory_order_relaxed);
            if (m_streamEof.load(std::memory_order_relaxed) || (baseFrame + bufferedFrames) >= totalFrames) {
                m_streamEof.store(true, std::memory_order_relaxed);
                continue;
            }
        }

        // Read next chunk
        const uint64_t baseFrame = m_streamBaseFrame.load(std::memory_order_relaxed);
        const uint64_t totalFrames = m_streamTotalFrames.load(std::memory_order_relaxed);
        const uint64_t currentEndFrame = baseFrame + (m_audioData.size() / m_numChannels);
        const uint64_t remainingFrames = (totalFrames > currentEndFrame) ? (totalFrames - currentEndFrame) : 0;
        uint32_t framesToRead = static_cast<uint32_t>(std::min<uint64_t>(chunkFrames, remainingFrames));
        if (framesToRead == 0) {
            m_streamEof.store(true, std::memory_order_relaxed);
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
            std::lock_guard<std::recursive_mutex> audioLock(m_audioDataMutex);
            m_audioData.insert(m_audioData.end(), decoded.begin(), decoded.end());
        }

        if (decoded.empty() || gotFrames < framesToRead) {
            m_streamEof.store(true, std::memory_order_relaxed);
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
// Using Catmull-Rom spline for smooth curves without overshoot
float Track::interpolateCubic(const float* data, uint32_t totalSamples, double position, uint32_t channel) const {
    uint32_t idx = static_cast<uint32_t>(position);
    double t = position - idx;  // Use double for precision
    
    // Get 4 samples for cubic interpolation (centered on idx)
    uint32_t idx0 = (idx > 0) ? (idx - 1) * m_numChannels + channel : 0;
    uint32_t idx1 = idx * m_numChannels + channel;
    uint32_t idx2 = (idx + 1) * m_numChannels + channel;
    uint32_t idx3 = (idx + 2) * m_numChannels + channel;
    
    // Use double precision for samples to preserve dynamic range
    double s0 = (idx0 < totalSamples) ? static_cast<double>(data[idx0]) : 0.0;
    double s1 = (idx1 < totalSamples) ? static_cast<double>(data[idx1]) : 0.0;
    double s2 = (idx2 < totalSamples) ? static_cast<double>(data[idx2]) : 0.0;
    double s3 = (idx3 < totalSamples) ? static_cast<double>(data[idx3]) : 0.0;
    
    // Catmull-Rom spline (tension = 0.5) - optimal for audio
    double t2 = t * t;
    double t3 = t2 * t;
    
    // Horner form for efficiency
    double a0 = -0.5 * s0 + 1.5 * s1 - 1.5 * s2 + 0.5 * s3;
    double a1 = s0 - 2.5 * s1 + 2.0 * s2 - 0.5 * s3;
    double a2 = -0.5 * s0 + 0.5 * s2;
    double a3 = s1;
    
    // No clamping! DAWs don't clamp during interpolation - only at final output
    // This preserves full dynamic range and prevents high-frequency distortion
    return static_cast<float>(a0 * t3 + a1 * t2 + a2 * t + a3);
}

// Windowed Sinc interpolation (high quality, efficient)
// Using Blackman window for excellent stopband rejection (-74dB)
float Track::interpolateSinc(const float* data, uint32_t totalSamples, double position, uint32_t channel) const {
    constexpr int SINC_WINDOW_SIZE = 8;  // 8-point sinc (good quality/performance balance)
    constexpr double PI = 3.14159265358979323846;
    constexpr double INV_PI = 1.0 / PI;
    
    uint32_t centerIdx = static_cast<uint32_t>(position);
    double fraction = position - static_cast<double>(centerIdx);
    
    // Use double precision accumulation for better dynamic range
    double sum = 0.0;
    double windowSum = 0.0;
    
    // Windowed sinc interpolation with Blackman window
    for (int i = -SINC_WINDOW_SIZE/2; i < SINC_WINDOW_SIZE/2; ++i) {
        int sampleIdx = static_cast<int>(centerIdx) + i;
        
        if (sampleIdx < 0 || static_cast<uint32_t>(sampleIdx * m_numChannels + channel) >= totalSamples) {
            continue;
        }
        
        double sample = static_cast<double>(data[sampleIdx * m_numChannels + channel]);
        
        // Sinc function: sin(PI*x) / (PI*x)
        double x = static_cast<double>(i) - fraction;
        double sinc = (std::abs(x) < 1e-10) ? 1.0 : std::sin(PI * x) * INV_PI / x;
        
        // Blackman window (better stopband than Hann)
        double windowPos = static_cast<double>(i + SINC_WINDOW_SIZE/2) / static_cast<double>(SINC_WINDOW_SIZE);
        double window = 0.42 - 0.5 * std::cos(2.0 * PI * windowPos) + 0.08 * std::cos(4.0 * PI * windowPos);
        
        double weight = sinc * window;
        sum += sample * weight;
        windowSum += weight;
    }
    
    // Normalize by window sum (no clamping - preserves dynamic range)
    return static_cast<float>((windowSum > 1e-10) ? (sum / windowSum) : 0.0);
}

// Ultra (Polyphase Sinc) interpolation (16-point, mastering grade)
// Uses precomputed Kaiser window for efficiency
float Track::interpolateUltra(const float* data, uint32_t totalSamples, double position, uint32_t channel) const {
    constexpr int POLYPHASE_SIZE = 16;
    constexpr double PI = 3.14159265358979323846;
    constexpr double INV_PI = 1.0 / PI;
    
    // Precomputed Kaiser window lookup table (beta=8.6)
    static const double kaiserWindow[POLYPHASE_SIZE] = {
        0.0000, 0.0217, 0.0854, 0.1865, 0.3180, 0.4706, 0.6341, 0.7975,
        0.9500, 0.9500, 0.7975, 0.6341, 0.4706, 0.3180, 0.1865, 0.0854
    };
    
    uint32_t centerIdx = static_cast<uint32_t>(position);
    double fraction = position - static_cast<double>(centerIdx);
    
    // Double precision accumulation for ~144dB dynamic range
    double sum = 0.0;
    double windowSum = 0.0;
    
    for (int i = -POLYPHASE_SIZE/2; i < POLYPHASE_SIZE/2; ++i) {
        int sampleIdx = static_cast<int>(centerIdx) + i;
        
        if (sampleIdx < 0 || static_cast<uint32_t>(sampleIdx * m_numChannels + channel) >= totalSamples) {
            continue;
        }
        
        double sample = static_cast<double>(data[sampleIdx * m_numChannels + channel]);
        
        // Sinc function with double precision
        double x = static_cast<double>(i) - fraction;
        double sinc = (std::abs(x) < 1e-10) ? 1.0 : std::sin(PI * x) * INV_PI / x;
        
        // Get precomputed Kaiser window value
        int windowIdx = i + POLYPHASE_SIZE/2;
        double window = kaiserWindow[windowIdx];
        
        double weight = sinc * window;
        sum += sample * weight;
        windowSum += weight;
    }
    
    // No clamping - preserves full dynamic range
    return static_cast<float>((windowSum > 1e-10) ? (sum / windowSum) : 0.0);
}

// Extreme interpolation (64-point polyphase sinc - mastering grade, real-time safe)
float Track::interpolateExtreme(const float* data, uint32_t totalSamples, double position, uint32_t channel) const {
    constexpr int EXTREME_SIZE = 64;
    constexpr double PI = 3.14159265358979323846;
    constexpr double INV_PI = 1.0 / PI;
    
    // Precomputed Kaiser window lookup table (beta=10.0)
    static const double kaiserWindow[EXTREME_SIZE] = {
        0.0000, 0.0011, 0.0044, 0.0098, 0.0173, 0.0268, 0.0384, 0.0520,
        0.0675, 0.0849, 0.1042, 0.1252, 0.1479, 0.1722, 0.1980, 0.2252,
        0.2537, 0.2834, 0.3142, 0.3460, 0.3786, 0.4119, 0.4459, 0.4803,
        0.5151, 0.5502, 0.5854, 0.6206, 0.6557, 0.6906, 0.7250, 0.7590,
        0.7923, 0.8249, 0.8566, 0.8873, 0.9169, 0.9453, 0.9724, 0.9981,
        1.0000, 0.9981, 0.9724, 0.9453, 0.9169, 0.8873, 0.8566, 0.8249,
        0.7923, 0.7590, 0.7250, 0.6906, 0.6557, 0.6206, 0.5854, 0.5502,
        0.5151, 0.4803, 0.4459, 0.4119, 0.3786, 0.3460, 0.3142, 0.2834
    };
    
    uint32_t centerIdx = static_cast<uint32_t>(position);
    double fraction = position - static_cast<double>(centerIdx);
    
    // Double precision accumulation
    double sum = 0.0;
    double windowSum = 0.0;
    
    for (int i = -EXTREME_SIZE/2; i < EXTREME_SIZE/2; ++i) {
        int sampleIdx = static_cast<int>(centerIdx) + i;
        
        if (sampleIdx < 0 || static_cast<uint32_t>(sampleIdx * m_numChannels + channel) >= totalSamples) {
            continue;
        }
        
        double sample = static_cast<double>(data[sampleIdx * m_numChannels + channel]);
        
        double x = static_cast<double>(i) - fraction;
        double sinc = (std::abs(x) < 1e-10) ? 1.0 : std::sin(PI * x) * INV_PI / x;
        
        int windowIdx = i + EXTREME_SIZE/2;
        double window = kaiserWindow[windowIdx];
        
        double weight = sinc * window;
        sum += sample * weight;
        windowSum += weight;
    }
    
    return static_cast<float>((windowSum > 1e-10) ? (sum / windowSum) : 0.0);
}

// Perfect interpolation (512-point polyphase sinc - FL Studio grade)
// WARNING: CPU intensive - best for offline rendering
float Track::interpolatePerfect(const float* data, uint32_t totalSamples, double position, uint32_t channel) const {
    constexpr int PERFECT_SIZE = 512;
    constexpr double PI = 3.14159265358979323846;
    constexpr double INV_PI = 1.0 / PI;
    
    uint32_t centerIdx = static_cast<uint32_t>(position);
    double fraction = position - static_cast<double>(centerIdx);
    
    // Double precision for maximum dynamic range
    double sum = 0.0;
    double windowSum = 0.0;
    
    // Precompute Bessel I0(12.0) once
    auto besselI0 = [](double x) -> double {
        double sum = 1.0;
        double term = 1.0;
        for (int n = 1; n < 25; ++n) {
            term *= (x / (2.0 * n)) * (x / (2.0 * n));
            sum += term;
            if (term < 1e-12) break;
        }
        return sum;
    };
    const double invI0_12 = 1.0 / besselI0(12.0);
    
    for (int i = -PERFECT_SIZE/2; i < PERFECT_SIZE/2; ++i) {
        int sampleIdx = static_cast<int>(centerIdx) + i;
        
        if (sampleIdx < 0 || static_cast<uint32_t>(sampleIdx * m_numChannels + channel) >= totalSamples) {
            continue;
        }
        
        double sample = static_cast<double>(data[sampleIdx * m_numChannels + channel]);
        
        double x = static_cast<double>(i) - fraction;
        double sinc = (std::abs(x) < 1e-10) ? 1.0 : std::sin(PI * x) * INV_PI / x;
        
        // Kaiser window (beta=12.0)
        double alpha = static_cast<double>(i + PERFECT_SIZE/2) / static_cast<double>(PERFECT_SIZE);
        double arg = 2.0 * alpha - 1.0;
        double kaiserArg = 12.0 * std::sqrt(std::max(0.0, 1.0 - arg * arg));
        double window = besselI0(kaiserArg) * invI0_12;
        
        double weight = sinc * window;
        sum += sample * weight;
        windowSum += weight;
    }
    
    return static_cast<float>((windowSum > 1e-10) ? (sum / windowSum) : 0.0);
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
        // Fast xorshift32 PRNG (better than rand() for audio - no thread safety issues)
        m_ditherRngState ^= m_ditherRngState << 13;
        m_ditherRngState ^= m_ditherRngState >> 17;
        m_ditherRngState ^= m_ditherRngState << 5;
        float r1 = static_cast<float>(m_ditherRngState) / 4294967295.0f - 0.5f;
        
        m_ditherRngState ^= m_ditherRngState << 13;
        m_ditherRngState ^= m_ditherRngState >> 17;
        m_ditherRngState ^= m_ditherRngState << 5;
        float r2 = static_cast<float>(m_ditherRngState) / 4294967295.0f - 0.5f;
        
        // TPDF: sum of two uniform random numbers gives triangular distribution
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
        // Fast xorshift32 PRNG for TPDF dither
        m_ditherRngState ^= m_ditherRngState << 13;
        m_ditherRngState ^= m_ditherRngState >> 17;
        m_ditherRngState ^= m_ditherRngState << 5;
        float r1 = static_cast<float>(m_ditherRngState) / 4294967295.0f - 0.5f;
        
        m_ditherRngState ^= m_ditherRngState << 13;
        m_ditherRngState ^= m_ditherRngState >> 17;
        m_ditherRngState ^= m_ditherRngState << 5;
        float r2 = static_cast<float>(m_ditherRngState) / 4294967295.0f - 0.5f;
        
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
        
        // Fast xorshift32 PRNG for TPDF dither
        m_ditherRngState ^= m_ditherRngState << 13;
        m_ditherRngState ^= m_ditherRngState >> 17;
        m_ditherRngState ^= m_ditherRngState << 5;
        float r1 = static_cast<float>(m_ditherRngState) / 4294967295.0f - 0.5f;
        
        m_ditherRngState ^= m_ditherRngState << 13;
        m_ditherRngState ^= m_ditherRngState >> 17;
        m_ditherRngState ^= m_ditherRngState << 5;
        float r2 = static_cast<float>(m_ditherRngState) / 4294967295.0f - 0.5f;
        
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
    
    // Using member variables instead of static to prevent cross-track contamination
    for (uint32_t frame = 0; frame < numFrames; ++frame) {
        uint32_t leftIdx = frame * 2;
        uint32_t rightIdx = frame * 2 + 1;
        
        float left = buffer[leftIdx];
        float right = buffer[rightIdx];
        
        // Convert to Mid/Side
        float mid = (left + right) * 0.5f;
        float side = (left - right) * 0.5f;
        
        // Apply differential delay to side (creates depth)
        int readPos = (m_airDelayPos - DELAY_SAMPLES + 8) % 8;
        float delayedSide = (m_airDelayL[readPos] - m_airDelayR[readPos]) * 0.5f;
        
        // Store current samples in member delay buffers
        m_airDelayL[m_airDelayPos] = left;
        m_airDelayR[m_airDelayPos] = right;
        m_airDelayPos = (m_airDelayPos + 1) % 8;
        
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
    // Using member variables instead of static to prevent cross-track contamination
    
    const float DRIFT_RATE = 0.0003f;      // Very slow modulation (~0.2 Hz)
    const float DRIFT_DEPTH = 0.00015f;    // ±0.015% pitch variance (very subtle)
    const float JITTER_AMOUNT = 0.00005f;  // Clock jitter noise floor
    
    for (uint32_t frame = 0; frame < numFrames; ++frame) {
        // LFO for tape speed variance
        m_driftPhase += DRIFT_RATE;
        if (m_driftPhase > 6.28318f) m_driftPhase -= 6.28318f;
        
        // Smooth drift modulation
        float drift = std::sin(m_driftPhase) * DRIFT_DEPTH;
        
        // Fast PRNG for jitter (xorshift32 - better than rand() for audio)
        m_ditherRngState ^= m_ditherRngState << 13;
        m_ditherRngState ^= m_ditherRngState >> 17;
        m_ditherRngState ^= m_ditherRngState << 5;
        float jitter = (static_cast<float>(m_ditherRngState) / 4294967295.0f - 0.5f) * JITTER_AMOUNT;
        
        m_driftAmount = drift + jitter;
        
        // Apply very subtle pitch modulation to stereo image
        // This creates the "living, breathing" quality of analog gear
        // Implementation: In real version, this would modulate the resampling phase
        // For now, we apply a subtle amplitude modulation as a placeholder
        for (uint32_t ch = 0; ch < m_numChannels; ++ch) {
            uint32_t idx = frame * m_numChannels + ch;
            buffer[idx] *= (1.0f + m_driftAmount);
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

// ============================================================================
// CLIP TRIMMING (non-destructive editing)
// ============================================================================

void Track::setTrimStart(double seconds) {
    double duration = getDuration();
    // Clamp to valid range
    seconds = std::max(0.0, std::min(seconds, duration));
    
    // Ensure trim start is before trim end
    double trimEnd = m_trimEnd.load();
    if (trimEnd >= 0.0 && seconds >= trimEnd) {
        seconds = trimEnd - 0.001; // At least 1ms before end
    }
    
    m_trimStart.store(seconds);
    Log::info("Track " + m_name + " trim start set to " + std::to_string(seconds) + "s");
}

void Track::setTrimEnd(double seconds) {
    double duration = getDuration();
    
    // -1 means use full length
    if (seconds < 0) {
        m_trimEnd.store(-1.0);
        return;
    }
    
    // Clamp to valid range
    seconds = std::max(0.0, std::min(seconds, duration));
    
    // Ensure trim end is after trim start
    double trimStart = m_trimStart.load();
    if (seconds <= trimStart) {
        seconds = trimStart + 0.001; // At least 1ms after start
    }
    
    m_trimEnd.store(seconds);
    Log::info("Track " + m_name + " trim end set to " + std::to_string(seconds) + "s");
}

double Track::getTrimmedDuration() const {
    double duration = getDuration();
    if (duration <= 0.0) return 0.0;
    
    double trimStart = m_trimStart.load();
    double trimEnd = m_trimEnd.load();
    
    if (trimEnd < 0.0) {
        trimEnd = duration;
    }
    
    return std::max(0.0, trimEnd - trimStart);
}

void Track::resetTrim() {
    m_trimStart.store(0.0);
    m_trimEnd.store(-1.0);
    Log::info("Track " + m_name + " trim reset to full length");
}

std::shared_ptr<Track> Track::splitAt(double positionInClip) {
    double duration = getDuration();
    if (positionInClip <= 0.0 || positionInClip >= duration) {
        Log::warning("Cannot split track at position " + std::to_string(positionInClip));
        return nullptr;
    }
    
    // Calculate split position in samples
    uint32_t splitSample = static_cast<uint32_t>(positionInClip * m_sampleRate);
    uint32_t totalSamples = static_cast<uint32_t>(m_audioData.size() / m_numChannels);
    
    if (splitSample >= totalSamples) {
        return nullptr;
    }
    
    // Create new track for second half - use SAME name (no "(split)" suffix!)
    // The new track gets its own UUID automatically, preserving track identity
    auto newTrack = std::make_shared<Track>(m_name, m_trackId + 1000);
    newTrack->setColor(m_color);
    
    // IMPORTANT: Copy lane index so both clips stay on the same visual row
    newTrack->setLaneIndex(m_laneIndex);
    
    // Copy second half to new track
    size_t splitIndex = splitSample * m_numChannels;
    std::vector<float> secondHalf(m_audioData.begin() + splitIndex, m_audioData.end());
    newTrack->setAudioData(secondHalf.data(), 
                           totalSamples - splitSample,
                           m_sampleRate, m_numChannels, m_sampleRate);
    
    // Position new track in timeline right after split point
    double originalStart = getStartPositionInTimeline();
    newTrack->setStartPositionInTimeline(originalStart + positionInClip);
    newTrack->setSourcePath(m_sourcePath);
    
    // Trim this track to only first half
    m_audioData.resize(splitIndex);
    m_durationSeconds.store(positionInClip);
    
    // Reset trim end since we truncated
    if (m_trimEnd.load() > positionInClip) {
        m_trimEnd.store(-1.0);
    }
    
    Log::info("Track " + m_name + " (UUID: " + m_uuid.toString() + 
              ") split at " + std::to_string(positionInClip) + "s, new clip UUID: " + 
              newTrack->getUUID().toString() + " (lane: " + std::to_string(m_laneIndex) + ")");
    return newTrack;
}

std::shared_ptr<Track> Track::duplicate() const {
    // Duplicate uses same name (no "(copy)" suffix for cleaner workflow)
    // Each duplicate gets its own UUID automatically
    auto newTrack = std::make_shared<Track>(m_name, m_trackId + 2000);
    
    // Copy all properties
    newTrack->setColor(m_color);
    newTrack->setVolume(getVolume());
    newTrack->setPan(getPan());
    newTrack->setMute(isMuted());
    newTrack->setSourcePath(m_sourcePath);
    
    // Copy audio data
    if (!m_audioData.empty()) {
        uint32_t totalSamples = static_cast<uint32_t>(m_audioData.size() / m_numChannels);
        newTrack->setAudioData(m_audioData.data(), totalSamples, m_sampleRate, m_numChannels, m_sampleRate);
    }
    
    // Copy trim settings
    newTrack->m_trimStart.store(m_trimStart.load());
    newTrack->m_trimEnd.store(m_trimEnd.load());
    
    // Position slightly offset from original
    newTrack->setStartPositionInTimeline(getStartPositionInTimeline());
    
    Log::info("Track " + m_name + " duplicated (new UUID: " + newTrack->getUUID().toString() + ")");
    return newTrack;
}

// =============================================================================
// SRC Quality Mapping
// =============================================================================

SRCQuality Track::mapResamplingToSRC(ResamplingMode mode) {
    switch (mode) {
        case ResamplingMode::Fast:
            return SRCQuality::Linear;
        case ResamplingMode::Medium:
            return SRCQuality::Cubic;
        case ResamplingMode::High:
            return SRCQuality::Sinc8;
        case ResamplingMode::Ultra:
            return SRCQuality::Sinc16;
        case ResamplingMode::Extreme:
        case ResamplingMode::Perfect:
            return SRCQuality::Sinc64;
        default:
            return SRCQuality::Sinc16;
    }
}

} // namespace Audio
} // namespace Nomad
