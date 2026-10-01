// © 2025 Aestra Studios — All Rights Reserved.
// ReverbSIMD definitions.

#include "DSP/ReverbSIMD.h"

namespace Aestra {
namespace Audio {
namespace DSP {
namespace ReverbSIMD {

bool g_forceScalarFallback = false;
bool g_forceLinearInterpolation = false;

bool g_useAVX2 = false;
bool g_useSSE41 = false;

void resolveSimdCapabilities() noexcept {
#ifdef AESTRA_REVERB_HAS_AVX2
    const auto& cpu = Aestra::Core::CPUDetection::get();
    g_useAVX2 = cpu.hasAVX2() && cpu.hasFMA();
#else
    g_useAVX2 = false;
#endif
#ifdef AESTRA_REVERB_HAS_SSE
    g_useSSE41 = Aestra::Core::CPUDetection::get().hasSSE41();
#else
    g_useSSE41 = false;
#endif
}

} // namespace ReverbSIMD
} // namespace DSP
} // namespace Audio
} // namespace Aestra
