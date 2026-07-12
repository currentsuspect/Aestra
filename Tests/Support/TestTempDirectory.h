// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Race-free temporary-directory allocation for tests.
//
// Several test binaries are registered more than once in CTest (e.g.
// ProjectRoundTripTest / AutosaveRoundTripTest run the same executable), and
// many tests run in parallel under `ctest -j`. The historical per-test
// allocator did `exists(candidate)` then `create_directories(candidate)` on a
// fixed, entropy-free name, so two concurrent processes could select the same
// directory and clobber each other mid atomic-write (surfacing as an LSan/CI
// failure). This helper removes that whole class of failure:
//
//   * The ownership decision is the atomic result of create_directory() —
//     mkdir(2)/CreateDirectory succeeds for exactly one caller; the loser
//     retries the next candidate. No exists()-before-create TOCTOU window.
//   * A per-call token (steady_clock timestamp + random_device + an in-process
//     atomic counter) makes collisions vanishingly unlikely across processes
//     and threads, so the atomic create almost never has to retry. The token
//     only reduces contention; create_directory is the correctness guarantee.
//   * Failure is reported explicitly (throw) rather than silently reusing a
//     shared fallback directory that could reintroduce sharing.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>

namespace Aestra::Tests {

// Upper bound on create_directory retries; with the per-call token a single
// attempt essentially always wins, so this is only a safety net.
inline constexpr int MAX_TEMP_DIR_ATTEMPTS = 4096;

// Root under the system temp dir shared by all Aestra tests. Unique per-call
// subdirectories live beneath it.
inline std::filesystem::path testTempRoot() {
    return std::filesystem::temp_directory_path() / "Aestra_tests";
}

// Process/thread-unique token: monotonic clock + non-deterministic entropy +
// an in-process counter. Never relied on alone — the atomic create below is
// the actual guard — but it keeps the create loop from ever contending.
inline std::string uniqueTempToken() {
    static std::atomic<std::uint64_t> counter{0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::random_device rd;
    const std::uint64_t entropy = (static_cast<std::uint64_t>(rd()) << 32) ^ static_cast<std::uint64_t>(rd());
    const std::uint64_t seq = counter.fetch_add(1, std::memory_order_relaxed);
    std::string token = std::to_string(static_cast<std::uint64_t>(now));
    token += '_';
    token += std::to_string(entropy);
    token += '_';
    token += std::to_string(seq);
    return token;
}

// Create and return a unique temporary directory named "<prefix>_<token>_<n>".
// Throws std::runtime_error if a fresh directory cannot be created (should be
// unreachable given the token, but never silently shares a directory).
inline std::filesystem::path makeUniqueTempDirectory(const std::string& prefix) {
    namespace fs = std::filesystem;
    const fs::path base = testTempRoot();
    std::error_code ec;
    fs::create_directories(base, ec); // idempotent; only the leaf below must be exclusive

    const std::string token = uniqueTempToken();
    for (int attempt = 0; attempt < MAX_TEMP_DIR_ATTEMPTS; ++attempt) {
        std::string name = prefix;
        name += '_';
        name += token;
        name += '_';
        name += std::to_string(attempt);
        fs::path candidate = base / name;
        std::error_code createEc;
        // create_directory returns true only if *this* call created it; a
        // concurrent creator gets false (already exists) and we try the next.
        if (fs::create_directory(candidate, createEc) && !createEc) {
            return candidate;
        }
    }
    throw std::runtime_error("makeUniqueTempDirectory: could not create a unique directory for prefix '" + prefix +
                             "'");
}

// RAII wrapper that removes its directory on destruction, unless the
// AESTRA_KEEP_TEST_ARTIFACTS environment variable is set (to anything other
// than "0"/empty) so a failing test's files can be inspected.
class ScopedTempDirectory {
public:
    explicit ScopedTempDirectory(const std::string& prefix) : m_path(makeUniqueTempDirectory(prefix)) {}
    ~ScopedTempDirectory() {
        if (keepArtifacts()) {
            return;
        }
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }

    ScopedTempDirectory(const ScopedTempDirectory&) = delete;
    ScopedTempDirectory& operator=(const ScopedTempDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return m_path; }
    operator const std::filesystem::path&() const { return m_path; }

private:
    static bool keepArtifacts() {
        const char* v = std::getenv("AESTRA_KEEP_TEST_ARTIFACTS");
        return v != nullptr && v[0] != '\0' && std::string(v) != "0";
    }
    std::filesystem::path m_path;
};

} // namespace Aestra::Tests
