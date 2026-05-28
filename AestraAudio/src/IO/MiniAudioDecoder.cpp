#if defined(AESTRA_USE_MINIAUDIO)
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DEVICE_IO
#include "miniaudio.h"
#endif

#include "AestraLog.h"
#include "MiniAudioDecoder.h"
#include "PathUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

#ifdef _WIN32
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mutex>
#include <wrl/client.h>
#endif

namespace Aestra {
namespace Audio {

namespace {
// ~192 MB at float32: enough for several minutes of stereo/96 kHz audio while
// keeping corrupt length reports from forcing multi-GB allocations.
constexpr size_t kMaxDecodedSamples = 48000000;

void downmixToStereoImpl(const std::vector<float>& input, uint32_t inChannels, std::vector<float>& output) {
    if (inChannels == 0)
        return;
    const size_t frames = input.size() / inChannels;
    output.assign(frames * 2, 0.0f);

    for (size_t i = 0; i < frames; ++i) {
        float left = 0.0f;
        float right = 0.0f;
        const float* frame = &input[i * inChannels];

        if (inChannels >= 1)
            left += frame[0];
        if (inChannels >= 2)
            right += frame[1];
        if (inChannels >= 3) {
            float c = frame[2] * 0.7071f;
            left += c;
            right += c;
        }
        if (inChannels >= 4) {
            float lfe = frame[3] * 0.5f;
            left += lfe;
            right += lfe;
        }
        if (inChannels >= 5)
            left += frame[4] * 0.7071f;
        if (inChannels >= 6)
            right += frame[5] * 0.7071f;
        for (uint32_t ch = 6; ch < inChannels; ++ch) {
            float v = frame[ch] * 0.5f;
            left += v;
            right += v;
        }

        output[i * 2] = std::max(-1.0f, std::min(1.0f, left));
        output[i * 2 + 1] = std::max(-1.0f, std::min(1.0f, right));
    }
}

bool readExact(std::ifstream& file, void* dest, std::streamsize bytes) {
    return static_cast<bool>(file.read(reinterpret_cast<char*>(dest), bytes));
}

bool loadWav(const std::string& filePath, std::vector<float>& audioData, uint32_t& sampleRate, uint32_t& numChannels) {
    std::ifstream file(makeUnicodePath(filePath), std::ios::binary);
    if (!file)
        return false;

    char riffId[4], waveId[4];
    uint32_t riffSize = 0;
    if (!file.read(riffId, 4) || !file.read(reinterpret_cast<char*>(&riffSize), 4) || !file.read(waveId, 4))
        return false;
    if (std::strncmp(riffId, "RIFF", 4) != 0 || std::strncmp(waveId, "WAVE", 4) != 0)
        return false;

    bool fmtFound = false, dataFound = false;
    uint16_t audioFormat = 1;
    uint16_t channelCount = 2;
    uint32_t sr = 44100;
    uint16_t bitsPerSample = 16;
    uint32_t dataSize = 0;
    std::streampos dataPos{};

    while (file && !(fmtFound && dataFound)) {
        char chunkId[4];
        uint32_t chunkSize = 0;
        if (!file.read(chunkId, 4) || !file.read(reinterpret_cast<char*>(&chunkSize), 4))
            break;

        if (std::strncmp(chunkId, "fmt ", 4) == 0) {
            fmtFound = true;
            if (chunkSize < 16)
                return false;
            if (!readExact(file, &audioFormat, sizeof(uint16_t)) ||
                !readExact(file, &channelCount, sizeof(uint16_t)) ||
                !readExact(file, &sr, sizeof(uint32_t))) {
                return false;
            }
            uint32_t byteRate = 0;
            uint16_t blockAlign = 0;
            if (!readExact(file, &byteRate, sizeof(uint32_t))) {
                return false;
            }
            if (!readExact(file, &blockAlign, sizeof(uint16_t))) {
                return false;
            }
            if (!readExact(file, &bitsPerSample, sizeof(uint16_t))) {
                return false;
            }
            uint16_t expectedBlockAlign = channelCount * static_cast<uint16_t>(bitsPerSample / 8);
            uint32_t expectedByteRate = sr * static_cast<uint32_t>(expectedBlockAlign);
            if (blockAlign != expectedBlockAlign || byteRate != expectedByteRate) {
                return false;
            }
            if (chunkSize > 16)
                file.seekg(chunkSize - 16, std::ios::cur);
        } else if (std::strncmp(chunkId, "data", 4) == 0) {
            dataFound = true;
            dataSize = chunkSize;
            dataPos = file.tellg();
            file.seekg(chunkSize, std::ios::cur);
        } else {
            file.seekg(chunkSize, std::ios::cur);
        }
        if (chunkSize % 2 == 1)
            file.seekg(1, std::ios::cur);
    }

    if (!(fmtFound && dataFound))
        return false;
    if (audioFormat != 1 && audioFormat != 3)
        return false;
    if (channelCount == 0 || channelCount > 64 || sr == 0)
        return false;

    // Guard against division by zero (RTM-001) and unsupported bit depths
    if (bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32)
        return false;
    if (audioFormat == 3 && bitsPerSample != 32)
        return false;

    // Guard against heap exhaustion — cap dataSize to actual remaining file size (RTM-002)
    file.seekg(0, std::ios::end);
    std::streamoff fileSize = file.tellg();
    std::streamoff expectedDataEnd = static_cast<std::streamoff>(dataPos) + static_cast<std::streamoff>(dataSize);
    if (expectedDataEnd > fileSize || dataSize == 0) {
        return false;
    }
    file.seekg(dataPos);

    const uint32_t bytesPerSample = bitsPerSample / 8;
    const uint32_t blockAlign = static_cast<uint32_t>(channelCount) * bytesPerSample;
    if (bytesPerSample == 0 || blockAlign == 0 || dataSize % bytesPerSample != 0 || dataSize % blockAlign != 0) {
        return false;
    }

    size_t samplesCount = dataSize / bytesPerSample;
    // Secondary cap: prevent allocations > 500M samples (~2 GB for float32)
    constexpr size_t kMaxSamples = 500000000;
    if (samplesCount > kMaxSamples) {
        return false;
    }
    audioData.resize(samplesCount);

    if (bitsPerSample == 16) {
        std::vector<int16_t> raw(samplesCount);
        if (!readExact(file, raw.data(), dataSize))
            return false;
        for (size_t i = 0; i < samplesCount; ++i)
            audioData[i] = raw[i] / 32768.0f;
    } else if (bitsPerSample == 24) {
        std::vector<uint8_t> raw(dataSize);
        if (!readExact(file, raw.data(), dataSize))
            return false;
        for (size_t i = 0; i < samplesCount; ++i) {
            uint32_t s24 = raw[i * 3] | (raw[i * 3 + 1] << 8) | (raw[i * 3 + 2] << 16);
            if (s24 & 0x800000)
                s24 |= 0xFF000000;
            audioData[i] = static_cast<int32_t>(s24) / 8388608.0f;
        }
    } else if (bitsPerSample == 32) {
        if (audioFormat == 3) {
            if (!readExact(file, audioData.data(), dataSize))
                return false;
        } else {
            std::vector<int32_t> raw(samplesCount);
            if (!readExact(file, raw.data(), dataSize))
                return false;
            constexpr float scale = 1.0f / 2147483648.0f;
            for (size_t i = 0; i < samplesCount; ++i)
                audioData[i] = static_cast<float>(raw[i] * scale);
        }
    } else {
        return false;
    }

    sampleRate = sr;
    numChannels = channelCount;
    return true;
}

#ifdef _WIN32
bool loadWithMediaFoundation(const std::string& filePath, std::vector<float>& audioData, uint32_t& sampleRate,
                             uint32_t& numChannels, std::function<void(float)> progressCallback,
                             uint64_t maxFrames = 0) {
    // Each decode is independent - Media Foundation is thread-safe
    // Stale decodes are discarded at the PreviewEngine level via generation counter

    using Microsoft::WRL::ComPtr;
    static std::once_flag initFlag;
    static HRESULT initResult = E_FAIL;
    std::call_once(initFlag, []() { initResult = MFStartup(MF_VERSION, MFSTARTUP_LITE); });
    if (FAILED(initResult))
        return false;

    ComPtr<IMFAttributes> attr;
    if (FAILED(MFCreateAttributes(&attr, 1)))
        return false;
#if defined(MF_SOURCE_READER_ENABLE_AUDIO_PROCESSING)
    attr->SetUINT32(MF_SOURCE_READER_ENABLE_AUDIO_PROCESSING, TRUE);
#elif defined(MF_READWRITE_ENABLE_AUDIO_PROCESSING)
    attr->SetUINT32(MF_READWRITE_ENABLE_AUDIO_PROCESSING, TRUE);
#endif

    ComPtr<IMFSourceReader> reader;
    std::wstring widePath = pathStringToWide(filePath);
    if (FAILED(MFCreateSourceReaderFromURL(widePath.c_str(), attr.Get(), &reader)))
        return false;

    ComPtr<IMFMediaType> type;
    if (FAILED(MFCreateMediaType(&type)))
        return false;
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    if (FAILED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, type.Get())))
        return false;

