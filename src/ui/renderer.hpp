/**
 * @file renderer.hpp
 * @brief Graphics renderer - Migration wrapper
 * 
 * Wraps your existing NUIRenderer and graphics system.
 */

#pragma once

// Include YOUR existing renderer
#include "NomadUI/Graphics/NUIRenderer.h"
#include "NomadUI/Graphics/NUIFont.h"
#include "NomadUI/Graphics/NUITextRenderer.h"
#include "NomadUI/Graphics/NUISVGCache.h"

namespace nomad::ui {

//=============================================================================
// Re-export existing types in new namespace
//=============================================================================

using Renderer = ::NUIRenderer;
using Font = ::NUIFont;
using TextRenderer = ::NUITextRenderer;
using SVGCache = ::NUISVGCache;

//=============================================================================
// Renderer access helpers
//=============================================================================

/// Get the global renderer instance
inline Renderer& getRenderer() {
    return ::NUIRenderer::getInstance();
}

} // namespace nomad::ui
