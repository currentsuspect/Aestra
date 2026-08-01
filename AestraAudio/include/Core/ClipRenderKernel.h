// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Core/AudioGraph.h"
#include "Models/PlaylistRuntimeSnapshot.h"
#include "DSP/Interpolators.h"

#include <cstdint>

namespace Aestra {
namespace Audio {
namespace ClipRenderKernel {

/**
 * @brief The source-domain clip renderer, shared by the live path and offline.
 *
 * This is the single authority for what one audio clip sounds like: source
 * offset and slip, playback rate and resampling, clip gain, pan and fades. It
 * was the per-clip body of AudioEngine::renderClips() and is unchanged
 * arithmetic — see AestraDocs/specs/consolidate-audio-range.md §3.1 and §4b,
 * which require the consolidation renderer to *mirror* the runtime rather than
 * re-derive an interpretation of ClipEdits.
 *
 * Deliberately free of engine state. It takes no transport, no graph, no
 * routing, no PDC and no automation — those stay with the caller, and anything
 * needing them does not belong here. Interpolation quality arrives as a value
 * because the engine snapshots it once per block; the kernel must not reach
 * back for an atomic and observe a different one mid-block.
 */

/** Anti-click ramp applied at every clip edge, in frames. */
inline constexpr uint32_t kClipEdgeFadeSamples = 128;

/**
 * @brief What rendering one clip reported back, beyond the audio it mixed.
 *
 * Not a bare bool: `false` means "direct-rate audio was mixed successfully",
 * not "the clip contributed nothing". Naming the field keeps SRC meaning
 * *sample-rate conversion ran*.
 */
struct ClipRenderResult {
    bool usedSampleRateConversion{false};
};

/**
 * @brief Build the render state for one clip from its runtime snapshot entry.
 *
 * The single conversion from model-domain ClipRuntimeInfo to the engine's
 * ClipRenderState: source buffer and channel count, absolute sample bounds,
 * and the slip offset rescaled from project rate to the source's own rate.
 *
 * Shared rather than duplicated for the same reason the kernel itself is:
 * an offline caller that rebuilt this mapping would be a second
 * interpretation of ClipEdits, which consolidate-audio-range.md §3.1 forbids.
 * A pure function of its two arguments — no engine, no graph, no manager.
 */
[[nodiscard]] ClipRenderState makeClipRenderState(const ClipRuntimeInfo& clipInfo, double projectSampleRate);

/**
 * @brief Mix one clip into @p destination for the block [blockStart, blockEnd).
 *
 * @param destination Interleaved stereo doubles, owned by the caller and
 *        indexed from blockStart. The kernel never allocates or reallocates.
 * @return Whether sample-rate conversion ran. The caller aggregates this
 *         across the block; the kernel knows nothing about telemetry.
 *
 * [[nodiscard]] deliberately: discarding it would keep every audio sample
 * identical while silently deleting SRC telemetry, which the characterization
 * digests cannot see.
 */
[[nodiscard]] ClipRenderResult renderClipInto(const ClipRenderState& clip, uint64_t blockStart, uint64_t blockEnd,
                                              double* destination, uint32_t engineSampleRate,
                                              Interpolators::InterpolationQuality quality);

} // namespace ClipRenderKernel
} // namespace Audio
} // namespace Aestra
