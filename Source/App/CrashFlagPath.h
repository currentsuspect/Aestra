// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Authoritative crash-flag path, resolved while platform utilities are alive
// and retained for use during final shutdown (#675).
//
// The defect this exists to prevent: `AestraApp::shutdown()` clears the crash
// flag LAST, deliberately, so that a crash during any earlier teardown step
// leaves the flag behind for recovery. But `Platform::shutdown()` runs before
// that final clear and destroys the platform utilities, and
// `AestraApp::getAppDataPath()` silently falls back to the process working
// directory when those utilities are gone. The clear therefore looked for
// `<cwd>/crash_flag`, found nothing, and returned having done nothing — and
// logged nothing either, because both its success and failure messages live
// inside the `exists()` branch. Every clean exit left the flag behind, so every
// next launch offered a spurious recovery.
//
// The ordering is correct and must not change. What must change is the path
// resolution: resolve once, early, and reuse that exact string at the end.
//
// Deliberately a plain string holder with no dependency on Platform, the app,
// or the filesystem, so the behaviour is unit-testable despite Source/ having
// no linkable library target (#666).

#include <string>

namespace Aestra {

class CrashFlagPath {
public:
    /// Record the resolved path. Called while platform utilities are alive —
    /// in practice when the crash flag is written at startup.
    static void prime(std::string path) { storage() = std::move(path); }

    /// True once a path has been recorded. Callers that clear the flag must
    /// check this rather than silently resolving a fresh (and by then wrong)
    /// path.
    static bool isPrimed() { return !storage().empty(); }

    /// The recorded path. Empty when never primed.
    static const std::string& get() { return storage(); }

    /// Test-only: return to the un-primed state.
    static void resetForTesting() { storage().clear(); }

private:
    static std::string& storage() {
        static std::string path;
        return path;
    }
};

} // namespace Aestra
