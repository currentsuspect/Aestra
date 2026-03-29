// Stub implementation for when ALSA is not available
#include "Core/AudioDeviceManager.h"
#include "Drivers/AudioDriverRegistry.h"

namespace Aestra {
namespace Audio {

// Weak symbol - Linux/GCC/Clang implementation; MSVC skips this entirely
// (Windows has WindowsDriverRegistry.cpp providing RegisterPlatformDrivers)
#ifdef __GNUC__
__attribute__((weak)) void RegisterPlatformDrivers(AudioDeviceManager& manager) {
    // No-op stub - no audio drivers available without ALSA
    (void)manager;
}
#endif

} // namespace Audio
} // namespace Aestra