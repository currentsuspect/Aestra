// Stub implementation for when ALSA is not available
#include "Core/AudioDeviceManager.h"
#include "Drivers/AudioDriverRegistry.h"

namespace Aestra {
namespace Audio {

// Weak symbol - Linux implementation overrides this if linked
__attribute__((weak)) void RegisterPlatformDrivers(AudioDeviceManager& manager) {
    // No-op stub - no audio drivers available without ALSA
    (void)manager;
}

} // namespace Audio
} // namespace Aestra