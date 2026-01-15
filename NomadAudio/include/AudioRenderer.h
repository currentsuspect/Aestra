// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AudioGraphState.h"
#include "EngineState.h" // For AudioGraph definition if needed, or forward declare
#include <cstdint>

namespace Nomad {
namespace Audio {

    class AudioEngine; // Forward declare for access to buffers (temporary)

    /**
     * @brief Stateless Audio Renderer.
     * 
     * Handles the "Meat" of the audio processing:
     * 1. Iterating through sorted RenderTracks
     * 2. Rendering Clips (Sampler/Audio)
     * 3. Processing Effects
     * 4. Mixing to destinations
     */
    class AudioRenderer {
    public:
        struct Context {
            double* masterBuffer;   // The final output accumulator (Double Precision)
            uint32_t numFrames;     // Block size
            uint32_t bufferOffset;  // Offset into the buffer (for split loops)
            uint64_t globalPos;     // Global timeline position (samples)
            uint32_t sampleRate;    // Current SR
            
            // Temporary: Access to AudioEngine's effect/clip resources until fully decoupled
            // ideally we pass these in too.
            const AudioGraph* graph{nullptr};
            
            // Metering Data from Engine (computed async)
            double integratedLufs{-144.0};

            // Bounce/Isolation Support
            int32_t isolatedTrackIndex{-1}; // If >= 0, only render this track
            bool isOffline{false};          // If true, skip real-time side effects (metering, etc)
        };

        AudioRenderer();
        ~AudioRenderer();

        /**
         * @brief Render a single block of audio.
         * @param ctx Execution context (buffers, time).
         * @param state The graph state (tracks, params).
         * @param engineRef Helper ref to engine for accessing EffectChains/Clips (Legacy bridge).
         */
        /**
         * @brief Pass 1: Handle MIDI timing and pattern scheduling
         */
        void processArsenalMidi(const Context& ctx, AudioEngine& engineRef);

        /**
         * @brief Pass 2: Main graph render (Clips + Plugins + Effects)
         */
        void renderBlock(const Context& ctx, AudioGraphState& state, AudioEngine& engineRef);

        /**
         * @brief Pass 3: Process Arsenal units routed to Master
         */
        void processArsenalUnits(const Context& ctx, AudioEngine& engineRef);

    private:
        /**
         * @brief Helper to render Arsenal units assigned to a specific track
         */
        void renderArsenalUnitsForTrack(uint32_t trackIndex, double* trackBuffer, const Context& ctx, AudioEngine& engineRef);

    private:
        // Helper methods (Moved from AudioEngine)
        void renderClipAudio(double* outputBuffer, 
                             TrackRTState& state, 
                             uint32_t trackIndex, 
                             const Context& ctx,
                             AudioEngine& engineRef);

        void processTrackEffects(const RenderTrack& track, 
                                 AudioGraphState& state,
                                 uint32_t numFrames, 
                                 uint32_t bufferOffset,
                                 AudioEngine& engineRef);
    };

} // namespace Audio
} // namespace Nomad
