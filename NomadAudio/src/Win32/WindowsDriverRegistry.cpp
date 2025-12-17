// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "../../include/AudioDriverRegistry.h"
#include "../../include/AudioDeviceManager.h"
#include "WASAPIExclusiveDriver.h"
#include "WASAPISharedDriver.h"
#include "RtAudioBackend.h"
#include <iostream>

namespace Nomad {
namespace Audio {

void RegisterPlatformDrivers(AudioDeviceManager& manager) {
    std::cout << "[AudioDriverRegistry] Registering Windows drivers..." << std::endl;
    
    // Register WASAPI Exclusive
    try {
        auto exclusive = std::make_unique<WASAPIExclusiveDriver>();
        if (exclusive->initialize()) {
            manager.addDriver(std::move(exclusive));
        } else {
             std::cout << "[AudioDriverRegistry] Failed to initialize WASAPI Exclusive" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[AudioDriverRegistry] WASAPI Exclusive exception: " << e.what() << std::endl;
    }

    // Register WASAPI Shared
    try {
        auto shared = std::make_unique<WASAPISharedDriver>();
        if (shared->initialize()) {
            manager.addDriver(std::move(shared));
        } else {
             std::cout << "[AudioDriverRegistry] Failed to initialize WASAPI Shared" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[AudioDriverRegistry] WASAPI Shared exception: " << e.what() << std::endl;
    }
    
    // Register RtAudio (Fallback)
    try {
        auto rtaudio = std::make_unique<RtAudioBackend>();
        // RtAudioBackend might arguably not need explicit initialize? 
        // Checking existing code, it doesn't have initialize() method in constructor usage previously.
        // But NativeAudioDriver interface has initialize().
        if (rtaudio->initialize()) {
             manager.addDriver(std::move(rtaudio));
        }
    } catch (...) {
        std::cerr << "[AudioDriverRegistry] RtAudio exception" << std::endl;
    }
}

} // namespace Audio
} // namespace Nomad
