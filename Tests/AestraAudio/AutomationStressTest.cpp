// © 2025 Aestra Studios — All Rights Reserved.
// Standalone stress test - no gtest dependency
#include "AutomationCurve.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace Aestra::Audio;

static int testsPassed = 0;
static int testsFailed = 0;

#define EXPECT_NO_CRASH(code) do { \
    try { code; } catch (...) { printf("FAIL: Exception in %s\n", #code); testsFailed++; return; } \
    printf("PASS: " #code "\n"); \
} while(0)

#define EXPECT_FLOAT_EQ(a, b, epsilon) do { \
    if (std::abs((a) - (b)) > (epsilon)) { \
        printf("FAIL: %s != %s (got %.4f, expected %.4f)\n", #a, #b, (float)(a), (float)(b)); \
        testsFailed++; return; \
    } \
    printf("PASS: %s == %s\n", #a, #b); \
} while(0)

// ============================================================================
// 2a-I: Missing fields (empty JSON - default construction)
void test_empty_construction() {
    EXPECT_NO_CRASH(
        AutomationCurve curve;
        auto pts = curve.getPoints();
        if (pts.size() != 0) { printf("FAIL: expected 0 points\n"); testsFailed++; return; }
    );
    testsPassed++;
}

// 2a-II: Wrong types in assignment
void test_wrong_types() {
    EXPECT_NO_CRASH(
        AutomationCurve curve;
        curve.target = AutomationTarget::Volume;
        if (curve.getTarget() != 0.0) { printf("FAIL: Volume != 0\n"); testsFailed++; return; }
    );
    // Set via double cast (wrong type)
    EXPECT_NO_CRASH(
        AutomationCurve curve2;
        curve2.target = static_cast<AutomationTarget>(255);
        if (curve2.getTarget() != 255.0) { printf("FAIL: custom != 255\n"); testsFailed++; return; }
    );
    testsPassed++;
}

// 2a-III: Out-of-range values
void test_out_of_range() {
    AutomationCurve curve;

    // Out-of-range default value
    EXPECT_NO_CRASH(curve.setDefaultValue(2.0f));
    EXPECT_FLOAT_EQ(curve.getDefaultValue(), 2.0f, 0.001f);

    EXPECT_NO_CRASH(curve.setDefaultValue(-5.0f));
    EXPECT_FLOAT_EQ(curve.getDefaultValue(), -5.0f, 0.001f);

    // Huge value in addPoint
    EXPECT_NO_CRASH(curve.addPoint(1.0, 100.0f, 480.0));
    EXPECT_NO_CRASH(curve.addPoint(2.0, -100.0f, 480.0));
    if (curve.getPoints().size() != 2) { printf("FAIL: expected 2 points\n"); testsFailed++; return; }
    testsPassed++;
}

// 2a-IV: Sample rate mismatch (different SPB)
void test_sample_rate_mismatch() {
    AutomationCurve curve;
    EXPECT_NO_CRASH(curve.addPoint(1.0, 0.5f, 480.0));
    EXPECT_NO_CRASH(curve.addPoint(2.0, 0.75f, 480.0));

    // Query at same beat, diff sample rate
    // Note: At 441 spb, beat 1.5 = sample 661.5, between points at 480 & 960
    // Linear interp: 0.5 + (661.5-480)/(960-480)*(0.75-0.5) = 0.594
    EXPECT_NO_CRASH(float v1 = curve.getValueAtBeat(1.0, 441.0));
    EXPECT_FLOAT_EQ(curve.getValueAtBeat(1.0, 441.0), 0.5f, 0.01f);

    EXPECT_NO_CRASH(float v2 = curve.getValueAtBeat(1.5, 441.0));
    EXPECT_FLOAT_EQ(curve.getValueAtBeat(1.5, 441.0), 0.594f, 0.01f);

    // Out of range queries
    EXPECT_NO_CRASH(float v3 = curve.getValueAtBeat(1000.0, 441.0));
    EXPECT_FLOAT_EQ(curve.getValueAtBeat(1000.0, 441.0), 0.75f, 0.001f);
    EXPECT_NO_CRASH(float v4 = curve.getValueAtBeat(-10.0, 441.0));
    // Negative beat uses first point or defaultValue
    testsPassed++;
}

// Zero samplesPerBeat (division by zero)
void test_zero_samples_per_beat() {
    AutomationCurve curve;
    EXPECT_NO_CRASH(curve.addPoint(1.0, 0.5f, 100.0));
    // Zero causes div by zero but catch should handle
    EXPECT_NO_CRASH(float v = curve.getValueAtBeat(1.0, 0.0));
    testsPassed++;
}

// Empty points query
void test_empty_curve() {
    AutomationCurve curve;
    EXPECT_FLOAT_EQ(curve.getValueAtBeat(1.0, 480.0), curve.getDefaultValue(), 0.001f);
    testsPassed++;
}

// RemovePoint out of bounds
void test_remove_out_of_bounds() {
    AutomationCurve curve;
    EXPECT_NO_CRASH(curve.addPoint(1.0, 0.5f, 480.0));
    EXPECT_NO_CRASH(curve.removePoint(10)); // Out of bounds
    EXPECT_NO_CRASH(curve.removePoint(static_cast<size_t>(-1))); // Wrap
    if (curve.getPoints().size() != 1) { printf("FAIL: point removed\n"); testsFailed++; return; }
    testsPassed++;
}

// 2a-VIII: Invalid target enum value (non-fatal, preserved as-is)
void test_invalid_target_enum() {
    // An unrecognized AutomationTarget value should be stored and queryable.
    // The renderer must handle unknown targets gracefully (e.g., skip).
    AutomationCurve curve("unknown_param", static_cast<AutomationTarget>(999));
    curve.setDefaultValue(0.75f);
    curve.addPoint(1.0, 0.5f, 480.0);

    if (curve.getAutomationTarget() != static_cast<AutomationTarget>(999)) {
        printf("FAIL: target enum mismatch\n"); testsFailed++; return;
    }
    if (curve.getPoints().size() != 1) { printf("FAIL: expected 1 point\n"); testsFailed++; return; }
    testsPassed++;
}

// 2a-IX: Curve with no points survives serialization round-trip concept
void test_empty_curve_survival() {
    AutomationCurve curve("pan", AutomationTarget::Pan);
    curve.setDefaultValue(0.0f);
    if (curve.getPoints().size() != 0) { printf("FAIL: expected 0 points\n"); testsFailed++; return; }
    if (curve.getValueAtBeat(1.0, 480.0) != curve.getDefaultValue()) {
        printf("FAIL: empty curve value should equal default\n"); testsFailed++; return;
    }
    testsPassed++;
}

// 2a-X: Orphaned automation target survives (semantic survival)
void test_orphan_target_survival() {
    // Automation for a removed/missing target should still be loadable
    // and round-trippable — the renderer is responsible for skipping unknown targets.
    AutomationCurve curve("deleted_plugin_param", static_cast<AutomationTarget>(511));
    curve.setDefaultValue(0.25f);
    curve.addPoint(0.0, 0.5f, 480.0);
    curve.addPoint(2.0, 0.75f, 480.0);
    if (curve.getPoints().size() != 2) { printf("FAIL: expected 2 points\n"); testsFailed++; return; }
    testsPassed++;
}

int main() {
    printf("=== Automation Deserialization Stress Tests ===\n\n");

    printf("2a-I: Empty construction\n");
    test_empty_construction();

    printf("\n2a-II: Wrong types\n");
    test_wrong_types();

    printf("\n2a-III: Out-of-range values\n");
    test_out_of_range();

    printf("\n2a-IV: Sample rate mismatch\n");
    test_sample_rate_mismatch();

    printf("\n2a-V: Zero samplesPerBeat\n");
    test_zero_samples_per_beat();

    printf("\n2a-VI: Empty curve default\n");
    test_empty_curve();

    printf("\n2a-VII: Remove out of bounds\n");
    test_remove_out_of_bounds();

    printf("\n2a-VIII: Invalid target enum\n");
    test_invalid_target_enum();

    printf("\n2a-IX: Empty curve survival\n");
    test_empty_curve_survival();

    printf("\n2a-X: Orphan target survival\n");
    test_orphan_target_survival();

    printf("\n=== Results: %d passed, %d failed ===\n", testsPassed, testsFailed);
    return testsFailed > 0 ? 1 : 0;
}