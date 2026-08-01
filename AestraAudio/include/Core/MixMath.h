// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cmath>

namespace Aestra {
namespace Audio {
namespace MixMath {

/**
 * @brief Numeric guards shared by everything that writes into a mix buffer.
 *
 * These were file-local to AudioEngine.cpp. They live here so the clip render
 * kernel — and the consolidation renderer built on it — use the same ones
 * rather than carrying private copies. Two copies of a clamp is how a rounding
 * difference eventually becomes an audible difference between the live path
 * and an offline one.
 *
 * Semantics are unchanged from the originals; this is a move, not a rewrite.
 */

/** @brief Clamp to [lo, hi]. NaN propagates, matching the original. */
inline double clampD(double v, double lo, double hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

/**
 * @brief Replace NaN/Inf with silence at a mix boundary.
 *
 * A misbehaving plugin or a corrupted clip source can emit non-finite samples;
 * catching them where they enter the mix keeps the poison out of per-track
 * effect state, meters and sends, rather than relying only on the final
 * output-stage guard after the damage has accumulated. Branch is bounded and
 * RT-safe.
 */
inline double sanitizeMix(double v) noexcept {
    return std::isfinite(v) ? v : 0.0;
}

} // namespace MixMath
} // namespace Audio
} // namespace Aestra
