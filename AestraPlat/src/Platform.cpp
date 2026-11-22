// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "../../AestraCore/include/AestraConfig.h"
#include "../../AestraCore/include/AestraLog.h"
#include "../include/AestraPlatform.h"

#include <memory>

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
#include "../include/LinuxVideoDriver.h"
#include <cstdlib>
#endif
#elif AESTRA_PLATFORM_MACOS
// macOS stub implementation - runtime parity pending
// No Cocoa/AppKit headers required for headless builds
#endif

namespace Aestra {

// Static members
std::shared_ptr<IPlatformUtils> Platform::s_utils;

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
    return nullptr; // TODO
#endif
}

IPlatformUtils* Platform::getUtils() {
    return s_utils.get();
}

std::shared_ptr<IPlatformUtils> Platform::getUtilsShared() {
    return s_utils;
}

bool Platform::isInitialized() {
    return s_utils != nullptr; // shared_ptr converts to bool implicitly
}

bool Platform::initialize() {
    if (s_utils) {
        return true; // Already initialized
    }

#if AESTRA_PLATFORM_WINDOWS
    // Initialize DPI awareness first (must be done before creating windows)
    PlatformDPI::initialize();

    s_utils = std::make_shared<PlatformUtilsWin32>();
    AESTRA_LOG_INFO("Windows platform initialized");
#elif AESTRA_PLATFORM_LINUX
#ifdef AESTRA_HAS_SDL2
    // Choose the video driver before SDL_Init: X11 (XWayland) on a Wayland session so plugin editors embed.
    Aestra::LinuxVideo::LinuxSessionEnv sessionEnv;
    auto readEnv = [](const char* name) -> std::optional<std::string> {
        const char* v = std::getenv(name);
        return v ? std::optional<std::string>(v) : std::nullopt;
    };
    sessionEnv.sdlVideoDriver = readEnv("SDL_VIDEODRIVER");
    sessionEnv.aestraVideoDriver = readEnv("AESTRA_VIDEO_DRIVER");
    sessionEnv.xdgSessionType = readEnv("XDG_SESSION_TYPE");
    sessionEnv.waylandDisplay = readEnv("WAYLAND_DISPLAY");
    sessionEnv.display = readEnv("DISPLAY");
    const auto videoChoice = Aestra::LinuxVideo::chooseLinuxVideoDriver(sessionEnv);
    if (videoChoice.driver) SDL_SetHint(SDL_HINT_VIDEODRIVER, videoChoice.driver->c_str());
    AESTRA_LOG_INFO("Video driver: " + (videoChoice.driver ? *videoChoice.driver : std::string("SDL default")) + " (" +
                    videoChoice.reason + ")");

    // Initialize SDL
    bool sdlReady = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) == 0;
    if (!sdlReady && videoChoice.driver && !sessionEnv.sdlVideoDriver) {
        // A DISPLAY that does not answer (stale, or no XWayland) must not stop Aestra starting: fall back to SDL's pick.
        AESTRA_LOG_WARNING(std::string("SDL_Init with video driver ") + *videoChoice.driver + " failed (" + SDL_GetError() +
                        "); retrying with SDL's default");
#if SDL_VERSION_ATLEAST(2, 26, 0)
        SDL_ResetHint(SDL_HINT_VIDEODRIVER);
#else
        SDL_ClearHints(); // nothing else has set a hint this early in startup
#endif
        sdlReady = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) == 0;
    }
    if (!sdlReady) {
        AESTRA_LOG_ERROR(std::string("SDL_Init failed: ") + SDL_GetError());
        return false;
    }

    s_utils = std::make_shared<PlatformUtilsLinux>();
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
    s_utils.reset();
#if AESTRA_PLATFORM_LINUX && defined(AESTRA_HAS_SDL2)
    SDL_Quit();
#endif
    AESTRA_LOG_INFO("Platform shutdown");
}

} // namespace Aestra
