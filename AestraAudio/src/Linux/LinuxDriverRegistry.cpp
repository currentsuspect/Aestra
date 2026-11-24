// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "../../include/Drivers/AudioPlatformRegistry.h"

#include "../DummyAudioDriver.h"
#include "RtAudioDriver.h"
#include "AestraLog.h"

#include <memory>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

namespace {
// The real driver is missing: say so in the log and hand the reason to the manager, which reports it to the
// app. The Dummy driver is added below either way, so without this the user gets silence and no explanation.
void reportRealDriverMissing(AudioDeviceManager& manager, const std::string& reason) {
    Aestra::Log::error("[LinuxDriverRegistry] No real audio driver: " + reason + ". Falling back to the silent driver.");
    manager.setDriverRegistrationFailure(reason);
}
} // namespace

void RegisterPlatformDrivers(AudioDeviceManager& manager) {
    try {
        auto rtAudio = std::make_unique<RtAudioDriver>();
        if (rtAudio->isAvailable()) {
            manager.addDriver(std::move(rtAudio));
        } else {
            std::string why = rtAudio->getErrorMessage();
            if (why.empty()) {
                why = "no usable sound system was found";
            }
            reportRealDriverMissing(manager, "RtAudio is unavailable: " + why);
        }
    } catch (const std::exception& e) {
        reportRealDriverMissing(manager, std::string("RtAudio failed to start: ") + e.what());
    } catch (...) {
        reportRealDriverMissing(manager, "RtAudio failed to start: unknown error");
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
