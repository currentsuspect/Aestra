// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V8-X2b Core: coordinate-space typing and the layout trace X2b·4 requires.
//
// Two things this proves, not just exercises:
//
//   1. A value tagged with one coordinate space cannot be substituted for another
//      without going through the explicit conversion functions. That property is
//      a COMPILE-TIME one — this test cannot demonstrate a rejected program by
//      running, so the negative case is recorded here in comments, next to the
//      positive cases that prove the accepted path is still correct.
//
//   2. A synthetic parent/child layout tree's trace answers X2b·4's question —
//      which constraint reached a node, what it asked for, what it actually got,
//      and the delta between them — from data alone, not from re-reading the
//      layout code afterward. The test asserts specific numbers, not just that a
//      trace object exists; an empty or default-constructed trace would pass a
//      weaker check and answer nothing.
//
// Deliberately links nothing: NUILayoutSpace.h and NUILayoutNode.h are
// header-only, so this test builds in every configuration, including
// AESTRA_CI=ON where AestraUI_Core does not exist. See NUITransformCompositionTest
// for the precedent and the reason (FD-19's silent-skip pattern).

#include "../../AestraUI/Layout/NUILayoutNode.h"
#include "../../AestraUI/Layout/NUILayoutSpace.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace AestraUI::Layout;

namespace {

int failures = 0;

void check(bool cond, const std::string& what) {
    if (cond) {
        std::cout << "PASS: " << what << "\n";
    } else {
        std::cout << "FAIL: " << what << "\n";
        ++failures;
    }
}

constexpr float kEps = 1e-4f;
bool nearly(float a, float b) { return std::fabs(a - b) <= kEps; }

// ---------------------------------------------------------------------------
// Coordinate space conversions
// ---------------------------------------------------------------------------

void testLocalToWindowRoundTrip() {
    const NUIWindowPoint parentOrigin(100.0f, 50.0f);
    const NUILocalPoint child(12.0f, 8.0f);

    const NUIWindowPoint asWindow = localToWindow(child, parentOrigin);
    check(nearly(asWindow.x, 112.0f), "localToWindow adds the parent's window-space origin (x)");
    check(nearly(asWindow.y, 58.0f), "localToWindow adds the parent's window-space origin (y)");

    const NUILocalPoint backToLocal = windowToLocal(asWindow, parentOrigin);
    check(nearly(backToLocal.x, child.x) && nearly(backToLocal.y, child.y),
          "windowToLocal is the exact inverse of localToWindow");
}

void testRectConversionPreservesSize() {
    const NUIWindowPoint parentOrigin(260.0f, 88.0f);
    const NUILocalRect local(10.0f, 20.0f, 300.0f, 150.0f);

    const NUIWindowRect asWindow = localToWindow(local, parentOrigin);
    check(nearly(asWindow.x, 270.0f) && nearly(asWindow.y, 108.0f),
          "rect conversion moves the origin by the parent's window offset");
    check(nearly(asWindow.width, local.width) && nearly(asWindow.height, local.height),
          "rect conversion never touches size — only the origin is space-dependent");

    const NUILocalRect back = windowToLocal(asWindow, parentOrigin);
    check(nearly(back.x, local.x) && nearly(back.y, local.y) &&
              nearly(back.width, local.width) && nearly(back.height, local.height),
          "rect round-trip recovers the original local rect exactly");
}

// The negative case NUITypedPoint/NUITypedRect exist to enforce. None of the
// following compiles, which is the point — a mismatched space is a type error,
// not a runtime one this test could catch by running:
//
//   NUILocalPoint  bad1 = someWindowPoint;                 // no implicit conversion between spaces
//   NUIWindowPoint bad2 = someLocalPoint;                  // ditto, the other direction
//   bool same = (someLocalPoint.x == someWindowPoint.x);   // fine (both float); but
//   NUILocalPoint  bad3 = someLocalPoint + someWindowPoint;// no operator+ across spaces — does not exist
//   NUIWindowRect  bad4(someLocalRect.raw());               // raw() drops the tag; a fresh tag must be
//                                                            // assigned explicitly, never inferred

// ---------------------------------------------------------------------------
// A synthetic parent/child tree, and whether its trace can answer X2b·4
// ---------------------------------------------------------------------------

// A leaf that always wants a fixed size, regardless of what it's offered.
class FixedSizeNode : public NUILayoutNode {
public:
    FixedSizeNode(float w, float h) : wantW_(w), wantH_(h) {}

