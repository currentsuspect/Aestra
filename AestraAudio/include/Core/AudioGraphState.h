// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <vector>
#include <cstdint>
#include <vector>
#include <string>

namespace Aestra {
namespace Audio {

    // Double-precision smoothed parameter for zero-zipper automation
    struct SmoothedParamD {
        double current{1.0};
        double target{1.0};
        double coeff{0.001};  // Per-sample coefficient
        
        inline double next() {
            current += coeff * (target - current);
            return current;
        }
        
        void setTarget(double t) { target = t; }
        // Snap to target immediately
        void snap() { current = target; }  
    };

    struct TrackRTState {
        // Optimized: Store smoothed L/R gains directly to avoid per-sample sin/cos
        SmoothedParamD gainL;
        SmoothedParamD gainR;
        
        // Logical state for command updates (snapshot of last known values)
        float currentVolume{1.0f};
        float currentPan{0.0f};

        bool mute{false};
        bool solo{false};
        bool soloSafe{false};
    };

    struct RuntimeConnection {
        double* destinationBufferL{nullptr}; // Raw pointer to Target Left Buffer
        double* destinationBufferR{nullptr}; // Raw pointer to Target Right Buffer
        size_t stride{2};                    // Interleaved stride (2 for stereo)
        double gainL{1.0};                   // Left Channel Gain (Linear)
        double gainR{1.0};                   // Right Channel Gain (Linear)
    };

    struct RenderTrack {
        uint32_t trackIndex{0};
        double* selfBuffer{nullptr}; // Pointer to this track's output buffer (interleaved)
        std::vector<RuntimeConnection> activeConnections; // Pre-resolved targets
    };

    /**
     * @brief Encapsulates the complete state required to render audio for a moment in time.
     * This object can be deep-copied to create a snapshot for the background thread.
     */
    struct AudioGraphState {
        // The ordered list of tracks to render (Topology)
        std::vector<RenderTrack> renderTracks;
        
        // The runtime state of each track (Volume, Pan, Mute)
        // Indexed by trackIndex
        std::vector<TrackRTState> trackStates;

        // Note: Plugin state will be added here later (v4.0 Hybrid Engine)
        // For now, plugins are still referenced via pointers in the AudioGraph or global lookups.
        // In the true Hybrid engine, we will need to clone plugin instances or parameters here.
    };

} // namespace Audio
} // namespace Aestra
