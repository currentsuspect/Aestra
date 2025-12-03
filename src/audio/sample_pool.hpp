/**
 * @file sample_pool.hpp
 * @brief Sample pool/management - Migration wrapper
 * 
 * Wraps your existing SamplePool.h implementation.
 */

#pragma once

// Include YOUR existing sample pool implementation
#include "NomadAudio/include/SamplePool.h"

namespace nomad::audio {

//=============================================================================
// Re-export existing types in new namespace
//=============================================================================

using SamplePool = ::Nomad::Audio::SamplePool;
using SampleData = ::Nomad::Audio::SampleData;

// Export any additional types from your implementation

//=============================================================================
// Sample pool helpers
//=============================================================================

/// Get the global sample pool instance
inline SamplePool& getSamplePool() {
    return ::Nomad::Audio::SamplePool::getInstance();
}

/// Load a sample file into the pool
inline bool loadSample(const std::string& path) {
    return getSamplePool().loadSample(path);
}

} // namespace nomad::audio
