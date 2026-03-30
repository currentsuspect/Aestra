// Fallback registry implementation for builds without a native backend.
#include "Core/AudioDeviceManager.h"
#include "Drivers/AudioDriverRegistry.h"
#include "../DummyAudioDriver.h"

namespace Aestra {
namespace Audio {

// Weak symbol - Linux/GCC/Clang implementation; MSVC skips this entirely
// (Windows has WindowsDriverRegistry.cpp providing RegisterPlatformDrivers)
#ifdef __GNUC__
__attribute__((weak)) void RegisterPlatformDrivers(AudioDeviceManager& manager) {
    // Keep the engine usable even when no native platform backend was built.
    manager.addDriver(std::make_unique<DummyAudioDriver>());
}
#endif

} // namespace Audio
} // namespace Aestra
