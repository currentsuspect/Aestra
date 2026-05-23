// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Aestra {
namespace Audio {

/**
 * Transport event types for diagnostic epoch tracking.
 * Used to correlate timing anomalies with transport state changes.
 */
enum class TransportEventType : uint32_t {
    None = 0,
    Seek,
    LoopWrap,
    StopToPlay,
    TempoMapRebuild,
    PrerollStart
};

/**
 * Diagnostic epoch capturing the state at the start of a processing callback.
 * Enables correlation of timing anomalies with specific engine states.
 */
struct DiagnosticEpoch {
    uint64_t callbackId{0};
    uint64_t graphGeneration{0};
    uint64_t transportGeneration{0};
    TransportEventType lastTransportEvent{TransportEventType::None};
};

/**
 * Information about a denormal (subnormal) floating-point event.
 * Severity levels:
 *   0 = Flag altered only (FTZ/DAZ detected a change)
 *   1 = Subnormal signal emitted from plugin
 *   2 = Confirmed timing slowdown due to denormals
 */
struct DenormalEventInfo {
    uint64_t pluginId{0};
    uint64_t timestamp{0};
    uint32_t severity{0};
};

/**
 * Multi-tier audio engine diagnostics.
 * Captures scheduling jitter, dispatch latency, callback duration,
 * xruns, RT violations, plugin health, and denormal events.
 *
 * All fields are populated by the audio thread and read by the UI/telemetry
 * thread via a seqlock snapshot mechanism.
 */
struct AudioEngineDiagnostics {
    // Multi-tier timing breakdown
    double schedulerJitterMs{0.0};   ///< Deviation between expected wake and actual signal wake
    double dispatchJitterMs{0.0};    ///< Deviation between signal wake and thread execution start
    double callbackDurationMs{0.0};  ///< Core DSP processing duration
    double callbackBudgetMs{0.0};    ///< Allocated time budget for this callback

    uint64_t xruns{0};
    uint64_t rtAllocations{0};
    uint64_t graphRebuilds{0};

    uint64_t pluginTimeouts{0};
    uint64_t pdcAdjustments{0};

    size_t activeVoices{0};
    size_t activePlugins{0};

    double cpuLoad{0.0};
    double peakCpuLoad{0.0};

    uint64_t denormalEvents{0};
    DenormalEventInfo lastDenormal{0};

    uint64_t nonRtMutexWaitTimeNs{0};
};

/**
 * Coherent diagnostics snapshot obtained via seqlock.
 * The generation counter enables the reader to detect torn reads.
 */
struct CoherentDiagnosticsSnapshot {
    uint64_t generation{0};
    AudioEngineDiagnostics diagnostics;
};

} // namespace Audio
} // namespace Aestra
