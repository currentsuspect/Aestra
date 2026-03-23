// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AudioDeviceManager.h"

namespace Aestra {
namespace Audio {

/**
 * @brief Registers platform-specific audio drivers with the device manager.
 *
 * This function is implemented in the platform-specific backend (e.g., AestraAudioWin).
 * On non-Windows platforms, it might be a no-op or register other drivers.
 *
 * @param manager The manager to register drivers with.
 */
void RegisterPlatformDrivers(AudioDeviceManager& manager);

} // namespace Audio
} // namespace Aestra
