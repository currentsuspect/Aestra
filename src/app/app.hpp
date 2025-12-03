/**
 * @file app.hpp
 * @brief Application core - Migration wrapper
 * 
 * Wraps your existing NUIApp and window system.
 */

#pragma once

// Include YOUR existing app system
#include "NomadUI/Core/NUIApp.h"
#include "NomadUI/Core/NUICustomWindow.h"
#include "NomadUI/Core/NUICustomTitleBar.h"
#include "NomadUI/Platform/NUIPlatformBridge.h"

namespace nomad::app {

//=============================================================================
// Re-export existing types in new namespace
//=============================================================================

using Application = ::NUIApp;
using Window = ::NUICustomWindow;
using TitleBar = ::NUICustomTitleBar;
using PlatformBridge = ::NUIPlatformBridge;

//=============================================================================
// Application helpers
//=============================================================================

/// Get the application instance
inline Application& getApp() {
    return ::NUIApp::getInstance();
}

/// Run the application main loop
inline int run() {
    return getApp().run();
}

} // namespace nomad::app