    ComPtr<IMFMediaType> curType;
    if (FAILED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &curType)))
        return false;
    UINT32 sr = 0;
    UINT32 ch = 0;
    if (FAILED(curType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr)) ||
        FAILED(curType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch)) ||
        sr == 0 || ch == 0 || ch > 64) {
        return false;
    }
    sampleRate = sr;
    numChannels = ch;

    LONGLONG duration = 0;
    PROPVARIANT var;
    if (SUCCEEDED(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var))) {
        duration = var.hVal.QuadPart;
        PropVariantClear(&var);
    }

    audioData.clear();
    while (true) {
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        if (FAILED(reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample)))
            break;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
            break;
        if (!sample)
            continue;
        ComPtr<IMFMediaBuffer> buf;
        if (FAILED(sample->ConvertToContiguousBuffer(&buf)))
            continue;
        BYTE* data;
        DWORD len;
        if (SUCCEEDED(buf->Lock(&data, nullptr, &len))) {
            const size_t sampleCount = len / sizeof(float);
            size_t samplesToCopy = sampleCount;
            if (maxFrames > 0) {
                const size_t maxSamples = static_cast<size_t>(maxFrames) * static_cast<size_t>(ch);
                if (audioData.size() >= maxSamples) {
                    buf->Unlock();
                    break;
                }
                samplesToCopy = std::min(samplesToCopy, maxSamples - audioData.size());
            }

            size_t prev = audioData.size();
            audioData.resize(prev + samplesToCopy);
            std::memcpy(audioData.data() + prev, data, samplesToCopy * sizeof(float));
            buf->Unlock();

            if (progressCallback && duration > 0) {
                LONGLONG timestamp = 0;
                sample->GetSampleTime(&timestamp);
                progressCallback(static_cast<float>(timestamp) / static_cast<float>(duration));
            }
            if (maxFrames > 0 && audioData.size() >= static_cast<size_t>(maxFrames) * static_cast<size_t>(ch)) {
                break;
            }
        }
    }
    if (progressCallback)
        progressCallback(1.0f);
    return !audioData.empty();
}
#endif
} // namespace

