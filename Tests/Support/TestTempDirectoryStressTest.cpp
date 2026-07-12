// © 2026 Aestra Studios — All Rights Reserved.
// TestTempDirectoryStressTest — proves makeUniqueTempDirectory hands out a
// distinct, freshly-created directory to every caller, even under heavy
// concurrency. This is the regression guard for the temp-dir collision that
// previously failed ProjectRoundTripTest under `ctest -j`.

#include "TestTempDirectory.h"

#include <atomic>
#include <filesystem>
#include <iostream>
#include <set>
#include <thread>
#include <vector>

int main() {
    namespace fs = std::filesystem;

    constexpr int THREAD_COUNT = 16;
    constexpr int DIRS_PER_THREAD = 250; // 4000 directories total

    std::vector<std::vector<fs::path>> perThread(THREAD_COUNT);
    std::atomic<int> createFailures{0};

    std::vector<std::thread> workers;
    workers.reserve(THREAD_COUNT);
    for (int t = 0; t < THREAD_COUNT; ++t) {
        workers.emplace_back([&, t]() {
            perThread[t].reserve(DIRS_PER_THREAD);
            for (int i = 0; i < DIRS_PER_THREAD; ++i) {
                try {
                    fs::path p = Aestra::Tests::makeUniqueTempDirectory("Stress");
                    // Must be a real, freshly created directory.
                    if (!fs::exists(p) || !fs::is_directory(p)) {
                        createFailures.fetch_add(1, std::memory_order_relaxed);
                    }
                    perThread[t].push_back(std::move(p));
                } catch (const std::exception&) {
                    createFailures.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& w : workers) {
        w.join();
    }

    // Every path returned across all threads must be unique.
    std::set<fs::path> unique;
    size_t total = 0;
    for (const auto& v : perThread) {
        for (const auto& p : v) {
            unique.insert(p);
            ++total;
        }
    }

    bool ok = true;
    if (createFailures.load() != 0) {
        std::cout << "[FAIL] " << createFailures.load() << " directories were not created / threw\n";
        ok = false;
    }
    if (unique.size() != total) {
        std::cout << "[FAIL] collision: " << total << " created but only " << unique.size() << " distinct paths\n";
        ok = false;
    }

    // Cleanup (best effort — this test intentionally does not keep artifacts).
    for (const auto& p : unique) {
        std::error_code ec;
        fs::remove_all(p, ec);
    }

    if (!ok) {
        return 1;
    }
    std::cout << "[PASS] " << total << " unique temp directories created across " << THREAD_COUNT << " threads\n";
    std::cout << "All TestTempDirectory stress checks passed\n";
    return 0;
}
