// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Nomad {
namespace Audio {

/**
 * @brief Optional miniaudio-based decoder for compressed formats (MP3/FLAC/OGG/etc).
 *
 * This is compiled only when NOMAD_USE_MINIAUDIO is defined and `miniaudio.h`
 * is available on the include path. Otherwise it returns false and callers
 * should fall back to platform decoders (Media Foundation on Windows).
 *
 * @param filePath Path to the audio file to decode
 * @param audioData Output parameter: decoded float32 audio data, interleaved stereo (LRLRLR...)
 *                  Data is normalized to [-1.0, 1.0] range and ready for audio processing
 * @param sampleRate Output parameter: sample rate in Hz (e.g., 44100, 48000)
 * @param numChannels Output parameter: number of channels (1=mono, 2=stereo, etc.)
 * @return true if successful, false if file could not be decoded
 *
 * @warning The audioData vector is resized to contain the decoded samples.
 *          Previous contents are lost on successful decode.
 * @warning Callers must handle the case where NOMAD_USE_MINIAUDIO is not defined,
 *          in which case this function always returns false.
 */
[[nodiscard]] bool loadWithMiniAudio(const std::string& filePath,
                                     std::vector<float>& audioData,
                                     uint32_t& sampleRate,
                                     uint32_t& numChannels);

} // namespace Audio
} // namespace Nomad

