// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstddef>
#include <vector>

namespace AestraUI {

/**
 * @file NUITransformStack.h
 * @brief The one place a renderer transform stack is composed into an affine map.
 *
 * This header is deliberately free of every other dependency — no NUITypes, no
 * GL, no component tree. Two reasons, and the second is the important one:
 *
 *  1. Both geometry and clipping must consume the *same* composition. When the
 *     renderer derived it twice, the two derivations drifted into opposite
 *     operation orders and disagreed for every scale != 1.
 *  2. A test for this must be able to fail a CI build. UI tests that link
 *     AestraUI_Core are skipped in CI (AESTRA_CI=ON forces AESTRA_ENABLE_UI=OFF,
 *     so the target does not exist and the registration falls through a status
 *     message). A header with no link dependency can be included by a plain test
 *     target, which runs unconditionally.
 */

/**
 * @brief One level of the transform stack.
 *
 * Maps a point from the space of the level's children into the space of its
 * parent: `p_parent = scale * p_child + t`.
 *
 * Scale is uniform and applied about the origin of the parent space, so a
 * translation pushed *inside* a scaled level is scaled by it — which is what
 * makes nesting behave the way a reader expects.
 *
 * There is no rotation field. `pushTransform` used to accept a rotation
 * argument, store it, and never apply it: the composition function ended with
 * a bare `// Apply rotation` comment and no code. A parameter that is accepted
 * and silently ignored reads as supported, which is worse than one that is
 * absent. Nothing in the tree ever passed a non-zero value. If rotation is
 * needed later it arrives with a test, as a matrix, not as a fourth float.
 */
struct NUITransform2D {
    float tx = 0.0f;
    float ty = 0.0f;
    float scale = 1.0f;
};

/**
 * @brief A whole stack collapsed into a single affine map to the outermost space.
 *
 * Applying this is exact and order-independent at the call site: every consumer
 * that uses it agrees with every other consumer by construction, which is the
 * property the two hand-rolled stack walks could not offer.
 */
struct NUIComposedTransform {
    float scale = 1.0f;
    float tx = 0.0f;
    float ty = 0.0f;

    /** Map a point from the innermost local space to the outermost space. */
    void applyToPoint(float& x, float& y) const {
        x = (x * scale) + tx;
        y = (y * scale) + ty;
    }

    /**
     * Map an axis-aligned rectangle, given as two corners.
     *
     * Both corners go through the identical map, so the rectangle's size scales
     * with the geometry inside it. This is the function clipping must use: a
     * clip derived any other way is a rectangle in a different space than the
     * pixels it is supposed to be clipping.
     */
    void applyToRect(float& x1, float& y1, float& x2, float& y2) const {
        applyToPoint(x1, y1);
        applyToPoint(x2, y2);
    }

    /** True when this map leaves every point where it found it. */
    [[nodiscard]] bool isIdentity() const {
        return scale == 1.0f && tx == 0.0f && ty == 0.0f;
    }
};

/**
 * @brief Compose a transform stack into one affine map.
 *
 * @param stack Levels ordered outermost-first — i.e. push order, so `stack[0]`
 *              is the first level pushed and `stack[count - 1]` the innermost.
 * @param count Number of levels.
 *
 * A point in the innermost local space is mapped by the innermost level first
 * and the outermost level last:
 *
 *     p_out = M_0( M_1( ... M_{n-1}(p) ... ))
 *
 * For two levels that expands to `s0*s1*p + (t0 + s0*t1)` — note that the inner
 * translation is scaled by the outer level, and the outer translation is not
 * scaled at all. Neither of the two implementations this replaces produced that:
 * one accumulated `p += t` then `p *= s` per level, the other `p *= s` then
 * `p += t`. Both are correct only when every scale is exactly 1, which is the
 * sole configuration any caller has ever used.
 */
inline NUIComposedTransform composeTransformStack(const NUITransform2D* stack, std::size_t count) {
    NUIComposedTransform out;
    for (std::size_t i = 0; i < count; ++i) {
        // Accumulate the translation in the *current* outer scale before
        // folding this level's scale in. Doing it the other way round is the
        // exact mistake being fixed here.
        out.tx += out.scale * stack[i].tx;
        out.ty += out.scale * stack[i].ty;
        out.scale *= stack[i].scale;
    }
    return out;
}

/** Convenience overload for the renderer's own storage. */
inline NUIComposedTransform composeTransformStack(const std::vector<NUITransform2D>& stack) {
    return composeTransformStack(stack.data(), stack.size());
}

} // namespace AestraUI
