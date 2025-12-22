// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "PreviewEngine.h"
#include "NomadLog.h"
#include "MiniAudioDecoder.h"
#include "PathUtils.h"
#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef _WIN32
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#endif

namespace Nomad {
namespace Audio {

namespace {
    // Utility copied from Track.cpp
    void downmixToStereoImpl(const std::vector<float>& input, uint32_t inChannels, std::vector<float>& output) {
        if (inChannels == 0) return;
        const size_t frames = input.size() / inChannels;
        output.assign(frames * 2, 0.0f);

        for (size_t i = 0; i < frames; ++i) {
            float left = 0.0f;
            float right = 0.0f;
            const float* frame = &input[i * inChannels];

            if (inChannels >= 1) left += frame[0];
            if (inChannels >= 2) right += frame[1];
            if (inChannels >= 3) {
                float c = frame[2] * 0.7071f;
                left += c; right += c;
            }
            if (inChannels >= 4) {
                float lfe = frame[3] * 0.5f;
                left += lfe; right += lfe;
            }
            if (inChannels >= 5) left += frame[4] * 0.7071f;
            if (inChannels >= 6) right += frame[5] * 0.7071f;
            for (uint32_t ch = 6; ch < inChannels; ++ch) {
                float v = frame[ch] * 0.5f;
                left += v;
                right += v;
            }

            output[i * 2] = std::max(-1.0f, std::min(1.0f, left));
            output[i * 2 + 1] = std::max(-1.0f, std::min(1.0f, right));
        }
    }

    void forceStereo(std::vector<float>& buffer, uint32_t& channelCount) {
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
        downmixToStereoImpl(buffer, channelCount, stereo);
        buffer.swap(stereo);
        channelCount = 2;
    }

    struct WavInfo {
        uint32_t sampleRate{0};
        uint16_t channels{0};
        uint16_t bitsPerSample{0};
        uint32_t dataOffset{0};
        uint32_t dataSize{0};
        uint16_t audioFormat{0};
    };

    bool loadWav(const std::string& filePath, std::vector<float>& audioData, uint32_t& sampleRate, uint32_t& numChannels) {
        // Use makeUnicodePath for proper Unicode file path handling
        std::ifstream file(makeUnicodePath(filePath), std::ios::binary);
        if (!file) {
            Log::warning("PreviewEngine: Failed to open WAV file: " + filePath);
            return false;
        }

        char riffId[4], waveId[4];
        uint32_t riffSize = 0;
        if (!file.read(riffId, 4) || !file.read(reinterpret_cast<char*>(&riffSize), 4) || !file.read(waveId, 4)) {
            return false;
        }
        if (std::strncmp(riffId, "RIFF", 4) != 0 || std::strncmp(waveId, "WAVE", 4) != 0) {
            return false;
        }

        bool fmtFound = false, dataFound = false;
        uint16_t audioFormat = 1;
        uint32_t channelCount = 2;
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
                if (chunkSize > 16) file.seekg(chunkSize - 16, std::ios::cur);
            } else if (std::strncmp(chunkId, "data", 4) == 0) {
                dataFound = true;
                dataSize = chunkSize;
                dataPos = file.tellg();
                file.seekg(chunkSize, std::ios::cur);
            } else {
                file.seekg(chunkSize, std::ios::cur);
            }
            if (chunkSize % 2 == 1) file.seekg(1, std::ios::cur);
        }

        if (!(fmtFound && dataFound)) return false;
        if (audioFormat != 1 && audioFormat != 3) return false;
        if (bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32) return false;

        file.seekg(dataPos);
        size_t bytesPerSample = bitsPerSample / 8;
        audioData.resize(dataSize / bytesPerSample);

        if (bitsPerSample == 16) {
            std::vector<int16_t> raw(dataSize / sizeof(int16_t));
            file.read(reinterpret_cast<char*>(raw.data()), dataSize);
            for (size_t i = 0; i < raw.size(); ++i) {
                audioData[i] = raw[i] / 32768.0f;
            }
        } else if (bitsPerSample == 24) {
            std::vector<uint8_t> raw(dataSize);
            file.read(reinterpret_cast<char*>(raw.data()), dataSize);
            for (size_t i = 0; i < audioData.size(); ++i) {
                uint32_t sample24 = raw[i * 3] | (raw[i * 3 + 1] << 8) | (raw[i * 3 + 2] << 16);
                if (sample24 & 0x800000) sample24 |= 0xFF000000;
                audioData[i] = static_cast<int32_t>(sample24) / 8388608.0f;
            }
        } else if (bitsPerSample == 32) {
            std::vector<float> raw(dataSize / sizeof(float));
            file.read(reinterpret_cast<char*>(raw.data()), dataSize);
            for (size_t i = 0; i < raw.size(); ++i) {
                audioData[i] = std::max(-1.0f, std::min(1.0f, raw[i]));
            }
        }

