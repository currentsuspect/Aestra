#include "../../include/Drivers/AudioPlatformRegistry.h"
#include "RtAudioDriver.h"

#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

void RegisterPlatformDrivers(AudioDeviceManager& manager) {
    manager.addDriver(std::make_unique<RtAudioDriver>());
}

PlatformAudioInfo GetPlatformAudioInfo() {
    PlatformAudioInfo info;
    info.availableBackends = {"PulseAudio", "ALSA", "JACK"}; // Generic list for now
    info.recommendedBackend = "PulseAudio";
    info.requiresRootForRT = true;
    return info;
}

} // namespace Audio
} // namespace Aestra
