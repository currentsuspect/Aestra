// Stub implementation for when ALSA is not available
#include "Core/AudioDeviceManager.h"
#include "Drivers/AudioDriverRegistry.h"
#include "DummyAudioDriver.h"

namespace Aestra {
namespace Audio {

// Weak symbol - Linux/GCC/Clang implementation; MSVC skips this entirely
// (Windows has WindowsDriverRegistry.cpp providing RegisterPlatformDrivers)
#ifdef __GNUC__
__attribute__((weak)) void RegisterPlatformDrivers(AudioDeviceManager& manager);
__attribute__((weak)) void RegisterPlatformDrivers(AudioDeviceManager& manager) {
    // Fallback stub - no audio drivers available without ALSA
    manager.addDriver(std::make_unique<DummyAudioDriver>());
}
#endif

} // namespace Audio
} // namespace Aestra