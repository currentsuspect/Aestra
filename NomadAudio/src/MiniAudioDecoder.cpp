#if defined(NOMAD_USE_MINIAUDIO)
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DEVICE_IO
#include "miniaudio.h"
#endif

#include "MiniAudioDecoder.h"
#include "PathUtils.h"
#include "NomadLog.h"
#include <fstream>
#include <algorithm>
#include <cstring>
#include <cmath>

#ifdef _WIN32
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <mutex>
#endif

namespace Nomad {
namespace Audio {

namespace {
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

    bool loadWav(const std::string& filePath, std::vector<float>& audioData, uint32_t& sampleRate, uint32_t& numChannels) {
        std::ifstream file(makeUnicodePath(filePath), std::ios::binary);
        if (!file) return false;

        char riffId[4], waveId[4];
        uint32_t riffSize = 0;
        if (!file.read(riffId, 4) || !file.read(reinterpret_cast<char*>(&riffSize), 4) || !file.read(waveId, 4)) return false;
        if (std::strncmp(riffId, "RIFF", 4) != 0 || std::strncmp(waveId, "WAVE", 4) != 0) return false;

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
            if (!file.read(chunkId, 4) || !file.read(reinterpret_cast<char*>(&chunkSize), 4)) break;

            if (std::strncmp(chunkId, "fmt ", 4) == 0) {
                fmtFound = true;
                if (chunkSize < 16) return false;
                file.read(reinterpret_cast<char*>(&audioFormat), sizeof(uint16_t));
                file.read(reinterpret_cast<char*>(&channelCount), sizeof(uint16_t));
                file.read(reinterpret_cast<char*>(&sr), sizeof(uint32_t));
                file.seekg(6, std::ios::cur); // Skip byteRate, blockAlign
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

        file.seekg(dataPos);
        size_t samplesCount = dataSize / (bitsPerSample / 8);
        audioData.resize(samplesCount);

        if (bitsPerSample == 16) {
            std::vector<int16_t> raw(samplesCount);
            file.read(reinterpret_cast<char*>(raw.data()), dataSize);
            for (size_t i = 0; i < samplesCount; ++i) audioData[i] = raw[i] / 32768.0f;
        } else if (bitsPerSample == 24) {
            std::vector<uint8_t> raw(dataSize);
            file.read(reinterpret_cast<char*>(raw.data()), dataSize);
            for (size_t i = 0; i < samplesCount; ++i) {
                uint32_t s24 = raw[i * 3] | (raw[i * 3 + 1] << 8) | (raw[i * 3 + 2] << 16);
                if (s24 & 0x800000) s24 |= 0xFF000000;
                audioData[i] = static_cast<int32_t>(s24) / 8388608.0f;
            }
        } else if (bitsPerSample == 32) {
            file.read(reinterpret_cast<char*>(audioData.data()), dataSize);
        } else {
            return false;
        }

        sampleRate = sr;
        numChannels = channelCount;
        return true;
    }

#ifdef _WIN32
    bool loadWithMediaFoundation(const std::string& filePath, std::vector<float>& audioData, uint32_t& sampleRate, uint32_t& numChannels) {
        using Microsoft::WRL::ComPtr;
        static std::once_flag initFlag;
        static HRESULT initResult = E_FAIL;
        std::call_once(initFlag, []() { initResult = MFStartup(MF_VERSION, MFSTARTUP_LITE); });
        if (FAILED(initResult)) return false;

        ComPtr<IMFAttributes> attr;
        if (FAILED(MFCreateAttributes(&attr, 1))) return false;
#if defined(MF_SOURCE_READER_ENABLE_AUDIO_PROCESSING)
        attr->SetUINT32(MF_SOURCE_READER_ENABLE_AUDIO_PROCESSING, TRUE);
#elif defined(MF_READWRITE_ENABLE_AUDIO_PROCESSING)
        attr->SetUINT32(MF_READWRITE_ENABLE_AUDIO_PROCESSING, TRUE);
#endif

        ComPtr<IMFSourceReader> reader;
        std::wstring widePath = pathStringToWide(filePath);
        if (FAILED(MFCreateSourceReaderFromURL(widePath.c_str(), attr.Get(), &reader))) return false;

        ComPtr<IMFMediaType> type;
        if (FAILED(MFCreateMediaType(&type))) return false;
        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
        if (FAILED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, type.Get()))) return false;

        ComPtr<IMFMediaType> curType;
        if (FAILED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &curType))) return false;
        UINT32 sr, ch;
        curType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr);
        curType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch);
        sampleRate = sr; numChannels = ch;

        audioData.clear();
        while (true) {
            DWORD flags = 0;
            ComPtr<IMFSample> sample;
            if (FAILED(reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample))) break;
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
            if (!sample) continue;
            ComPtr<IMFMediaBuffer> buf;
            if (FAILED(sample->ConvertToContiguousBuffer(&buf))) continue;
            BYTE* data; DWORD len;
            if (SUCCEEDED(buf->Lock(&data, nullptr, &len))) {
                size_t prev = audioData.size();
                audioData.resize(prev + (len / sizeof(float)));
                std::memcpy(audioData.data() + prev, data, len);
                buf->Unlock();
            }
        }
        return !audioData.empty();
    }
#endif
} // namespace

void forceStereo(std::vector<float>& buffer, uint32_t& channelCount) {
    if (channelCount == 2) return;
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

bool loadWithMiniAudio(const std::string& filePath, std::vector<float>& audioData, uint32_t& sampleRate, uint32_t& numChannels) {
#if defined(NOMAD_USE_MINIAUDIO)
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder decoder;
#ifdef _WIN32
    if (ma_decoder_init_file_w(pathStringToWide(filePath).c_str(), &config, &decoder) != MA_SUCCESS) return false;
#else
    if (ma_decoder_init_file(filePath.c_str(), &config, &decoder) != MA_SUCCESS) return false;
#endif
    ma_uint64 len;
    ma_decoder_get_length_in_pcm_frames(&decoder, &len);
    audioData.resize(static_cast<size_t>(len) * decoder.outputChannels);
    ma_uint64 read = ma_decoder_read_pcm_frames(&decoder, audioData.data(), len, nullptr);
    sampleRate = decoder.outputSampleRate;
    numChannels = decoder.outputChannels;
    ma_decoder_uninit(&decoder);
    if (read != len) audioData.resize(static_cast<size_t>(read) * numChannels);
    return read > 0;
#else
    return false;
#endif
}

bool decodeAudioFile(const std::string& filePath, std::vector<float>& audioData, uint32_t& sampleRate, uint32_t& numChannels) {
    std::string ext = filePath.substr(filePath.find_last_of('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    bool ok = false;
    if (ext == "wav") ok = loadWav(filePath, audioData, sampleRate, numChannels);
    
    if (!ok) ok = loadWithMiniAudio(filePath, audioData, sampleRate, numChannels);
    
#ifdef _WIN32
    if (!ok) ok = loadWithMediaFoundation(filePath, audioData, sampleRate, numChannels);
#endif

    if (ok) forceStereo(audioData, numChannels);
    return ok;
}

} // namespace Audio
} // namespace Nomad

