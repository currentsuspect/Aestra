// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V8-X2b Step 0: the renderer transform stack composes as one affine map, and
// geometry and clipping consume that same map.
//
// Before this, NUIRendererGL walked the stack twice in two places, in opposite
// operation orders:
//
//   applyTransform()  (geometry)  x += t.tx;  x *= t.scale;   // translate, scale
//   setClipRect()     (clipping)  x *= t.scale;  x += t.tx;    // scale, translate
//
// Neither is affine composition, and they disagree with each other for every
// scale != 1 — a scissor rectangle in a different space than the pixels it was
// clipping. Nothing observed it, because all five production call sites pass
// scale 1.0, the one configuration where all three answers coincide.
//
// That is exactly why the cases below use a scale other than 1.0. A test written
// at scale 1.0 passes against both broken implementations and proves nothing;
// kLegacy* below exist to demonstrate that this test's configuration really does
// separate the correct answer from the two it replaces.
//
// This test deliberately links nothing. NUITransformStack.h is dependency-free
// so the test compiles as a plain target: UI tests that link AestraUI_Core are
// skipped in CI (AESTRA_CI=ON forces AESTRA_ENABLE_UI=OFF, the target does not
// exist, and the registration falls through a status message), so a test written
// that way could not fail this build — FD-19's silent-skip pattern.

#include "../../AestraUI/Graphics/NUITransformStack.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using AestraUI::composeTransformStack;
using AestraUI::NUIComposedTransform;
using AestraUI::NUITransform2D;

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

bool nearly(float a, float b) {
    return std::fabs(a - b) <= kEps;
}

void checkNear(float actual, float expected, const std::string& what) {
    const bool ok = nearly(actual, expected);
    if (!ok) {
        std::cout << "  expected " << expected << ", got " << actual << "\n";
    }
    check(ok, what);
}

// ---------------------------------------------------------------------------
// An independent reference implementation.
//
// The production code folds the stack into (scale, tx, ty) in a single pass.
// Comparing that against itself proves nothing, so the reference below composes
// actual 2x3 affine matrices and multiplies them — a different route to the same
// answer. If the fold is ever "simplified" into something that is not affine
// composition, these two stop agreeing.
// ---------------------------------------------------------------------------
struct Mat2x3 {
    // | a  0  tx |   uniform scale only, no shear or rotation
    // | 0  a  ty |
    float a = 1.0f;
    float tx = 0.0f;
    float ty = 0.0f;
};

// outer applied after inner: result(p) = outer(inner(p))
Mat2x3 multiply(const Mat2x3& outer, const Mat2x3& inner) {
    Mat2x3 m;
    m.a = outer.a * inner.a;
    m.tx = (outer.a * inner.tx) + outer.tx;
    m.ty = (outer.a * inner.ty) + outer.ty;
    return m;
}

Mat2x3 referenceCompose(const std::vector<NUITransform2D>& stack) {
    Mat2x3 acc; // identity
    for (const auto& level : stack) {
        Mat2x3 m;
        m.a = level.scale;
        m.tx = level.tx;
        m.ty = level.ty;
        // stack[0] is outermost, so each new level is applied *inside* what we
        // have so far.
        acc = multiply(acc, m);
    }
    return acc;
}

// ---------------------------------------------------------------------------
// The two implementations this replaces, preserved verbatim so the test can
// show its own configuration is sensitive to the defect.
// ---------------------------------------------------------------------------
float kLegacyGeometryX(const std::vector<NUITransform2D>& stack, float x) {
    for (const auto& t : stack) {
        x += t.tx;
        x *= t.scale;
    }
    return x;
}

float kLegacyClipX(const std::vector<NUITransform2D>& stack, float x) {
    for (const auto& t : stack) {
        x *= t.scale;
        x += t.tx;
    }
    return x;
}

// ---------------------------------------------------------------------------

void testEmptyStackIsIdentity() {
    const NUIComposedTransform c = composeTransformStack(std::vector<NUITransform2D>{});
    check(c.isIdentity(), "empty stack composes to identity");

    float x = 7.0f;
    float y = -3.0f;
    c.applyToPoint(x, y);
    checkNear(x, 7.0f, "identity leaves x alone");
    checkNear(y, -3.0f, "identity leaves y alone");
}

void testSingleTranslate() {
    const std::vector<NUITransform2D> stack{{10.0f, 20.0f, 1.0f}};
    const NUIComposedTransform c = composeTransformStack(stack);

    float x = 5.0f;
    float y = 5.0f;
    c.applyToPoint(x, y);
    checkNear(x, 15.0f, "single translate moves x by tx");
    checkNear(y, 25.0f, "single translate moves y by ty");
    check(!c.isIdentity(), "a real translate is not reported as identity");
}

// The configuration every production call site actually uses today. It must keep
// working — but note it cannot detect the defect, which is the whole point of
// the scaled case that follows.
void testNestedTranslatesAtUnitScale() {
    const std::vector<NUITransform2D> stack{
        {100.0f, 0.0f, 1.0f}, // outer
        {10.0f, 0.0f, 1.0f},  // inner
    };
    const NUIComposedTransform c = composeTransformStack(stack);

    float x = 5.0f;
    float y = 0.0f;
    c.applyToPoint(x, y);
    checkNear(x, 115.0f, "nested translates at scale 1 simply add");

    check(nearly(kLegacyGeometryX(stack, 5.0f), 115.0f) &&
              nearly(kLegacyClipX(stack, 5.0f), 115.0f),
          "at scale 1 both legacy orders agree — so a scale-1 test is vacuous");
}

