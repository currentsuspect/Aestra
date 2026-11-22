// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// When Aestra asks the window system to maximise, decided apart from SDL so it is unit-tested.
//
// Why: on X11 (including XWayland) each maximise request makes SDL wait on the window manager, about a second per
// request. A saved-maximised start used to pay it twice: SDL_WINDOW_MAXIMIZED at creation, then maximize() again
// from AestraWindowManager::applyWindowState.
#pragma once

#include <string>

namespace Aestra::WindowMaximize {

// applyWindowState: only ask for maximise when the window is not already maximised. A window born maximised
// (SDL_WINDOW_MAXIMIZED) has nothing left to ask for; the second request is a pure wait.
inline bool shouldCallMaximize(bool stateMaximized, bool windowIsMaximized) {
    return stateMaximized && !windowIsMaximized;
}

// PlatformWindowLinux::create: whether to put SDL_WINDOW_MAXIMIZED on the new window. Under XWayland (SDL driver x11
// inside a Wayland session) the compositor decides placement and size, often tiling, so the request means nothing
// there yet costs ~1 s of waiting. Skip it and create at the saved size. A plain X11 session, native Wayland and
// every other platform keep the request.
inline bool shouldRequestMaximizeAtCreate(bool startMaximized, bool driverIsX11, bool waylandSession) {
    return startMaximized && !(driverIsX11 && waylandSession);
}

} // namespace Aestra::WindowMaximize
