// © 2026 Aestra Studios — All Rights Reserved.
//
// Regression coverage for #675: the crash flag must still be clearable after
// platform teardown has destroyed the utilities that resolve app-data paths.
//
// The original defect: AestraApp::shutdown() deliberately clears the crash flag
// LAST, so a crash during earlier teardown leaves the flag for recovery. But
// Platform::shutdown() runs first and destroys the platform utilities, and
// AestraApp::getAppDataPath() *used to* silently fall back to the process
// working directory when they were gone. The final clear therefore looked for
// <cwd>/crash_flag, found nothing, removed nothing, and logged nothing —
// because both its success and failure messages sit inside the exists() branch.
// Every clean exit left the flag behind and every next launch offered a
// spurious recovery.
//
// #676 removed that fallback: the path resolvers now return std::nullopt
// instead of substituting a plausible wrong path. Retaining the primed path
// (below) is still what makes the clear work, because after teardown there is
// now no path to resolve at all.
//
// These tests pin the property that makes the fix work: a path resolved while
// the platform was alive stays available and unchanged afterwards. Source/ has
// no linkable library target (#666), so the resolution/retention behaviour was
// deliberately factored into a dependency-free header that can be tested
// directly.

#include "CrashFlagPath.h"

#include <iostream>
#include <optional>
#include <string>

namespace {

int g_failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cout << "[FAIL] " << message << "\n";
        ++g_failures;
    }
}

/// Stands in for AestraApp::getAppDataPath(): resolves while the platform is
/// alive and yields nothing once it is gone, matching the real one since #676.
///
/// It used to model the original behaviour — degrading to the working
/// directory — because that is what the real function did. #676 removed that
/// fallback: there is no portable-mode feature that wants a working-directory
/// app-data location, so an unresolvable path is now reported as absent rather
/// than substituted.
struct FakePlatform {
    bool aliveFlag = true;
    std::string appData = "/home/user/.local/share/Aestra";

    std::optional<std::string> resolveCrashFlagPath() const {
        if (!aliveFlag) {
            return std::nullopt;
        }
        return appData + "/crash_flag";
    }
    void shutdown() { aliveFlag = false; }
};

void testUnprimedIsDetectable() {
    Aestra::CrashFlagPath::resetForTesting();
    check(!Aestra::CrashFlagPath::isPrimed(), "an un-primed path reports itself as un-primed");
    check(Aestra::CrashFlagPath::get().empty(), "an un-primed path is empty rather than a plausible wrong path");
}

void testPathSurvivesPlatformTeardown() {
    Aestra::CrashFlagPath::resetForTesting();
    FakePlatform platform;

    // Startup: resolve while the platform is alive, as writeCrashFlag() does.
    const auto resolved = platform.resolveCrashFlagPath();
    check(resolved.has_value(), "the path resolves while the platform is alive");
    Aestra::CrashFlagPath::prime(*resolved);
    const std::string atStartup = Aestra::CrashFlagPath::get();
    check(atStartup == "/home/user/.local/share/Aestra/crash_flag",
          "the primed path is the app-data path");

    // Shutdown: platform utilities are destroyed before the final clear.
    platform.shutdown();

    // Post-#676, re-resolving after teardown yields nothing at all. Before it,
    // it produced a plausible working-directory path that nothing could
    // distinguish from the real one — which is what made #675 silent.
    check(!platform.resolveCrashFlagPath().has_value(),
          "re-resolving after teardown yields nothing rather than a substitute path");

    // The retained one is unchanged, which is what the clear must use.
    check(Aestra::CrashFlagPath::isPrimed(), "the path is still primed after teardown");
    check(Aestra::CrashFlagPath::get() == atStartup,
          "the retained path is unchanged after platform teardown");
    check(!Aestra::CrashFlagPath::get().empty() && !platform.resolveCrashFlagPath().has_value(),
          "the retained path is usable precisely when fresh resolution is not");
}

void testPrimingIsIdempotentlyOverwritable() {
    // Re-priming must replace, not append or ignore: a second app instance in
    // the same process (tests, tooling) has to be able to correct the path.
    Aestra::CrashFlagPath::resetForTesting();
    Aestra::CrashFlagPath::prime("/first/crash_flag");
    Aestra::CrashFlagPath::prime("/second/crash_flag");
    check(Aestra::CrashFlagPath::get() == "/second/crash_flag",
          "re-priming replaces the retained path");
}

} // namespace

int main() {
    testUnprimedIsDetectable();
    testPathSurvivesPlatformTeardown();
    testPrimingIsIdempotentlyOverwritable();

    if (g_failures != 0) {
        std::cout << "[FAIL] CrashFlagPathTest: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "[PASS] CrashFlagPathTest\n";
    return 0;
}