    NUIMeasureResult measure(const NUIConstraint& /*constraint*/) override {
        NUIMeasureResult result(wantW_, wantH_);
        recordMeasured(result);
        return result;
    }

private:
    float wantW_;
    float wantH_;
};

// A minimal horizontal stack: measures each child against the remaining width,
// then arranges them left to right. Exists only to give the trace something
// non-trivial to report — not a general layout algorithm (out of scope for
// this slice; see the spec reconstruction doc).
class HStackNode : public NUILayoutNode {
public:
    void addChild(std::shared_ptr<NUILayoutNode> child) { children_.push_back(std::move(child)); }

    NUIMeasureResult measure(const NUIConstraint& constraint) override {
        float usedWidth = 0.0f;
        float maxHeight = 0.0f;
        for (auto& child : children_) {
            NUIConstraint childConstraint(constraint.maxWidth - usedWidth, constraint.maxHeight);
            NUIMeasureResult childResult = child->measure(childConstraint);
            usedWidth += childResult.desiredWidth;
            maxHeight = std::max(maxHeight, childResult.desiredHeight);
        }
        NUIMeasureResult result(usedWidth, maxHeight);
        recordMeasured(result);
        return result;
    }

    void arrange(const NUILocalRect& finalRect, const NUILayoutNode* constraintSource, const NUIConstraint& constraintReceived) override {
        NUILayoutNode::arrange(finalRect, constraintSource, constraintReceived);

        // Left-to-right placement. A child asking for more than the remaining
        // width gets clamped here — deliberately, so the trace has a real,
        // non-zero delta to report rather than a contrived one.
        float cursorX = 0.0f;
        const float remainingWidthAtStart = finalRect.width;
        for (auto& child : children_) {
            // Re-derive what this specific child asked for from its own trace
            // is not available yet (arrange hasn't run on it) — measure was
            // already called during this node's own measure() pass above, so
            // ask again for the constraint actually posed to it here.
            float remaining = finalRect.width - cursorX;
            NUIConstraint childConstraint(remaining, finalRect.height);
            NUIMeasureResult childWanted = child->measure(childConstraint);

            float givenWidth = std::min(childWanted.desiredWidth, remaining);
            NUILocalRect childRect(cursorX, 0.0f, givenWidth, finalRect.height);
            child->arrange(childRect, this, childConstraint);

            cursorX += givenWidth;
        }
        (void)remainingWidthAtStart;
    }

private:
    std::vector<std::shared_ptr<NUILayoutNode>> children_;
};

void testTraceAnswersWhyANodeEndedUpWhereItDid() {
    // Two children wanting 220 and 150 respectively, offered only 300 total —
    // the second child cannot get what it asked for. That shortfall is exactly
    // what X2b·4 requires the trace to explain, from data.
    auto first = std::make_shared<FixedSizeNode>(220.0f, 40.0f);
    auto second = std::make_shared<FixedSizeNode>(150.0f, 40.0f);

    HStackNode stack;
    stack.addChild(first);
    stack.addChild(second);

    const NUIConstraint rootConstraint(300.0f, 40.0f);
    NUIMeasureResult stackWanted = stack.measure(rootConstraint);
    check(nearly(stackWanted.desiredWidth, 370.0f),
          "the stack's own measured width is the sum of both children's requests, unclamped");

    stack.arrange(NUILocalRect(0.0f, 0.0f, 300.0f, 40.0f), nullptr, rootConstraint);

    const NUILayoutTrace& firstTrace = first->lastTrace();
    check(nearly(firstTrace.measured.desiredWidth, 220.0f), "first child's trace records what it asked for");
    check(nearly(firstTrace.arranged.width, 220.0f), "first child got everything it asked for — no shortfall");
    check(!firstTrace.hasDelta(), "first child's trace correctly reports no delta to explain");
    check(firstTrace.constraintSource == &stack, "first child's trace names the stack as the constraint source");

    const NUILayoutTrace& secondTrace = second->lastTrace();
    check(nearly(secondTrace.measured.desiredWidth, 150.0f), "second child's trace records what IT asked for");
    check(nearly(secondTrace.arranged.width, 80.0f),
          "second child's trace records what it actually got — clamped by the first child taking 220 of 300");
    check(nearly(secondTrace.widthDelta(), -70.0f),
          "the trace's delta is the exact, signed shortfall (80 - 150) — this is the number that answers "
          "\"why is it there\": not narrated, read directly off the trace");
    check(secondTrace.hasDelta(), "a real shortfall is flagged as a delta worth explaining");
    check(secondTrace.constraintSource == &stack, "second child's trace also names the stack, not the root");
}

void testDirtyStateTransitions() {
    FixedSizeNode node(10.0f, 10.0f);
    check(node.dirtyState() == NUILayoutDirty::MeasureDirty, "a freshly constructed node starts MeasureDirty");

    node.measure(NUIConstraint(100.0f, 100.0f));
    node.arrange(NUILocalRect(0.0f, 0.0f, 10.0f, 10.0f), nullptr, NUIConstraint(100.0f, 100.0f));
    check(node.dirtyState() == NUILayoutDirty::Clean, "arrange() clears dirty state to Clean");

    node.markArrangeDirty();
    check(node.dirtyState() == NUILayoutDirty::ArrangeDirty, "markArrangeDirty sets ArrangeDirty from Clean");

    node.markMeasureDirty();
    check(node.dirtyState() == NUILayoutDirty::MeasureDirty,
          "MeasureDirty supersedes ArrangeDirty — a node needing full re-measurement is not merely re-arrange-dirty");

    // ArrangeDirty must never downgrade a MeasureDirty node — a pending full
    // re-measurement is a strictly bigger obligation than a pending re-arrange.
    node.markArrangeDirty();
    check(node.dirtyState() == NUILayoutDirty::MeasureDirty,
          "markArrangeDirty does not downgrade an already MeasureDirty node");
}

void testAbsolutePositioningIsExemptButStillTraced() {
    FixedSizeNode overlay(999.0f, 999.0f); // deliberately absurd — a piano-roll marker doesn't answer to a parent's offer
    overlay.setAbsolutelyPositioned(true);
    check(overlay.isAbsolutelyPositioned(), "the escape hatch is queryable, not just a silent behavioural switch");

    overlay.measure(NUIConstraint(10.0f, 10.0f)); // a tiny constraint an absolute node may freely ignore
    overlay.arrange(NUILocalRect(500.0f, 500.0f, 999.0f, 999.0f), nullptr, NUIConstraint(10.0f, 10.0f));

    const NUILayoutTrace& trace = overlay.lastTrace();
    check(trace.absolutelyPositioned, "an absolutely positioned node's trace records that fact");
    check(nearly(trace.arranged.width, 999.0f),
          "an absolute node is exempt from its parent's constraint — the escape hatch works — "
          "but it is not exempt from being traced, which is what keeps the hatch from losing observability");
}

} // namespace

int main() {
    std::cout << "=== V8-X2b core: coordinate spaces and the layout trace ===\n";

    testLocalToWindowRoundTrip();
    testRectConversionPreservesSize();
    testTraceAnswersWhyANodeEndedUpWhereItDid();
    testDirtyStateTransitions();
    testAbsolutePositioningIsExemptButStillTraced();

    if (failures != 0) {
        std::cout << "\n" << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "\nall checks passed\n";
    return 0;
}
