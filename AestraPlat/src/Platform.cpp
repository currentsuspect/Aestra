// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "../../AestraCore/include/AestraConfig.h"
#include "../../AestraCore/include/AestraLog.h"
#include "../include/AestraPlatform.h"

// Platform-specific includes
#if AESTRA_PLATFORM_WINDOWS
#include "Win32/PlatformDPIWin32.h"
#include "Win32/PlatformUtilsWin32.h"
#include "Win32/PlatformWindowWin32.h"
#elif AESTRA_PLATFORM_LINUX
#ifdef AESTRA_HAS_SDL2
#include "Linux/PlatformUtilsLinux.h"
#include "Linux/PlatformWindowLinux.h"

#include <SDL2/SDL.h>
#endif
#elif AESTRA_PLATFORM_MACOS
#ifdef AESTRA_HAS_SDL2
#include "macOS/PlatformUtilsmacOS.h"
#include "macOS/PlatformWindowmacOS.h"

#include <SDL2/SDL.h>
#endif
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
#ifdef AESTRA_HAS_SDL2
    return new PlatformWindowmacOS();
#else
    return nullptr;
#endif
#endif
}

IPlatformUtils* Platform::getUtils() {
    if (!s_utils) {
        AESTRA_LOG_ERROR("Platform not initialized! Call Platform::initialize() first.");
    }
    return s_utils;
}

bool Platform::isInitialized() {
    return s_utils != nullptr;
}

bool Platform::initialize() {
    if (s_utils) {
        return true; // Already initialized
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
        AESTRA_LOG_ERROR(std::string("SDL_Init failed: ") + SDL_GetError());
        return false;
    }

    s_utils = new PlatformUtilsLinux();
    AESTRA_LOG_INFO("Linux platform initialized (SDL2)");
#else
    AESTRA_LOG_ERROR("Linux platform not supported without SDL2");
    return false;
#endif
#elif AESTRA_PLATFORM_MACOS
    // Initialize SDL for macOS
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0) {
        AESTRA_LOG_STREAM_ERROR << "SDL_Init failed: " << SDL_GetError();
        return false;
    }

    // macOS-specific SDL hints
    SDL_SetHint(SDL_HINT_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK, "1");
    SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "1");

    s_utils = new PlatformUtilsmacOS();
    AESTRA_LOG_INFO("macOS platform initialized (SDL2)");
#endif

    return s_utils != nullptr;
}

void Platform::shutdown() {
    if (s_utils) {
        delete s_utils;
        s_utils = nullptr;
#if (AESTRA_PLATFORM_LINUX && defined(AESTRA_HAS_SDL2)) || (AESTRA_PLATFORM_MACOS && defined(AESTRA_HAS_SDL2))
        SDL_Quit();
#endif
        AESTRA_LOG_INFO("Platform shutdown");
    }
}

} // namespace Aestra
