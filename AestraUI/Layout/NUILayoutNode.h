// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUILayoutSpace.h"
#include <vector>

namespace AestraUI {
namespace Layout {

/**
 * @file NUILayoutNode.h
 * @brief The layout node, its dirty semantics, and the debug trace X2b·4 requires.
 *
 * Header-only, deliberately: no .cpp, no link dependency on AestraUI_Core. See the
 * file comment on NUILayoutSpace.h for why a test built against this must not need
 * that target to exist.
 *
 * Not wired into NUIComponent. That is the next migration phase (a simple panel),
 * not this one — see the V8-X2b Core Spec Reconstruction doc for the scope line.
 */

/**
 * @brief Per-node layout dirtiness. Three states, not two, and not a repaint flag.
 *
 * `MeasureDirty` means this node (or a descendant) must be re-measured before
 * anything else. `ArrangeDirty` means the last measurement is still valid and only
 * placement needs to run again — a resize of an already-measured sibling, say.
 * `Clean` needs neither.
 *
 * This is not the same concept as a repaint rectangle (V8-C3's dirty regions) or a
 * cache-invalidation signal (V8-C2's render cache), and must not be merged with
 * either even though the three compose in one frame: layout produces "this node's
 * geometry changed," dirty regions produce "this rectangle must be repainted," the
 * render cache produces "this cached surface is now invalid." Layout must not know
 * about render caches — that chain is a sequencing fact for the milestone, not a
 * dependency inside this subsystem.
 */
enum class NUILayoutDirty {
    Clean,
    MeasureDirty,
    ArrangeDirty,
};

/**
 * @brief The inbound sizing question a parent poses to a child.
 *
 * Deliberately minimal for this slice — width/height ceilings only. The contract's
 * ten-part list names "constraints" without elaborating a type family (min/max,
 * aspect ratio, fill-remaining, fixed, …), and the surviving summary does not
 * settle which. Extend this only against a second real consumer's actual
 * requirement, not in anticipation of one — see the spec reconstruction doc.
 */
struct NUIConstraint {
    float maxWidth = 0.0f;
    float maxHeight = 0.0f;

    NUIConstraint() = default;
    NUIConstraint(float w, float h) : maxWidth(w), maxHeight(h) {}
};

/** @brief What a node asked for, given a constraint — not yet where it will go. */
struct NUIMeasureResult {
    float desiredWidth = 0.0f;
    float desiredHeight = 0.0f;

    NUIMeasureResult() = default;
    NUIMeasureResult(float w, float h) : desiredWidth(w), desiredHeight(h) {}
};

class NUILayoutNode;

/**
 * @brief The record X2b·4 requires: not just where a node ended up, but why.
 *
 * Captured at arrange() time, when both halves of the answer exist together —
 * what was asked for (measure) and what was actually given (arrange). Capturing
 * these separately and reconciling them later is how "why is it there" answers
 * end up narrated after the fact instead of read off data; this contract's
 * whole complaint about the 2025 deferral was that the automated system needed
 * to be observable, not just automated.
 */
struct NUILayoutTrace {
    const NUILayoutNode* node = nullptr;
    const NUILayoutNode* constraintSource = nullptr; //!< Which ancestor posed the constraint this node measured against.
    NUIConstraint constraintReceived;
    NUIMeasureResult measured;   //!< What this node asked for.
    NUILocalRect arranged;       //!< What it was actually given, in its PARENT's local space.
    bool absolutelyPositioned = false;

    /** Positive: the node got more than it asked for. Negative: less. Zero: no gap to explain. */
    float widthDelta() const { return arranged.width - measured.desiredWidth; }
    float heightDelta() const { return arranged.height - measured.desiredHeight; }

    bool hasDelta() const {
        constexpr float kEps = 0.01f;
        return widthDelta() > kEps || widthDelta() < -kEps || heightDelta() > kEps || heightDelta() < -kEps;
    }
};

/**
 * @brief A single layout participant.
 *
 * Absolute positioning is the contract's required escape hatch, not a concession:
 * piano-roll overlays, waveform markers, handles and precisely positioned musical
 * elements need it. An absolutely positioned node is exempt from a parent's
 * arrangement pass but not from the coordinate-space rules or the trace — an
 * escape hatch that loses observability is not an escape hatch.
 */
class NUILayoutNode {
public:
    virtual ~NUILayoutNode() = default;

    void setAbsolutelyPositioned(bool absolute) { absolutelyPositioned_ = absolute; }
    bool isAbsolutelyPositioned() const { return absolutelyPositioned_; }

    void markMeasureDirty() { dirty_ = NUILayoutDirty::MeasureDirty; }
    void markArrangeDirty() {
        if (dirty_ == NUILayoutDirty::Clean) dirty_ = NUILayoutDirty::ArrangeDirty;
    }
    void markClean() { dirty_ = NUILayoutDirty::Clean; }
    NUILayoutDirty dirtyState() const { return dirty_; }

    /**
     * Pure: must not mutate this node's own placement or any sibling's state.
     * Returns what this node wants, given the constraint a parent (or the root)
     * posed. A node that changes its answer in response to its own arrangement
     * is a measure/arrange cycle — the contract requires that be named, not
     * silently settled by re-running until it stops oscillating.
     */
    virtual NUIMeasureResult measure(const NUIConstraint& constraint) = 0;

    /**
     * Assigns this node's final rect, in the PARENT's local space, and records
     * the trace. Overriders that need side effects from placement should call
     * this base implementation to keep the trace honest, then extend it —
     * never skip recording just because a subclass has more to do.
     */
    virtual void arrange(const NUILocalRect& finalRect, const NUILayoutNode* constraintSource, const NUIConstraint& constraintReceived) {
        bounds_ = finalRect;

        trace_.node = this;
        trace_.constraintSource = constraintSource;
        trace_.constraintReceived = constraintReceived;
        trace_.measured = lastMeasured_;
        trace_.arranged = finalRect;
        trace_.absolutelyPositioned = absolutelyPositioned_;

        dirty_ = NUILayoutDirty::Clean;
    }

    NUILocalRect bounds() const { return bounds_; }
    const NUILayoutTrace& lastTrace() const { return trace_; }

protected:
    /** Overriders call this at the end of measure() so arrange() has the "asked for" half of the trace. */
    void recordMeasured(const NUIMeasureResult& result) { lastMeasured_ = result; }

private:
    NUILayoutDirty dirty_ = NUILayoutDirty::MeasureDirty;
    bool absolutelyPositioned_ = false;
    NUILocalRect bounds_;
    NUIMeasureResult lastMeasured_;
    NUILayoutTrace trace_;
};

} // namespace Layout
} // namespace AestraUI
