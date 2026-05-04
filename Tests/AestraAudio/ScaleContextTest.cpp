// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Music/ScaleContext.h"
#include <cassert>
#include <iostream>

using namespace Aestra::Audio;

static int testsPassed = 0;
static int testsFailed = 0;

#define PASS(msg) do { std::cout << "PASS: " << msg << "\n"; testsPassed++; } while(0)
#define FAIL(msg) do { std::cout << "FAIL: " << msg << "\n"; testsFailed++; } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while(0)

// -----------------------------------------------------------------------------
// Test: default scale context is C / Chromatic / snap off
// -----------------------------------------------------------------------------
static void test_default_context() {
    ScaleContext ctx = ScaleContext::defaultContext();
    ASSERT(ctx.rootKey == 0, "default rootKey should be 0");
    ASSERT(ctx.scaleKind == ScaleKind::Chromatic, "default scaleKind should be Chromatic");
    ASSERT(ctx.snapToScale == false, "default snapToScale should be false");
    PASS("default scale context is C/Chromatic/snap off");
}

// -----------------------------------------------------------------------------
// Test: hasNonDefaultValues detects changes
// -----------------------------------------------------------------------------
static void test_hasNonDefaultValues() {
    ScaleContext ctx = ScaleContext::defaultContext();
    ASSERT(!ctx.hasNonDefaultValues(), "default context should have no non-default values");

    ctx.rootKey = 2;
    ASSERT(ctx.hasNonDefaultValues(), "changed rootKey should have non-default values");

    ctx = ScaleContext::defaultContext();
    ctx.scaleKind = ScaleKind::Major;
    ASSERT(ctx.hasNonDefaultValues(), "changed scaleKind should have non-default values");

    ctx = ScaleContext::defaultContext();
    ctx.snapToScale = true;
    ASSERT(ctx.hasNonDefaultValues(), "changed snapToScale should have non-default values");

    PASS("hasNonDefaultValues detects changes");
}

// -----------------------------------------------------------------------------
// Test: ScaleKind string roundtrip
// -----------------------------------------------------------------------------
static void test_scaleKind_roundtrip() {
    ASSERT(scaleKindToString(ScaleKind::Chromatic) == "chromatic", "chromatic string");
    ASSERT(scaleKindToString(ScaleKind::Major) == "major", "major string");
    ASSERT(scaleKindToString(ScaleKind::Minor) == "minor", "minor string");
    ASSERT(scaleKindToString(ScaleKind::Blues) == "blues", "blues string");

    auto chrom = scaleKindFromString("chromatic");
    ASSERT(chrom.has_value() && chrom.value() == ScaleKind::Chromatic, "chromatic roundtrip");

    auto maj = scaleKindFromString("major");
    ASSERT(maj.has_value() && maj.value() == ScaleKind::Major, "major roundtrip");

    auto unknown = scaleKindFromString("unknownScale");
    ASSERT(!unknown.has_value(), "unknown scaleKind should return nullopt");

    PASS("ScaleKind string roundtrip");
}

// -----------------------------------------------------------------------------
// Test: rootKey normalization (external code should clamp, verify behavior)
// Note: ScaleContext doesn't normalize automatically; tests verify expected range
// -----------------------------------------------------------------------------
static void test_rootKey_range() {
    // These test that values outside 0-11 are handled by callers
    // ScaleContext itself doesn't enforce the range (that's caller responsibility)
    ScaleContext ctx;
    ctx.rootKey = -1;
    ASSERT(ctx.rootKey == -1, "values outside 0-11 are stored as-is");

    ctx.rootKey = 24;
    ASSERT(ctx.rootKey == 24, "values outside 0-11 are stored as-is");

    // clampRootKey is the helper for normalization
    ASSERT(clampRootKey(0) == 0, "clamp 0");
    ASSERT(clampRootKey(11) == 11, "clamp 11");
    ASSERT(clampRootKey(-1) == 0, "clamp -1 to 0");
    ASSERT(clampRootKey(12) == 11, "clamp 12 to 11");
    ASSERT(clampRootKey(24) == 11, "clamp 24 to 11");

    PASS("rootKey range behavior");
}

// -----------------------------------------------------------------------------
// Test: ScaleContext optional behavior
// -----------------------------------------------------------------------------
static void test_optional_behavior() {
    std::optional<ScaleContext> empty;
    ASSERT(!empty.has_value(), "empty optional has no value");

    ScaleContext ctx;
    ctx.rootKey = 5;
    ctx.scaleKind = ScaleKind::Dorian;
    ctx.snapToScale = true;

    std::optional<ScaleContext> withValue = ctx;
    ASSERT(withValue.has_value(), "withValue has value");
    ASSERT(withValue->rootKey == 5, "withValue rootKey");
    ASSERT(withValue->scaleKind == ScaleKind::Dorian, "withValue scaleKind");
    ASSERT(withValue->snapToScale == true, "withValue snapToScale");

    PASS("optional behavior");
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main() {
    std::cout << "=== ScaleContext Unit Tests ===\n\n";

    test_default_context();
    test_hasNonDefaultValues();
    test_scaleKind_roundtrip();
    test_rootKey_range();
    test_optional_behavior();

    std::cout << "\n=== Results: " << testsPassed << " passed, " << testsFailed << " failed ===\n";
    return testsFailed > 0 ? 1 : 0;
}