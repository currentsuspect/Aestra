/**
 * @file types.hpp
 * @brief Core type definitions - Migration wrapper
 * 
 * This header establishes the new namespace structure while
 * maintaining compatibility with existing code.
 */

#pragma once

// Pull in existing core types
#include "NomadCore/include/NomadConfig.h"

#include <cstdint>
#include <cstddef>

namespace nomad {

//=============================================================================
// Integer types (match existing usage patterns)
//=============================================================================
using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using usize = std::size_t;
using isize = std::ptrdiff_t;

//=============================================================================
// Floating point types
//=============================================================================
using f32 = float;
using f64 = double;

//=============================================================================
// Audio-specific types
//=============================================================================

/// Single audio sample (32-bit float, standard for real-time audio)
using Sample = f32;

/// High-precision sample for offline processing
using SampleHQ = f64;

/// Frame count type
using FrameCount = u32;

/// Sample position (signed for relative offsets)  
using SamplePos = i64;

//=============================================================================
// Compatibility aliases (bridge to existing Nomad:: namespace)
//=============================================================================
namespace compat {
    // These allow gradual migration from Nomad:: to nomad::
    using namespace ::Nomad;
}

} // namespace nomad

// Allow existing code to continue working
namespace Nomad {
    // Types are already defined in NomadConfig.h
}
