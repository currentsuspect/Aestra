// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "../../include/Drivers/AudioPlatformRegistry.h"

#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

// STUB: Phase 2 will register RtAudioDriver once it conforms to IAudioDriver interface
void RegisterPlatformDrivers(AudioDeviceManager& /*manager*/) {
    // RtAudioDriver exists but uses legacy interface — not yet compatible with IAudioDriver
}

PlatformAudioInfo GetPlatformAudioInfo() {
    PlatformAudioInfo info;
    info.availableBackends = {"PulseAudio", "ALSA", "JACK"};
    info.recommendedBackend = "PulseAudio";
    info.requiresRootForRT = true;
    return info;
}

} // namespace Audio
} // namespace Aestra
