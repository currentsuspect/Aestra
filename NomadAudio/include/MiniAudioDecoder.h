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
 */
bool loadWithMiniAudio(const std::string& filePath,
                       std::vector<float>& audioData,
                       uint32_t& sampleRate,
                       uint32_t& numChannels);

} // namespace Audio
} // namespace Nomad

