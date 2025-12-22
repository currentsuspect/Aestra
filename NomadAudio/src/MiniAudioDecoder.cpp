// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "MiniAudioDecoder.h"
#include "PathUtils.h"

#if defined(NOMAD_USE_MINIAUDIO)
// Miniaudio is header-only. Provide the implementation unit here when enabled.
// Expectation: user vendors `miniaudio.h` under NomadAudio/External/miniaudio or similar
// and adds that directory to include paths.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DEVICE_IO
#include "miniaudio.h"

namespace Nomad {
namespace Audio {

bool loadWithMiniAudio(const std::string& filePath,
                       std::vector<float>& audioData,
                       uint32_t& sampleRate,
                       uint32_t& numChannels) {
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder decoder;
    
    // Use wide-string API on Windows for proper Unicode path support
#ifdef _WIN32
    std::wstring widePath = pathStringToWide(filePath);
    if (ma_decoder_init_file_w(widePath.c_str(), &config, &decoder) != MA_SUCCESS) {
        return false;
    }
#else
    if (ma_decoder_init_file(filePath.c_str(), &config, &decoder) != MA_SUCCESS) {
        return false;
    }
#endif

    ma_uint64 totalFrames = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames) != MA_SUCCESS) {
        ma_decoder_uninit(&decoder);
        return false;
    }

    const ma_uint32 ch = decoder.outputChannels;
    if (ch == 0 || totalFrames == 0) {
        ma_decoder_uninit(&decoder);
        return false;
    }

    audioData.resize(static_cast<size_t>(totalFrames) * ch);
    ma_uint64 framesRead = ma_decoder_read_pcm_frames(&decoder,
                                                      audioData.data(),
                                                      totalFrames,
                                                      nullptr);
    ma_decoder_uninit(&decoder);

    if (framesRead == 0) {
        audioData.clear();
        return false;
    }

    // Trim if decoder returned fewer frames than reported.
    if (framesRead != totalFrames) {
        audioData.resize(static_cast<size_t>(framesRead) * ch);
    }

    sampleRate = decoder.outputSampleRate;
    numChannels = ch;
    return true;
}

} // namespace Audio
} // namespace Nomad

#else

namespace Nomad {
namespace Audio {

bool loadWithMiniAudio(const std::string&,
                       std::vector<float>&,
                       uint32_t&,
                       uint32_t&) {
    return false;
}

} // namespace Audio
} // namespace Nomad

#endif