        sampleRate = sr;
        numChannels = channelCount;
        return true;
    }

#ifdef _WIN32
    bool loadWithMediaFoundation(const std::string& filePath,
                                 std::vector<float>& audioData,
                                 uint32_t& sampleRate,
                                 uint32_t& numChannels) {
        using Microsoft::WRL::ComPtr;

        static std::once_flag initFlag;
        static HRESULT initResult = E_FAIL;
        std::call_once(initFlag, []() {
            initResult = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        });
        if (!SUCCEEDED(initResult)) return false;

        ComPtr<IMFAttributes> attributes;
        if (FAILED(MFCreateAttributes(&attributes, 1))) return false;
#if defined(MF_SOURCE_READER_ENABLE_AUDIO_PROCESSING)
        attributes->SetUINT32(MF_SOURCE_READER_ENABLE_AUDIO_PROCESSING, TRUE);
#elif defined(MF_READWRITE_ENABLE_AUDIO_PROCESSING)
        attributes->SetUINT32(MF_READWRITE_ENABLE_AUDIO_PROCESSING, TRUE);
#endif

        ComPtr<IMFSourceReader> reader;
        std::wstring widePath = pathStringToWide(filePath);
        HRESULT hr = MFCreateSourceReaderFromURL(widePath.c_str(), attributes.Get(), &reader);
        if (FAILED(hr)) return false;

        ComPtr<IMFMediaType> audioType;
        hr = MFCreateMediaType(&audioType);
        if (FAILED(hr)) return false;
        audioType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        audioType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
        audioType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
        hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, audioType.Get());
        if (FAILED(hr)) return false;

        ComPtr<IMFMediaType> currentType;
        hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &currentType);
        if (FAILED(hr)) return false;

        UINT32 sr = 0, ch = 0;
        if (FAILED(currentType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr))) return false;
        if (FAILED(currentType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch))) return false;
        sampleRate = sr;
        numChannels = ch;

        audioData.clear();
        while (true) {
            DWORD flags = 0;
            ComPtr<IMFSample> sample;
            hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample);
            if (FAILED(hr)) return false;
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

        return !audioData.empty();
    }
#endif
} // namespace

PreviewEngine::PreviewEngine()
    : m_activeVoice(nullptr)
    , m_outputSampleRate(48000.0)
    , m_globalGainDb(-6.0f) {}

PreviewEngine::~PreviewEngine() {
    stop();
}

float PreviewEngine::dbToLinear(float db) const {
    return std::pow(10.0f, db / 20.0f);
}

std::shared_ptr<AudioBuffer> PreviewEngine::loadBuffer(const std::string& path, uint32_t& sampleRate, uint32_t& channels) {
    auto loader = [path, &sampleRate, &channels](AudioBuffer& out) -> bool {
        std::string extension;
        if (auto dotPos = path.find_last_of('.'); dotPos != std::string::npos && dotPos + 1 < path.size()) {
            extension = path.substr(dotPos + 1);
            std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
        }

        std::vector<float> decoded;
        uint32_t sr = 48000;
        uint32_t ch = 2;
        bool ok = false;

        if (extension == "wav") {
            ok = loadWav(path, decoded, sr, ch);
        } else {
            // Prefer miniaudio when enabled; fallback to platform decoder.
            ok = loadWithMiniAudio(path, decoded, sr, ch);
#ifdef _WIN32
            if (!ok) {
                ok = loadWithMediaFoundation(path, decoded, sr, ch);
            }
#endif
        }

        if (!ok) return false;
        forceStereo(decoded, ch);
        out.data.swap(decoded);
        out.sampleRate = sr;
        out.channels = ch;
        out.numFrames = out.channels ? out.data.size() / out.channels : 0;
        out.sourcePath = path;
        sampleRate = sr;
        channels = ch;
        return true;
    };

    return SamplePool::getInstance().acquire(path, loader);
}

PreviewResult PreviewEngine::play(const std::string& path, float gainDb, double maxSeconds) {
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    auto buffer = loadBuffer(path, sampleRate, channels);
    if (!buffer || !buffer->ready.load()) {
        Log::warning("PreviewEngine: Failed to load preview for " + path);
        return PreviewResult::Failed;
    }
    // Cache hit path: populate metadata from buffer if loader didn't run
    if (sampleRate == 0 && buffer->sampleRate > 0) {
        sampleRate = buffer->sampleRate;
    }
    if (channels == 0 && buffer->channels > 0) {
        channels = buffer->channels;
    }

    auto voice = std::make_shared<PreviewVoice>();
    voice->buffer = buffer;
    voice->path = path;
    voice->sampleRate = sampleRate > 0 ? static_cast<double>(sampleRate) : 48000.0;
    voice->channels = channels == 0 ? 2u : channels;
    voice->durationSeconds = (sampleRate > 0 && buffer->numFrames > 0) ? (static_cast<double>(buffer->numFrames) / sampleRate) : 0.0;
    voice->maxPlaySeconds = maxSeconds;
    voice->gain = dbToLinear(gainDb + m_globalGainDb.load(std::memory_order_relaxed));
    voice->phaseFrames = 0.0;
    voice->elapsedSeconds = 0.0;
    voice->fadeInPos = 0.0;
    voice->fadeOutPos = 0.0;
    voice->stopRequested.store(false, std::memory_order_release);
    voice->fadeOutActive = false;
    voice->playing.store(true, std::memory_order_release);

    std::atomic_store_explicit(&m_activeVoice, voice, std::memory_order_release);
    Log::info("PreviewEngine: Playing '" + path + "' (" + std::to_string(sampleRate) + " Hz, " +
              std::to_string(voice->durationSeconds) + " sec)");
    return PreviewResult::Success;
}

