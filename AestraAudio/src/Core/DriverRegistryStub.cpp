// Stub implementation for when ALSA is not available
#include "Drivers/AudioDriverRegistry.h"
#include "Core/AudioDeviceManager.h"

namespace Aestra {
namespace Audio {

void RegisterPlatformDrivers(AudioDeviceManager& manager) {
    // No-op stub - no audio drivers available without ALSA
    (void)manager;
}

} // namespace Audio
} // namespace Aestra