void forceStereo(std::vector<float>& buffer, uint32_t& channelCount) {
    if (channelCount == 2)
        return;
    if (channelCount == 0) {
        buffer.clear();
        channelCount = 1;
        return;
    }
    if (channelCount == 1) {
        std::vector<float> stereo(buffer.size() * 2);
        for (size_t i = 0; i < buffer.size(); ++i) {
            stereo[i * 2] = buffer[i];
            stereo[i * 2 + 1] = buffer[i];
        }
        buffer.swap(stereo);
        channelCount = 2;
    } else {
        std::vector<float> stereo;
        downmixToStereoImpl(buffer, channelCount, stereo);
        buffer.swap(stereo);
        channelCount = 2;
    }
}

bool loadWithMiniAudio(const std::string& filePath, std::vector<float>& audioData, uint32_t& sampleRate,
                       uint32_t& numChannels, std::function<void(float)> progressCallback) {
#if defined(AESTRA_USE_MINIAUDIO)
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder decoder;
#ifdef _WIN32
    if (ma_decoder_init_file_w(pathStringToWide(filePath).c_str(), &config, &decoder) != MA_SUCCESS) {
        const uint64_t fallbackMaxFrames = static_cast<uint64_t>(std::ceil(maxSeconds * 192000.0));
        if (!loadWithMediaFoundation(filePath, audioData, sampleRate, numChannels, nullptr, fallbackMaxFrames)) {
            return false;
        }
        forceStereo(audioData, numChannels);
        return true;
    }
#else
    if (ma_decoder_init_file(filePath.c_str(), &config, &decoder) != MA_SUCCESS)
        return false;
#endif
    ma_uint64 len = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &len) != MA_SUCCESS || len == 0 ||
        decoder.outputChannels == 0 || decoder.outputChannels > 64 || decoder.outputSampleRate == 0) {
        ma_decoder_uninit(&decoder);
        return false;
    }
    const ma_uint32 cappedChannels = std::max<ma_uint32>(decoder.outputChannels, 2);
    if (len > static_cast<ma_uint64>(std::numeric_limits<size_t>::max() / decoder.outputChannels) ||
        len > static_cast<ma_uint64>(std::numeric_limits<size_t>::max() / cappedChannels)) {
        ma_decoder_uninit(&decoder);
        return false;
    }
    const size_t sampleCount = static_cast<size_t>(len) * static_cast<size_t>(decoder.outputChannels);
    const size_t postStereoSampleCount = static_cast<size_t>(len) * static_cast<size_t>(cappedChannels);
    if (postStereoSampleCount > kMaxDecodedSamples) {
        ma_decoder_uninit(&decoder);
        return false;
    }
    audioData.resize(sampleCount);

    ma_uint64 totalRead = 0;
    const ma_uint64 chunkSize = 4096 * 4; // Read in chunks to report progress

    while (totalRead < len) {
        ma_uint64 toRead = std::min(chunkSize, len - totalRead);
        ma_uint64 framesRead = 0;
        if (ma_decoder_read_pcm_frames(&decoder, audioData.data() + (totalRead * decoder.outputChannels), toRead,
                                       &framesRead) != MA_SUCCESS)
            break;
        if (framesRead == 0)
            break;

        totalRead += framesRead;
        if (progressCallback && len > 0) {
            progressCallback(static_cast<float>(totalRead) / static_cast<float>(len));
        }
    }

    sampleRate = decoder.outputSampleRate;
    numChannels = decoder.outputChannels;
    ma_decoder_uninit(&decoder);
    if (totalRead != len)
        audioData.resize(static_cast<size_t>(totalRead) * numChannels);
    if (progressCallback)
        progressCallback(1.0f);
    return totalRead > 0;
