// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// PluginScannerSnapshotTest — the scanned-plugin catalog is handed out as a snapshot, never as a reference into
// the scanner (CodeRabbit on #1493).
//
// getScannedPlugins() used to return `const std::vector<PluginInfo>&` after releasing the scanner's mutex. A second
// scan assigns m_scannedPlugins on its worker thread, so a UI refresh iterating the returned reference could have
// its storage freed underneath it. A by-value return copies under the lock: each caller owns its list, and the
// writer can no longer affect it. The contract is the return type, so the first check is compile-time.

#include "Plugin/PluginScanner.h"

#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using Aestra::Audio::PluginInfo;
using Aestra::Audio::PluginScanner;

static_assert(!std::is_reference_v<decltype(std::declval<const PluginScanner&>().getScannedPlugins())>,
              "getScannedPlugins() must return a snapshot (by value), not a reference into the scanner");

namespace {
int g_failures = 0;

void check(bool condition, const std::string& label) {
    if (!condition) {
        std::cerr << "FAIL: " << label << "\n";
        ++g_failures;
    }
}
} // namespace

int main() {
    PluginScanner scanner;
    scanner.clearSearchPaths(); // nothing on disk: the catalog is just whatever the scanner merges in by itself
    scanner.scanBlocking();

    const std::vector<PluginInfo> snapshot = scanner.getScannedPlugins();
    const size_t before = snapshot.size();

    scanner.scanBlocking(); // replaces the scanner's own catalog
    check(snapshot.size() == before, "a snapshot is unaffected by a later scan replacing the catalog");
    check(scanner.getScannedPlugins().size() == before, "an empty search list scans to the same catalog every time");

    if (g_failures > 0) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "PluginScannerSnapshotTest: all checks passed\n";
    return 0;
}
