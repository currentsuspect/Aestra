// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "../include/NomadPlatform.h"
#include "../../NomadCore/include/NomadConfig.h"
#include "../../NomadCore/include/NomadLog.h"

// Platform-specific includes
#if NOMAD_PLATFORM_WINDOWS
    #include "Win32/PlatformWindowWin32.h"
    #include "Win32/PlatformUtilsWin32.h"
    #include "Win32/PlatformDPIWin32.h"
#elif NOMAD_PLATFORM_LINUX
    // TODO: Linux implementation
    #error "Linux platform not yet implemented"
#elif NOMAD_PLATFORM_MACOS
    // TODO: macOS implementation
    #error "macOS platform not yet implemented"
#endif

namespace Nomad {

// Static members
IPlatformUtils* Platform::s_utils = nullptr;

// =============================================================================
// Platform Factory
// =============================================================================

IPlatformWindow* Platform::createWindow() {
#if NOMAD_PLATFORM_WINDOWS
    return new PlatformWindowWin32();
#elif NOMAD_PLATFORM_LINUX
    return nullptr;  // TODO
#elif NOMAD_PLATFORM_MACOS
    return nullptr;  // TODO
#endif
}

IPlatformUtils* Platform::getUtils() {
    if (!s_utils) {
        NOMAD_LOG_ERROR("Platform not initialized! Call Platform::initialize() first.");
    }
    return s_utils;
}

bool Platform::initialize() {
    if (s_utils) {
        return true;  // Already initialized
    }

#if NOMAD_PLATFORM_WINDOWS
    // Initialize DPI awareness first (must be done before creating windows)
    PlatformDPI::initialize();
    
    s_utils = new PlatformUtilsWin32();
    NOMAD_LOG_INFO("Windows platform initialized");
#elif NOMAD_PLATFORM_LINUX
    // TODO: Linux initialization
    NOMAD_LOG_ERROR("Linux platform not yet implemented");
    return false;
#elif NOMAD_PLATFORM_MACOS
    // TODO: macOS initialization
    NOMAD_LOG_ERROR("macOS platform not yet implemented");
    return false;
#endif

    return s_utils != nullptr;
}

void Platform::shutdown() {
    if (s_utils) {
        delete s_utils;
        s_utils = nullptr;
        NOMAD_LOG_INFO("Platform shutdown");
    }
}

} // namespace Nomad
