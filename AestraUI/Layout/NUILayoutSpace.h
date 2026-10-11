// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUITypes.h"

namespace AestraUI {
namespace Layout {

/**
 * @file NUILayoutSpace.h
 * @brief Typed coordinate spaces — V8-X2b principle 1 and 2.
 *
 * V8-X2b Core Spec Reconstruction (Aestra-Internals, 13 Agent Work) names three
 * minimal spaces — Window, Local, Viewport — each required to earn existence by
 * representing a real transformation boundary. This header carries the two that
 * have one today.
 *
 * Deliberately no include beyond NUITypes.h, which itself pulls in nothing but the
 * standard library. A test that includes only this header links nothing and needs
 * no target to exist — see the file comment on NUITransformStack.h for why that
 * matters: AESTRA_CI=ON force-disables AestraUI_Core, so a test with a link
 * dependency on it is skipped, not failed, when the invariant it guards breaks.
 */

/**
 * @brief The coordinate spaces the layout subsystem recognises.
 *
 * `Viewport` is declared and deliberately not given a concrete type here. Its
 * mapping (beats→x, dB→y, semitones→y, …) is per-consumer, and there is no single
 * universal Viewport transform to typedef. Inventing one now, with no migrated
 * consumer to derive its shape from, would be a new abstraction adopted for
 * anticipated convenience rather than an observed defect — the exact pattern
 * FD-18 constraint 1 exists to refuse. The first real consumer that needs it
 * settles its shape; see the spec reconstruction doc for the full reasoning.
 */
enum class NUISpace {
    Window,   //!< Origin at the platform window's top-left. What input events and the scissor rect are expressed in.
    Local,    //!< Origin at a node's own top-left. What a node places its own children relative to.
    Viewport, //!< Reserved. No concrete type yet — see above.
};

/**
 * @brief A point tagged with the space it was measured in.
 *
 * Two points in different spaces cannot be added, compared, or substituted for one
 * another without going through localToWindow()/windowToLocal() first. That is the
 * entire purpose of the tag: a mismatched space becomes a compile error — a type
 * mismatch the compiler rejects — rather than a runtime mystery discovered by
 * reading pixel offsets off a screenshot, which is how the TrackManager
 * window-absolute-vs-relative confusion this contract answers was actually found.
 *
 * No conversion operator to or from a bare NUIPoint is provided. Accepting a raw
 * NUIPoint anywhere a space-tagged one is expected would silently readmit the
 * confusion this type exists to end — the escape hatch is the explicit raw()
 * accessor, one call, visible at every use.
 */
template <NUISpace S>
struct NUITypedPoint {
    static constexpr NUISpace kSpace = S;
    float x = 0.0f;
    float y = 0.0f;

    NUITypedPoint() = default;
    NUITypedPoint(float xIn, float yIn) : x(xIn), y(yIn) {}

    /** The untagged value. Named raw() so every call site marks the moment the tag is dropped. */
    NUIPoint raw() const { return NUIPoint(x, y); }
};

/**
 * @brief A rectangle tagged with the space it was measured in. Same rule as NUITypedPoint.
 */
template <NUISpace S>
struct NUITypedRect {
    static constexpr NUISpace kSpace = S;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    NUITypedRect() = default;
    NUITypedRect(float xIn, float yIn, float wIn, float hIn) : x(xIn), y(yIn), width(wIn), height(hIn) {}

    NUIRect raw() const { return NUIRect(x, y, width, height); }
    float right() const { return x + width; }
    float bottom() const { return y + height; }
    bool isEmpty() const { return width <= 0.0f || height <= 0.0f; }
};

using NUIWindowPoint = NUITypedPoint<NUISpace::Window>;
using NUILocalPoint = NUITypedPoint<NUISpace::Local>;
using NUIWindowRect = NUITypedRect<NUISpace::Window>;
using NUILocalRect = NUITypedRect<NUISpace::Local>;

/**
 * @brief The one sanctioned way to move a point from a parent's Local space into
 * Window space: add the parent's own Window-space origin.
 *
 * There is deliberately no operator+ between a NUILocalPoint and a NUIWindowPoint,
 * and no implicit constructor from one to the other — both would let a Local value
 * silently pass as a Window one, which is precisely the confusion principle 2 exists
 * to end.
 */
inline NUIWindowPoint localToWindow(const NUILocalPoint& local, const NUIWindowPoint& parentOriginInWindow) {
    return NUIWindowPoint(local.x + parentOriginInWindow.x, local.y + parentOriginInWindow.y);
}

/** The inverse of localToWindow(): recover a point's position in the parent's Local space. */
inline NUILocalPoint windowToLocal(const NUIWindowPoint& point, const NUIWindowPoint& parentOriginInWindow) {
    return NUILocalPoint(point.x - parentOriginInWindow.x, point.y - parentOriginInWindow.y);
}

/** Rect form of localToWindow() — the size is space-independent, only the origin moves. */
inline NUIWindowRect localToWindow(const NUILocalRect& local, const NUIWindowPoint& parentOriginInWindow) {
    return NUIWindowRect(local.x + parentOriginInWindow.x, local.y + parentOriginInWindow.y, local.width, local.height);
}

/** Rect form of windowToLocal(). */
inline NUILocalRect windowToLocal(const NUIWindowRect& windowRect, const NUIWindowPoint& parentOriginInWindow) {
    return NUILocalRect(windowRect.x - parentOriginInWindow.x, windowRect.y - parentOriginInWindow.y, windowRect.width, windowRect.height);
}

} // namespace Layout
} // namespace AestraUI
