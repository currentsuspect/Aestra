// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// ProjectHistoryCapTest
//
// Guards #274: the per-project .history directory must stay bounded. Each save
// writes a history snapshot; without a cap a large project could grow history
// to gigabytes. ProjectSerializer::setHistoryLimits() configures a count cap
// and a cumulative-byte cap, and pruneHistorySnapshots keeps only the newest
// snapshots within both. This verifies both caps, with the newest always kept.

#include "../../Source/Core/ProjectSerializer.h"
#include "Models/TrackManager.h"

#include "../Support/TestTempDirectory.h"

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace fs = std::filesystem;

namespace {

int gFailures = 0;

void check(bool cond, const std::string& label) {
    std::cout << "  " << (cond ? "PASS " : "FAIL ") << label << "\n";
    if (!cond) {
        ++gFailures;
    }
}

// Count *.aes snapshots and total bytes in a project's .history directory.
struct DirStats {
    size_t count = 0;
    uintmax_t bytes = 0;
};

DirStats historyStats(const std::string& projectPath) {
    DirStats stats;
    const fs::path dir = ProjectSerializer::getHistoryDirectory(projectPath);
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        return stats;
    }
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".aes") {
            ++stats.count;
            std::error_code sec;
            stats.bytes += fs::file_size(entry.path(), sec);
        }
    }
    return stats;
}

} // namespace

int main() {
    using namespace Aestra::Audio;

    const Aestra::Tests::ScopedTempDirectory tempScope{"ProjectHistoryCap"};

    // --- Count cap: keep at most N newest snapshots ---------------------------
    {
        const auto projectPath = (tempScope.path() / "count.aes").string();
        ProjectSerializer::setHistoryLimits(/*maxEntries=*/3, /*maxTotalBytes=*/1ull << 40); // huge byte cap

        auto tm = std::make_shared<TrackManager>();
        for (int i = 0; i < 8; ++i) {
            const bool ok = ProjectSerializer::save(projectPath, tm, 120.0, 0.0);
            check(ok, "save " + std::to_string(i) + " succeeded");
        }

        const DirStats stats = historyStats(projectPath);
        check(stats.count == 3, "count cap retains exactly maxEntries snapshots (got " +
                                    std::to_string(stats.count) + ")");
    }

    // --- Byte cap: prune oldest until under the cumulative budget -------------
    {
        const auto projectPath = (tempScope.path() / "bytes.aes").string();

        // First measure a single snapshot's size, then set a byte cap that fits
        // ~2 snapshots so a third forces size-based pruning.
        ProjectSerializer::setHistoryLimits(/*maxEntries=*/1000, /*maxTotalBytes=*/1ull << 40);
        auto tm = std::make_shared<TrackManager>();
        ProjectSerializer::save(projectPath, tm, 120.0, 0.0);
        const uintmax_t oneSnapshot = historyStats(projectPath).bytes;
        check(oneSnapshot > 0, "a saved snapshot has non-zero size");

        // Budget for ~2 snapshots.
        ProjectSerializer::setHistoryLimits(/*maxEntries=*/1000, /*maxTotalBytes=*/oneSnapshot * 2 + oneSnapshot / 2);
        for (int i = 0; i < 6; ++i) {
            ProjectSerializer::save(projectPath, tm, 120.0, 0.0);
        }

        const DirStats stats = historyStats(projectPath);
        check(stats.bytes <= oneSnapshot * 2 + oneSnapshot / 2,
              "byte cap keeps cumulative history within budget (" + std::to_string(stats.bytes) +
                  " <= " + std::to_string(oneSnapshot * 2 + oneSnapshot / 2) + ")");
        check(stats.count >= 1, "at least the newest snapshot is retained");
        check(stats.count <= 3, "byte cap pruned older snapshots (kept " + std::to_string(stats.count) + ")");
    }

    // --- Zero cap is rejected (guards against disabling history entirely) -----
    {
        const auto projectPath = (tempScope.path() / "zero.aes").string();
        ProjectSerializer::setHistoryLimits(/*maxEntries=*/0, /*maxTotalBytes=*/0);
        auto tm = std::make_shared<TrackManager>();
        for (int i = 0; i < 3; ++i) {
            ProjectSerializer::save(projectPath, tm, 120.0, 0.0);
        }
        const DirStats stats = historyStats(projectPath);
        check(stats.count >= 1, "zero limits fall back to defaults, history is not wiped");
    }

    // Restore production defaults for any later tests in the same process.
    ProjectSerializer::setHistoryLimits(50, 512ull * 1024ull * 1024ull);

    if (gFailures == 0) {
        std::cout << "ProjectHistoryCapTest: PASS\n";
        return 0;
    }
    std::cout << "ProjectHistoryCapTest: FAIL (" << gFailures << ")\n";
    return 1;
}
