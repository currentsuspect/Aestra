// © 2025 Aestra Studios — All Rights Reserved.
// ReverbSIMD definitions.

#include "DSP/ReverbSIMD.h"

#include <mutex>

namespace Aestra {
namespace Audio {
namespace DSP {
namespace ReverbSIMD {

bool g_forceScalarFallback = false;
bool g_forceLinearInterpolation = false;

bool g_useAVX2 = false;
bool g_useSSE41 = false;

namespace {
// One-time gate. Without it these writes are a data race whenever a second
// instance is prepared while a first is already processing: the write is
// concurrent with processFDNSample()'s read, and writing the same value is
// still a race in C++. Making the assignment happen exactly once, ever, means
// the flags are effectively immutable after first use and the audio thread can
// keep reading them unsynchronized.
std::once_flag g_resolveOnce;
} // namespace

void resolveSimdCapabilities() noexcept {
    std::call_once(g_resolveOnce, []() noexcept {
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
    });
}

} // namespace ReverbSIMD
} // namespace DSP
} // namespace Audio
} // namespace Aestra
