/**
 * @file nomad.hpp
 * @brief Main Nomad DAW header - Unified API
 * 
 * This is the single include that provides access to all Nomad
 * functionality through the new namespace structure.
 * 
 * Usage:
 *   #include <nomad.hpp>
 *   
 *   // New code uses new namespaces:
 *   nomad::audio::Track track;
 *   nomad::dsp::Filter filter;
 *   nomad::ui::Button button;
 *   
 *   // Existing code continues to work:
 *   Nomad::Audio::Track legacyTrack;  // Still works!
 * 
 * Migration Path:
 *   1. New code uses nomad:: namespace
 *   2. Gradually migrate existing code
 *   3. Eventually deprecate Nomad:: namespace
 */

#pragma once

//=============================================================================
// Core Module
//=============================================================================
#include "core/types.hpp"
#include "core/log.hpp"
#include "core/assert.hpp"
#include "core/math.hpp"

//=============================================================================
// DSP Module (Pure algorithms)
//=============================================================================
#include "dsp/filter.hpp"
#include "dsp/oscillator.hpp"

//=============================================================================
// Audio Module (Engine, tracks, mixing)
//=============================================================================
#include "audio/track.hpp"
#include "audio/mixer.hpp"
#include "audio/device.hpp"
#include "audio/sample_pool.hpp"

//=============================================================================
// UI Module (Components, widgets, rendering)
//=============================================================================
#include "ui/component.hpp"
#include "ui/widgets.hpp"
#include "ui/theme.hpp"
#include "ui/renderer.hpp"

//=============================================================================
// App Module (Application, window)
//=============================================================================
#include "app/app.hpp"

//=============================================================================
// Version Info
//=============================================================================
namespace nomad {

constexpr const char* VERSION = "2.0.0-migration";
constexpr int VERSION_MAJOR = 2;
constexpr int VERSION_MINOR = 0;
constexpr int VERSION_PATCH = 0;

/// Returns true if the migration wrappers are active
constexpr bool isMigrationBuild() { return true; }

} // namespace nomad
