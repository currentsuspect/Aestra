// © 2026 Aestra Studios — All Rights Reserved.
//
// Regression coverage for #675: the crash flag must still be clearable after
// platform teardown has destroyed the utilities that resolve app-data paths.
//
// The live defect: AestraApp::shutdown() deliberately clears the crash flag
// LAST, so a crash during earlier teardown leaves the flag for recovery. But
// Platform::shutdown() runs first and destroys the platform utilities, and
// AestraApp::getAppDataPath() silently falls back to the process working
// directory when they are gone. The final clear therefore looked for
// <cwd>/crash_flag, found nothing, removed nothing, and logged nothing —
// because both its success and failure messages sit inside the exists() branch.
// Every clean exit left the flag behind and every next launch offered a
// spurious recovery.
//
// These tests pin the property that makes the fix work: a path resolved while
// the platform was alive stays available and unchanged afterwards. Source/ has
// no linkable library target (#666), so the resolution/retention behaviour was
// deliberately factored into a dependency-free header that can be tested
// directly.

#include "CrashFlagPath.h"

#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cout << "[FAIL] " << message << "\n";
        ++g_failures;
    }
}

/// Stands in for AestraApp::getAppDataPath(): resolves correctly while the
/// platform is alive, and silently degrades to the working directory once it is
/// gone — which is exactly what the real one does.
struct FakePlatform {
    bool aliveFlag = true;
    std::string appData = "/home/user/.local/share/Aestra";
    std::string cwd = "/some/working/dir";

    std::string resolveCrashFlagPath() const {
        return (aliveFlag ? appData : cwd) + "/crash_flag";
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
    Aestra::CrashFlagPath::prime(platform.resolveCrashFlagPath());
    const std::string atStartup = Aestra::CrashFlagPath::get();
    check(atStartup == "/home/user/.local/share/Aestra/crash_flag",
          "the primed path is the app-data path");

    // Shutdown: platform utilities are destroyed before the final clear.
    platform.shutdown();

    // The regression: re-resolving now yields the wrong path...
    check(platform.resolveCrashFlagPath() == "/some/working/dir/crash_flag",
          "re-resolving after teardown does produce the wrong path (defect premise)");

    // ...but the retained one is unchanged, which is what the clear must use.
    check(Aestra::CrashFlagPath::isPrimed(), "the path is still primed after teardown");
    check(Aestra::CrashFlagPath::get() == atStartup,
          "the retained path is unchanged after platform teardown");
    check(Aestra::CrashFlagPath::get() != platform.resolveCrashFlagPath(),
          "the retained path differs from what post-teardown resolution would give");
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
