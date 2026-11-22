// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Which SDL video driver Aestra asks for on Linux, decided before SDL_Init. Pure and SDL-free so it is unit-tested.
//
// Why: a VST3/CLAP editor embeds into an Aestra panel only when Aestra's window is an X11 window
// (PlatformWindowLinux::getNativeHandle). On a Wayland session SDL picks native Wayland, the plugin then opens as a
// bare floating window. Under XWayland Aestra is an X11 window, so on a Wayland session with an X server reachable
// the default is "x11".
//
// Precedence: SDL_VIDEODRIVER set by the user always wins (we leave SDL alone); then AESTRA_VIDEO_DRIVER (e.g.
// AESTRA_VIDEO_DRIVER=wayland forces native Wayland, at the cost of plugin panels); then the Wayland+DISPLAY rule.
// Empty variables count as unset.
#pragma once

#include <optional>
#include <string>

namespace Aestra::LinuxVideo {

struct LinuxSessionEnv {
    std::optional<std::string> sdlVideoDriver;   ///< SDL_VIDEODRIVER
    std::optional<std::string> aestraVideoDriver; ///< AESTRA_VIDEO_DRIVER
    std::optional<std::string> xdgSessionType;   ///< XDG_SESSION_TYPE
    std::optional<std::string> waylandDisplay;   ///< WAYLAND_DISPLAY
    std::optional<std::string> display;          ///< DISPLAY
};

struct VideoDriverChoice {
    std::optional<std::string> driver; ///< set -> SDL_SetHint(SDL_HINT_VIDEODRIVER, *driver); empty -> leave SDL's default
    std::string reason;                ///< one human sentence for the log
};

// A Wayland session (as opposed to a plain X11 one): XDG_SESSION_TYPE=wayland or a WAYLAND_DISPLAY.
inline bool isWaylandSession(const LinuxSessionEnv& env) {
    auto set = [](const std::optional<std::string>& v) { return v && !v->empty(); };
    return (set(env.xdgSessionType) && *env.xdgSessionType == "wayland") || set(env.waylandDisplay);
}

inline VideoDriverChoice chooseLinuxVideoDriver(const LinuxSessionEnv& env) {
    auto set = [](const std::optional<std::string>& v) { return v && !v->empty(); };
    if (set(env.sdlVideoDriver))
        return {std::nullopt, "SDL_VIDEODRIVER=" + *env.sdlVideoDriver + " set by the user; leaving SDL alone"};
    if (set(env.aestraVideoDriver))
        return {*env.aestraVideoDriver, "AESTRA_VIDEO_DRIVER=" + *env.aestraVideoDriver + " override"};
    if (!isWaylandSession(env)) return {std::nullopt, "not a Wayland session; SDL default"};
    if (!set(env.display))
        return {std::nullopt, "Wayland session without DISPLAY (no XWayland); SDL default, plugin editors open as their own windows"};
    return {std::string("x11"), "Wayland session with XWayland on DISPLAY=" + *env.display +
                                    "; X11 so plugin editors embed in their panel (AESTRA_VIDEO_DRIVER=wayland overrides)"};
}

} // namespace Aestra::LinuxVideo
