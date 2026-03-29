// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "../../include/Drivers/AudioPlatformRegistry.h"

#include "../DummyAudioDriver.h"
#include "RtAudioDriver.h"

#include <memory>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

void RegisterPlatformDrivers(AudioDeviceManager& manager) {
    try {
        auto rtAudio = std::make_unique<RtAudioDriver>();
        if (rtAudio->isAvailable()) {
            manager.addDriver(std::move(rtAudio));
        }
    } catch (const std::exception&) {
    } catch (...) {
    }
    manager.addDriver(std::make_unique<DummyAudioDriver>());
}

PlatformAudioInfo GetPlatformAudioInfo() {
    PlatformAudioInfo info;
    info.availableBackends = {"ALSA", "PulseAudio", "JACK"};
    info.recommendedBackend = "ALSA";
    info.requiresRootForRT = false;
    return info;
}

} // namespace Audio
} // namespace Aestra
