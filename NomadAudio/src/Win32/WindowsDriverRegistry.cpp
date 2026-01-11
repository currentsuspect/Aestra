// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "../../include/AudioDriverRegistry.h"
#include "../../include/AudioDeviceManager.h"
#include "WASAPIExclusiveDriver.h"
#include "WASAPISharedDriver.h"
#include "../../include/ASIODriver.h"
#include "RtAudioBackend.h"
#include "../DummyAudioDriver.h"
#include <iostream>

namespace Nomad {
namespace Audio {

void RegisterPlatformDrivers(AudioDeviceManager& manager) {
    std::cout << "[AudioDriverRegistry] Registering Windows drivers..." << std::endl;
    
    // Register ASIO Drivers (Phase 6)
    try {
        auto asio = std::make_unique<ASIODriver>();
        if (asio->isAvailable()) {
            manager.addDriver(std::move(asio));
        }
    } catch (const std::exception& e) {
        std::cerr << "[AudioDriverRegistry] ASIO exception: " << e.what() << std::endl;
    }

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
        if (rtaudio->initialize()) {
             manager.addDriver(std::move(rtaudio));
        }
    } catch (...) {
        std::cerr << "[AudioDriverRegistry] RtAudio exception" << std::endl;
    }

    // Register Dummy Driver (Last Resort / Safety)
    try {
        manager.addDriver(std::make_unique<DummyAudioDriver>());
        std::cout << "[AudioDriverRegistry] Dummy driver registered (Safety fallback active)" << std::endl;
    } catch (...) {}
}

} // namespace Audio
} // namespace Nomad
