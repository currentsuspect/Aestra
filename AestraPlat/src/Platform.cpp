// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "../include/AestraPlatform.h"
#include "../../AestraCore/include/AestraConfig.h"
#include "../../AestraCore/include/AestraLog.h"

// Platform-specific includes
#if AESTRA_PLATFORM_WINDOWS
    #include "Win32/PlatformWindowWin32.h"
    #include "Win32/PlatformUtilsWin32.h"
    #include "Win32/PlatformDPIWin32.h"
#elif AESTRA_PLATFORM_LINUX
    #ifdef AESTRA_HAS_SDL2
    #include "Linux/PlatformWindowLinux.h"
    #include "Linux/PlatformUtilsLinux.h"
    #include <SDL2/SDL.h>
    #endif
#elif AESTRA_PLATFORM_MACOS
    // TODO: macOS implementation
    #error "macOS platform not yet implemented"
#endif

namespace Aestra {

// Static members
IPlatformUtils* Platform::s_utils = nullptr;

// =============================================================================
// Platform Factory
// =============================================================================

IPlatformWindow* Platform::createWindow() {
#if AESTRA_PLATFORM_WINDOWS
    return new PlatformWindowWin32();
#elif AESTRA_PLATFORM_LINUX
    #ifdef AESTRA_HAS_SDL2
    return new PlatformWindowLinux();
    #else
    return nullptr;
    #endif
#elif AESTRA_PLATFORM_MACOS
    return nullptr;  // TODO
#endif
}

IPlatformUtils* Platform::getUtils() {
    if (!s_utils) {
        AESTRA_LOG_ERROR("Platform not initialized! Call Platform::initialize() first.");
    }
    return s_utils;
}

bool Platform::initialize() {
    if (s_utils) {
        return true;  // Already initialized
    }

#if AESTRA_PLATFORM_WINDOWS
    // Initialize DPI awareness first (must be done before creating windows)
    PlatformDPI::initialize();
    
    s_utils = new PlatformUtilsWin32();
    AESTRA_LOG_INFO("Windows platform initialized");
#elif AESTRA_PLATFORM_LINUX
    #ifdef AESTRA_HAS_SDL2
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0) {
         AESTRA_LOG_ERROR("SDL_Init failed: " << SDL_GetError());
         return false;
    }
    
    s_utils = new PlatformUtilsLinux();
    AESTRA_LOG_INFO("Linux platform initialized (SDL2)");
    #else
    AESTRA_LOG_ERROR("Linux platform not supported without SDL2");
    return false;
    #endif
#elif AESTRA_PLATFORM_MACOS
    // TODO: macOS initialization
    AESTRA_LOG_ERROR("macOS platform not yet implemented");
    return false;
#endif

    return s_utils != nullptr;
}

void Platform::shutdown() {
    if (s_utils) {
        delete s_utils;
        s_utils = nullptr;
        #if AESTRA_PLATFORM_LINUX && defined(AESTRA_HAS_SDL2)
        SDL_Quit();
        #endif
        AESTRA_LOG_INFO("Platform shutdown");
    }
}

} // namespace Aestra
