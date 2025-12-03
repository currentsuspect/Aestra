/**
 * @file log.hpp
 * @brief Logging system - Migration wrapper
 * 
 * Wraps existing NomadLog.h and re-exports in new namespace.
 * Existing code using Nomad::Logger continues to work.
 */

#pragma once

// Include the existing implementation
#include "NomadCore/include/NomadLog.h"

namespace nomad::log {

//=============================================================================
// Re-export existing types in new namespace
//=============================================================================

using Level = ::Nomad::LogLevel;
using ILogger = ::Nomad::ILogger;
using ConsoleLogger = ::Nomad::ConsoleLogger;
using FileLogger = ::Nomad::FileLogger;
using Logger = ::Nomad::Logger;

//=============================================================================
// Convenience functions wrapping existing Logger singleton
//=============================================================================

inline Logger& get() {
    return ::Nomad::Logger::getInstance();
}

inline void debug(const std::string& msg) {
    get().debug(msg);
}

inline void info(const std::string& msg) {
    get().info(msg);
}

inline void warn(const std::string& msg) {
    get().warning(msg);
}

inline void error(const std::string& msg) {
    get().error(msg);
}

inline void setLevel(Level level) {
    get().setLevel(level);
}

//=============================================================================
// Category-based logging (extension for new code)
//=============================================================================

enum class Category : u8 {
    Core = 0,
    Audio,
    DSP,
    UI,
    Plugin,
    File,
    Network,
    Count
};

inline const char* categoryName(Category cat) {
    static const char* names[] = {
        "CORE", "AUDIO", "DSP", "UI", "PLUGIN", "FILE", "NET"
    };
    return names[static_cast<int>(cat)];
}

// Category-aware logging (prefixes message with category)
inline void log(Category cat, Level level, const std::string& msg) {
    std::string prefixed = std::string("[") + categoryName(cat) + "] " + msg;
    switch (level) {
        case Level::Debug:   get().debug(prefixed); break;
        case Level::Info:    get().info(prefixed); break;
        case Level::Warning: get().warning(prefixed); break;
        case Level::Error:   get().error(prefixed); break;
    }
}

} // namespace nomad::log

//=============================================================================
// Convenience macros (optional, for new code)
//=============================================================================

#define NOMAD_LOG_DEBUG(msg) ::nomad::log::debug(msg)
#define NOMAD_LOG_INFO(msg)  ::nomad::log::info(msg)
#define NOMAD_LOG_WARN(msg)  ::nomad::log::warn(msg)
#define NOMAD_LOG_ERROR(msg) ::nomad::log::error(msg)
