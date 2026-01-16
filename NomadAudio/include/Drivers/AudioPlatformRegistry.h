#pragma once
#include "AudioDeviceManager.h"
#include <vector>
#include <string>
#include <memory>

namespace NomadAudio {

    /**
     * @brief Structure containing information about the platform's audio capabilities
     * and backend availability.
     */
    struct PlatformAudioInfo {
        std::vector<std::string> availableBackends;
        std::string recommendedBackend;
        bool requiresRootForRT = false;
        std::string warningMessage;
    };

    /**
     * @brief Called during AudioDeviceManager initialization to register platform-specific
     * audio drivers (WASAPI, ASIO, ALSA, PulseAudio, etc.).
     * 
     * This function must be implemented by each platform's audio layer.
     * 
     * @param manager Reference to the AudioDeviceManager instance
     */
    void RegisterPlatformDrivers(AudioDeviceManager& manager);

    /**
     * @brief Query the platform for available audio backends and capabilities
     * before driver registration.
     * 
     * @return PlatformAudioInfo struct with backend details
     */
    PlatformAudioInfo GetPlatformAudioInfo();

} // namespace NomadAudio