void PreviewEngine::stop() {
    auto voice = std::atomic_load_explicit(&m_activeVoice, std::memory_order_acquire);
    if (voice) {
        voice->stopRequested.store(true, std::memory_order_release);
        voice->fadeOutActive = true;
        voice->fadeOutPos = 0.0;
    }
}

void PreviewEngine::setOutputSampleRate(double sr) {
    if (sr <= 0.0) return;
    m_outputSampleRate.store(sr, std::memory_order_relaxed);
}

void PreviewEngine::process(float* interleavedOutput, uint32_t numFrames) {
    auto voice = std::atomic_load_explicit(&m_activeVoice, std::memory_order_acquire);
    if (!voice || !voice->playing.load(std::memory_order_acquire) || !interleavedOutput) {
        return;
    }
    auto buffer = voice->buffer;
    if (!buffer || buffer->data.empty() || buffer->sampleRate == 0) {
        return;
    }

    const double streamRate = (m_outputSampleRate.load(std::memory_order_relaxed) > 0.0) ? m_outputSampleRate.load() : 48000.0;
    const double fadeInSamples = streamRate * 0.02;  // 20ms fade-in
    const double fadeOutSamples = streamRate * 0.05; // 50ms fade-out
    const double ratio = voice->sampleRate / streamRate;
    const uint64_t totalFrames = buffer->numFrames;
    const float* data = buffer->data.data();
    double phase = voice->phaseFrames;
    const float gain = voice->gain;

    for (uint32_t i = 0; i < numFrames; ++i) {
        if (static_cast<uint64_t>(phase) >= totalFrames) {
            voice->stopRequested.store(true, std::memory_order_release);
            voice->fadeOutActive = true;
            break;
        }
        uint64_t idx = static_cast<uint64_t>(phase);
        uint64_t idx1 = std::min<uint64_t>(idx + 1, totalFrames - 1);
        float frac = static_cast<float>(phase - idx);

        float l0 = data[idx * 2];
        float l1 = data[idx1 * 2];
        float r0 = data[idx * 2 + 1];
        float r1 = data[idx1 * 2 + 1];

        float outL = l0 + frac * (l1 - l0);
        float outR = r0 + frac * (r1 - r0);

        float envelope = 1.0f;
        if (voice->fadeInPos < fadeInSamples) {
            envelope = static_cast<float>(voice->fadeInPos / fadeInSamples);
            voice->fadeInPos += 1.0;
        }
        if (voice->stopRequested.load(std::memory_order_acquire) || voice->fadeOutActive) {
            voice->fadeOutActive = true;
            double remaining = std::max(0.0, (fadeOutSamples - voice->fadeOutPos) / fadeOutSamples);
            envelope *= static_cast<float>(remaining);
            voice->fadeOutPos += 1.0;
        }

        interleavedOutput[i * 2] += outL * gain * envelope;
        interleavedOutput[i * 2 + 1] += outR * gain * envelope;

        phase += ratio;
    }

    voice->phaseFrames = phase;
    voice->elapsedSeconds += static_cast<double>(numFrames) / streamRate;
    if (voice->maxPlaySeconds > 0.0 && voice->elapsedSeconds >= voice->maxPlaySeconds) {
        voice->stopRequested.store(true, std::memory_order_release);
        voice->fadeOutActive = true;
    }

    bool finished = voice->fadeOutActive && (voice->fadeOutPos >= fadeOutSamples);
    if (finished) {
        if (m_onComplete) {
            m_onComplete(voice->path);
        }
        // Clear only if still the active voice
        std::shared_ptr<PreviewVoice> expected = voice;
        std::atomic_compare_exchange_strong_explicit(
            &m_activeVoice, &expected, std::shared_ptr<PreviewVoice>(),
            std::memory_order_acq_rel, std::memory_order_relaxed);
    }
}

bool PreviewEngine::isPlaying() const {
    auto voice = std::atomic_load_explicit(&m_activeVoice, std::memory_order_acquire);
    return voice && voice->playing.load(std::memory_order_acquire);
}

void PreviewEngine::setOnComplete(std::function<void(const std::string& path)> callback) {
    m_onComplete = std::move(callback);
}

void PreviewEngine::setGlobalPreviewVolume(float gainDb) {
    m_globalGainDb.store(gainDb, std::memory_order_relaxed);
}

float PreviewEngine::getGlobalPreviewVolume() const {
    return m_globalGainDb.load(std::memory_order_relaxed);
}

} // namespace Audio
} // namespace Nomad
