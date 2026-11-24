// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// A rendered cache (an FBO holding a panel's static content) is made of theme colours, so it is only good for the theme
// it was drawn under. Each entry carries the theme generation it was built in (NUIThemeManager::themeGeneration());
// when the generation moves on, the entry is stale and is redrawn on its next use. This is the single place a theme
// change reaches every cache, instead of each owner remembering to invalidate its own (the file browser and the
// timeline kept the old theme's pixels until a scroll or a hover invalidated them, 21 Nov 2026).
//
// Header-only and free of GL, so the rule is tested without a renderer.

#include <cstdint>

namespace AestraUI {

/// Marks the entry invalid when it was built under another theme generation. Returns true when it did.
inline bool invalidateIfThemeChanged(bool& valid, uint64_t& builtInGeneration, uint64_t currentGeneration) {
    if (builtInGeneration == currentGeneration)
        return false;
    builtInGeneration = currentGeneration;
    valid = false;
    return true;
}

} // namespace AestraUI
