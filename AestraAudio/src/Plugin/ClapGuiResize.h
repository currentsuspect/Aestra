// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// How the helper resizes a CLAP plugin's editor: the negotiation the CLAP gui extension asks of a host.
//
//   adjust_size  : the plugin turns the size the user dragged to into one it can draw (aspect ratio, steps, limits);
//   set_size     : the plugin is given THAT size;
//   the window   : takes the same size, so plugin and window agree.
//
// Passing the raw dragged size skips the first step: the plugin refuses it, snaps to its own size anyway, and is then
// drawn wider or taller than the window it sits in (RF-16VU, which keeps a fixed aspect ratio). No X11 and no plugin
// module here, so a test can drive it with a fake extension.

#include "ClapAbiMirror.h"

#include <cstdint>
#include <functional>
#include <utility>

namespace AestraHelper {
namespace ClapAbi {

/// Returns the size handed to set_size ({0, 0} when the plugin has no set_size). @p resizeWindow is called only when
/// that size differs from the one asked for; it may be empty.
inline std::pair<int, int> applyClapGuiResize(const ClapPluginGui& gui, const ClapPlugin* plugin, int width, int height,
                                              const std::function<void(int, int)>& resizeWindow) {
    if (!gui.setSize || width <= 0 || height <= 0) {
        return {0, 0};
    }
    uint32_t w = static_cast<uint32_t>(width);
    uint32_t h = static_cast<uint32_t>(height);
    if (gui.adjustSize) {
        uint32_t adjustedW = w;
        uint32_t adjustedH = h;
        // A plugin that says no gave no size: keep the request rather than whatever it left in the arguments.
        if (gui.adjustSize(plugin, &adjustedW, &adjustedH) && adjustedW > 0 && adjustedH > 0) {
            w = adjustedW;
            h = adjustedH;
        }
    }
    gui.setSize(plugin, w, h);
    const int appliedW = static_cast<int>(w);
    const int appliedH = static_cast<int>(h);
    if (resizeWindow && (appliedW != width || appliedH != height)) {
        resizeWindow(appliedW, appliedH);
    }
    return {appliedW, appliedH};
}

} // namespace ClapAbi
} // namespace AestraHelper
