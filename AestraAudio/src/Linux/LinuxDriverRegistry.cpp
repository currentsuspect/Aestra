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
    fprintf(stderr, "[LinuxDriverRegistry] RegisterPlatformDrivers called\n");
    fflush(stderr);
    try {
        fprintf(stderr, "[LinuxDriverRegistry] Creating RtAudioDriver\n");
        fflush(stderr);
        auto rtAudio = std::make_unique<RtAudioDriver>();
        fprintf(stderr, "[LinuxDriverRegistry] RtAudioDriver created, isAvailable=%d\n", rtAudio->isAvailable() ? 1 : 0);
        fflush(stderr);
        if (rtAudio->isAvailable()) {
            fprintf(stderr, "[LinuxDriverRegistry] Adding RtAudioDriver to manager\n");
            fflush(stderr);
            manager.addDriver(std::move(rtAudio));
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "[LinuxDriverRegistry] Exception: %s\n", e.what());
        fflush(stderr);
    } catch (...) {
        fprintf(stderr, "[LinuxDriverRegistry] Unknown exception\n");
        fflush(stderr);
    }

    fprintf(stderr, "[LinuxDriverRegistry] Adding DummyAudioDriver\n");
    fflush(stderr);
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
