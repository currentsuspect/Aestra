// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// DriverRegistrationFailureTest — when the platform's real audio driver is missing at startup, the manager keeps
// the cause and reports it with the fallback, so the app can tell the user why there is no sound (#1274).
//
// On Linux, RegisterPlatformDrivers used to swallow every RtAudio failure in an empty catch and then add the
// silent Dummy driver anyway. The app came up with no sound, no log line and nothing to read. Registration
// cannot be forced to fail here, so this drives the manager the way the registry does: record the cause, add
// the Dummy driver, open a stream.

#include "AestraAudio.h"
#include "DummyAudioDriver.h"

#include <iostream>
#include <memory>
#include <string>

using namespace Aestra::Audio;

namespace {
int g_failures = 0;

void check(bool condition, const std::string& label) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << label << "\n";
    if (!condition) {
        ++g_failures;
    }
}

int silentCallback(float* out, const float*, uint32_t frames, double, void*) {
    for (uint32_t i = 0; i < frames * 2; ++i) {
        out[i] = 0.0f;
    }
    return 0;
}

// A manager with only the silent driver, as the Linux registry leaves it when RtAudio is missing.
std::unique_ptr<AudioDeviceManager> managerWithOnlyTheSilentDriver() {
    auto manager = std::make_unique<AudioDeviceManager>();
    manager->initialize(false); // no platform drivers: only what the test injects
    manager->addDriver(std::make_unique<DummyAudioDriver>());
    return manager;
}

AudioStreamConfig silentConfig(AudioDeviceManager& manager) {
    AudioStreamConfig config;
    config.sampleRate = 48000;
    config.bufferSize = 256;
    config.numOutputChannels = 2;
    for (const auto& device : manager.getDevices()) {
        if (device.maxOutputChannels > 0) {
            config.deviceId = device.id;
            break;
        }
    }
    return config;
}
} // namespace

int main() {
    const std::string cause = "RtAudio failed to start: no sound system found";

    {
        auto manager = managerWithOnlyTheSilentDriver();
        check(manager->getDriverRegistrationFailure().empty(), "nothing recorded before a failure is reported");

        manager->setDriverRegistrationFailure(cause);
        check(manager->getDriverRegistrationFailure() == cause, "the registration failure is kept as given");

        const bool opened = manager->openStream(silentConfig(*manager), silentCallback, nullptr);
        check(opened, "the silent driver still opens, so the app keeps running");
        check(manager->getActiveDriverType() == AudioDriverType::DUMMY, "the active driver is the silent one");
        check(manager->getFallbackReason().find(cause) != std::string::npos,
              "the fallback reason carries the cause, not just 'unavailable'");
        manager->closeStream();
    }

    {
        // Negative control: without a recorded failure the reason must not invent one.
        auto manager = managerWithOnlyTheSilentDriver();
        const bool opened = manager->openStream(silentConfig(*manager), silentCallback, nullptr);
        check(opened, "control: the silent driver opens");
        check(manager->getFallbackReason().find("RtAudio") == std::string::npos,
              "control: no cause appears when none was recorded");
        manager->closeStream();
    }

    {
        // A second initialization must not inherit the first one's cause (CodeRabbit, #1491). The cause is recorded
        // while platform drivers register, so it is cleared just before they register again. Whether real
        // registration succeeds here depends on the machine; either way the stale text must be gone, and so must
        // the fallback reason that was derived from it.
        const std::string stale = "STALE-CAUSE-FROM-A-PREVIOUS-INITIALIZE";
        auto manager = managerWithOnlyTheSilentDriver();
        manager->setDriverRegistrationFailure(stale);
        check(manager->openStream(silentConfig(*manager), silentCallback, nullptr), "stale: the silent driver opens");
        check(manager->getFallbackReason().find(stale) != std::string::npos,
              "stale: the first fallback reason carries the first cause (premise)");
        manager->closeStream();

        manager->shutdown();
        manager->initialize(true);
        check(manager->getDriverRegistrationFailure().find(stale) == std::string::npos,
              "a new initialization does not report the previous cause");
        check(manager->getFallbackReason().find(stale) == std::string::npos,
              "a new initialization does not keep a fallback reason built from the previous cause");
        manager->shutdown();
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "driver registration failure passed\n";
    return 0;
}