#else
    return false;
#endif
}

bool decodeAudioFile(const std::string& filePath, std::vector<float>& audioData, uint32_t& sampleRate,
                     uint32_t& numChannels, std::function<void(float)> progressCallback) {
    std::string ext = filePath.substr(filePath.find_last_of('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    bool ok = false;
    if (ext == "wav") {
        ok = loadWav(filePath, audioData, sampleRate, numChannels);
        if (ok && progressCallback)
            progressCallback(1.0f);
    }

    if (!ok)
        ok = loadWithMiniAudio(filePath, audioData, sampleRate, numChannels, progressCallback);

#ifdef _WIN32
    if (!ok)
        ok = loadWithMediaFoundation(filePath, audioData, sampleRate, numChannels, progressCallback);
#endif

    if (ok)
        forceStereo(audioData, numChannels);
    return ok;
}

bool decodeAudioPreview(const std::string& filePath, std::vector<float>& audioData, uint32_t& sampleRate,
                        uint32_t& numChannels, double maxSeconds) {
#if defined(AESTRA_USE_MINIAUDIO)
    if (!std::isfinite(maxSeconds) || maxSeconds <= 0.0) {
        return false;
    }

    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, 0);
    ma_decoder decoder;
#ifdef _WIN32
    if (ma_decoder_init_file_w(pathStringToWide(filePath).c_str(), &config, &decoder) != MA_SUCCESS)
        return false;
#else
    if (ma_decoder_init_file(filePath.c_str(), &config, &decoder) != MA_SUCCESS)
        return false;
#endif

    if (decoder.outputSampleRate == 0 || decoder.outputChannels != 2) {
        ma_decoder_uninit(&decoder);
        return false;
    }

    ma_uint64 totalFrames = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames) != MA_SUCCESS || totalFrames == 0) {
        totalFrames = static_cast<ma_uint64>(std::ceil(maxSeconds * static_cast<double>(decoder.outputSampleRate)));
    }

    const ma_uint64 maxFrames = static_cast<ma_uint64>(std::ceil(maxSeconds * static_cast<double>(decoder.outputSampleRate)));
    const ma_uint64 framesToRead = std::min<ma_uint64>(totalFrames, maxFrames);
    if (framesToRead == 0 ||
        framesToRead > static_cast<ma_uint64>(std::numeric_limits<size_t>::max() / decoder.outputChannels)) {
        ma_decoder_uninit(&decoder);
        return false;
    }
    audioData.resize(static_cast<size_t>(framesToRead) * static_cast<size_t>(decoder.outputChannels));

    ma_uint64 framesRead = 0;
    if (ma_decoder_read_pcm_frames(&decoder, audioData.data(), framesToRead, &framesRead) != MA_SUCCESS) {
        ma_decoder_uninit(&decoder);
        return false;
    }

    sampleRate = decoder.outputSampleRate;
    numChannels = decoder.outputChannels;
    ma_decoder_uninit(&decoder);

    if (framesRead == 0) {
        audioData.clear();
        return false;
    }

    audioData.resize(static_cast<size_t>(framesRead) * numChannels);
    return true;
#else
    (void) maxSeconds;
    return decodeAudioFile(filePath, audioData, sampleRate, numChannels, nullptr);
#endif
}

} // namespace Audio
} // namespace Aestra
