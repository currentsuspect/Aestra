/**
 * @file theme.hpp
 * @brief Theme system - Migration wrapper
 * 
 * Wraps your existing NUITheme and NUIThemeSystem.
 */

#pragma once

// Include YOUR existing theme system
#include "NomadUI/Core/NUITheme.h"
#include "NomadUI/Core/NUIThemeSystem.h"

namespace nomad::ui {

//=============================================================================
// Re-export existing types in new namespace
//=============================================================================

using Theme = ::NUITheme;
using ThemeSystem = ::NUIThemeSystem;

//=============================================================================
// Theme helpers
//=============================================================================

/// Get the current theme
inline Theme& getCurrentTheme() {
    return ::NUIThemeSystem::getInstance().getCurrentTheme();
}

/// Set theme by name
inline bool setTheme(const std::string& themeName) {
    return ::NUIThemeSystem::getInstance().setTheme(themeName);
}

} // namespace nomad::ui
