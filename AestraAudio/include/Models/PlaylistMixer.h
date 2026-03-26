#pragma once
#include "../DSP/ClipResampler.h"

namespace Aestra {
namespace Audio {

class PlaylistMixer {
public:
    static void setResamplingQuality(ClipResamplingQuality quality) {
        s_resamplingQuality = quality;
    }

    static ClipResamplingQuality getResamplingQuality() {
        return s_resamplingQuality;
    }

private:
    static inline ClipResamplingQuality s_resamplingQuality = ClipResamplingQuality::Standard;
};

} // namespace Audio
} // namespace Aestra
