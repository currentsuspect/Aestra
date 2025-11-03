// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <string>
#include <cstdint>

namespace Nomad {
namespace Audio {

/**
 * @brief Audio driver types supported by NOMAD
 * 
 * Ordered by priority (lower value = higher priority)
 */
enum class AudioDriverType : uint8_t {
    // Phase 2-3: ASIO Drivers (lowest latency)
    ASIO_EXTERNAL = 0,      // External ASIO drivers (ASIO4ALL, FL ASIO, Focusrite, etc.)
    ASIO_NOMAD = 1,         // Nomad's own ASIO wrapper â†’ WASAPI Exclusive
    
    // Phase 1: Native Windows Audio
    WASAPI_EXCLUSIVE = 2,   // Exclusive mode - Pro/low-latency
    WASAPI_SHARED = 3,      // Shared mode - Default safe mode
    DIRECTSOUND = 4,        // Legacy fallback - Maximum compatibility
    
    // Legacy RtAudio
    RTAUDIO = 5,            // RtAudio backend (legacy fallback)
    
    // Future: Cross-platform
    COREAUDIO = 10,         // macOS
    ALSA = 11,              // Linux
    JACK = 12,              // Linux/macOS pro audio
    PULSEAUDIO = 13,        // Linux
    
    UNKNOWN = 255
};

/**
 * @brief Driver priority metadata
 */
struct DriverPriority {
    AudioDriverType type;
    uint8_t priority;           // 0 = highest
    const char* displayName;
    const char* description;
    bool requiresExternalDll;   // True for ASIO external
    float typicalLatencyMs;     // Typical achievable latency
};

/**
 * @brief Get priority table for Windows drivers
 */
inline const DriverPriority* GetWindowsDriverPriorities() {
    static const DriverPriority priorities[] = {
        { AudioDriverType::ASIO_EXTERNAL,     0, "ASIO (External)",    "Professional ASIO drivers (ASIO4ALL, etc.)", true,  2.0f },
        { AudioDriverType::ASIO_NOMAD,        1, "Nomad ASIO",         "Built-in ASIO wrapper",                      false, 3.0f },
        { AudioDriverType::WASAPI_EXCLUSIVE,  2, "WASAPI Exclusive",   "Low-latency exclusive mode",                 false, 5.0f },
        { AudioDriverType::WASAPI_SHARED,     3, "WASAPI Shared",      "Default safe mode",                          false, 15.0f },
        { AudioDriverType::DIRECTSOUND,       4, "DirectSound",        "Legacy fallback",                            false, 30.0f },
        { AudioDriverType::UNKNOWN,         255, "Unknown",            "Unknown driver",                             false, 100.0f }
    };
    return priorities;
}

/**
 * @brief Get number of Windows driver priorities
 */
inline size_t GetWindowsDriverPriorityCount() {
    return 6;
}

/**
 * @brief Convert driver type to display name
 */
inline const char* DriverTypeToString(AudioDriverType type) {
    const DriverPriority* priorities = GetWindowsDriverPriorities();
    for (size_t i = 0; i < GetWindowsDriverPriorityCount(); ++i) {
        if (priorities[i].type == type) {
            return priorities[i].displayName;
        }
    }
    return "Unknown";
}

/**
 * @brief Driver capability flags
 */
enum class DriverCapability : uint32_t {
    NONE                    = 0,
    PLAYBACK                = 1 << 0,   // Supports audio output
    RECORDING               = 1 << 1,   // Supports audio input
    DUPLEX                  = 1 << 2,   // Supports simultaneous I/O
    SAMPLE_RATE_CONVERSION  = 1 << 3,   // Can convert sample rates
    BIT_DEPTH_CONVERSION    = 1 << 4,   // Can convert bit depths
    EXCLUSIVE_MODE          = 1 << 5,   // Supports exclusive device access
    EVENT_DRIVEN            = 1 << 6,   // Uses event-driven callbacks
    HOT_PLUG_DETECTION      = 1 << 7,   // Detects device connection changes
    CHANNEL_MIXING          = 1 << 8,   // Can mix/route channels
};

inline DriverCapability operator|(DriverCapability a, DriverCapability b) {
    return static_cast<DriverCapability>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline DriverCapability operator&(DriverCapability a, DriverCapability b) {
    return static_cast<DriverCapability>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool HasCapability(DriverCapability flags, DriverCapability capability) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(capability)) != 0;
}

/**
 * @brief Driver state
 */
enum class DriverState : uint8_t {
    UNINITIALIZED = 0,
    INITIALIZED,
    STREAM_OPEN,
    STREAM_RUNNING,
    DRIVER_ERROR,
    FALLBACK_PENDING
};

/**
 * @brief Driver error codes
 */
enum class DriverError : uint32_t {
    NONE = 0,
    INITIALIZATION_FAILED,
    DEVICE_NOT_FOUND,
    DEVICE_IN_USE,
    UNSUPPORTED_FORMAT,
    BUFFER_UNDERRUN,
    BUFFER_OVERRUN,
    SAMPLE_RATE_MISMATCH,
    EXCLUSIVE_MODE_UNAVAILABLE,
    DRIVER_DLL_NOT_FOUND,
    DRIVER_DLL_LOAD_FAILED,
    STREAM_OPEN_FAILED,
    STREAM_START_FAILED,
    UNKNOWN_ERROR
};

/**
 * @brief Convert error to string
 */
inline const char* DriverErrorToString(DriverError error) {
    switch (error) {
        case DriverError::NONE:                         return "No error";
        case DriverError::INITIALIZATION_FAILED:        return "Driver initialization failed";
        case DriverError::DEVICE_NOT_FOUND:             return "Audio device not found";
        case DriverError::DEVICE_IN_USE:                return "Device is already in use";
        case DriverError::UNSUPPORTED_FORMAT:           return "Audio format not supported";
        case DriverError::BUFFER_UNDERRUN:              return "Buffer underrun detected";
        case DriverError::BUFFER_OVERRUN:               return "Buffer overrun detected";
        case DriverError::SAMPLE_RATE_MISMATCH:         return "Sample rate mismatch";
        case DriverError::EXCLUSIVE_MODE_UNAVAILABLE:   return "Exclusive mode not available";
        case DriverError::DRIVER_DLL_NOT_FOUND:         return "Driver DLL not found";
        case DriverError::DRIVER_DLL_LOAD_FAILED:       return "Driver DLL load failed";
        case DriverError::STREAM_OPEN_FAILED:           return "Stream open failed";
        case DriverError::STREAM_START_FAILED:          return "Stream start failed";
        default:                                        return "Unknown error";
    }
}

/**
 * @brief Driver statistics for monitoring and benchmarking
 */
struct DriverStatistics {
    uint64_t callbackCount = 0;          // Total callbacks processed
    uint64_t underrunCount = 0;          // Buffer underruns
    uint64_t overrunCount = 0;           // Buffer overruns
    double actualLatencyMs = 0.0;        // Measured latency
    double cpuLoadPercent = 0.0;         // CPU usage in audio thread
    double averageCallbackTimeUs = 0.0;  // Average callback execution time
    double maxCallbackTimeUs = 0.0;      // Peak callback execution time
    
    void reset() {
        callbackCount = 0;
        underrunCount = 0;
        overrunCount = 0;
        actualLatencyMs = 0.0;
        cpuLoadPercent = 0.0;
        averageCallbackTimeUs = 0.0;
        maxCallbackTimeUs = 0.0;
    }
};

} // namespace Audio
} // namespace Nomad
