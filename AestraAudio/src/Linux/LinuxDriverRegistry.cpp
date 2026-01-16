#include "../../include/AudioPlatformRegistry.h"
#include "RtAudioDriver.h"
#include <vector>
#include <string>

namespace AestraAudio {

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

} // namespace AestraAudio