// The case that matters. Outer scale 2, inner translate 10.
//
//   correct:          p -> 2*p + (100 + 2*10) = 2*p + 120
//   legacy geometry:  ((p + 100) * 2 + 10) * 1
//   legacy clip:      ((p * 2 + 100) * 1 + 10)
void testNestedWithScaleSeparatesTheThreeAnswers() {
    const std::vector<NUITransform2D> stack{
        {100.0f, 0.0f, 2.0f}, // outer: translate 100, then scale everything inside by 2
        {10.0f, 0.0f, 1.0f},  // inner: translate 10, in the outer's scaled space
    };
    const NUIComposedTransform c = composeTransformStack(stack);

    checkNear(c.scale, 2.0f, "composed scale is the product of the levels");
    checkNear(c.tx, 120.0f, "inner translation is scaled by the outer level, outer is not");

    float x = 5.0f;
    float y = 0.0f;
    c.applyToPoint(x, y);
    checkNear(x, 130.0f, "point maps through the composed affine transform");

    // Prove this configuration is bug-sensitive: all three answers differ, so a
    // regression to either legacy order fails the assertions above rather than
    // sliding through.
    const float legacyGeom = kLegacyGeometryX(stack, 5.0f);
    const float legacyClip = kLegacyClipX(stack, 5.0f);
    check(!nearly(legacyGeom, 130.0f), "legacy geometry order would fail this case");
    check(!nearly(legacyClip, 130.0f), "legacy clip order would fail this case");
    check(!nearly(legacyGeom, legacyClip),
          "the two legacy orders disagreed with each other — the defect being fixed");
}

// The regression that motivated Step 0: a clip rectangle and the geometry inside
// it must land in the same place. They now share one composition, so this checks
// the property end to end rather than the implementation.
void testClipRectAgreesWithGeometry() {
    const std::vector<NUITransform2D> stack{
        {40.0f, 15.0f, 1.5f},
        {8.0f, 4.0f, 2.0f},
    };
    const NUIComposedTransform c = composeTransformStack(stack);

    // A rect, transformed as a rect.
    float x1 = 10.0f;
    float y1 = 10.0f;
    float x2 = 30.0f;
    float y2 = 20.0f;
    c.applyToRect(x1, y1, x2, y2);

    // Its corners, transformed as ordinary geometry.
    float cx1 = 10.0f;
    float cy1 = 10.0f;
    float cx2 = 30.0f;
    float cy2 = 20.0f;
    c.applyToPoint(cx1, cy1);
    c.applyToPoint(cx2, cy2);

    checkNear(x1, cx1, "clip left matches geometry");
    checkNear(y1, cy1, "clip top matches geometry");
    checkNear(x2, cx2, "clip right matches geometry");
    checkNear(y2, cy2, "clip bottom matches geometry");

    // And the rect genuinely scaled — otherwise the agreement above could hold
    // trivially with a transform that did nothing.
    const float composedScale = 1.5f * 2.0f;
    checkNear(x2 - x1, 20.0f * composedScale, "clip width scaled with the geometry");
    checkNear(y2 - y1, 10.0f * composedScale, "clip height scaled with the geometry");
}

void testMatchesIndependentMatrixComposition() {
    const std::vector<std::vector<NUITransform2D>> cases{
        {},
        {{3.0f, -4.0f, 1.0f}},
        {{100.0f, 0.0f, 2.0f}, {10.0f, 5.0f, 1.0f}},
        {{5.0f, 5.0f, 0.5f}, {20.0f, -10.0f, 4.0f}, {1.0f, 2.0f, 1.25f}},
        {{-60.0f, -12.0f, 1.0f}, {0.0f, 0.0f, 3.0f}, {7.0f, 7.0f, 0.25f}},
    };

    for (std::size_t i = 0; i < cases.size(); ++i) {
        const NUIComposedTransform c = composeTransformStack(cases[i]);
        const Mat2x3 ref = referenceCompose(cases[i]);
        const std::string tag = " (case " + std::to_string(i) + ")";

        checkNear(c.scale, ref.a, "fold matches matrix composition: scale" + tag);
        checkNear(c.tx, ref.tx, "fold matches matrix composition: tx" + tag);
        checkNear(c.ty, ref.ty, "fold matches matrix composition: ty" + tag);
    }
}

} // namespace

int main() {
    std::cout << "=== NUI transform composition (V8-X2b Step 0) ===\n";

    testEmptyStackIsIdentity();
    testSingleTranslate();
    testNestedTranslatesAtUnitScale();
    testNestedWithScaleSeparatesTheThreeAnswers();
    testClipRectAgreesWithGeometry();
    testMatchesIndependentMatrixComposition();

    if (failures != 0) {
        std::cout << "\n" << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "\nall checks passed\n";
    return 0;
}
