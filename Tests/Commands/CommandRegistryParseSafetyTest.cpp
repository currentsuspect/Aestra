// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// CommandRegistry Parse Safety Tests
// Tests: safeStoi, safeStof, safeStoull, requireFlag — malformed input handling

#include <cctype>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

// IMPORTANT: Keep these helpers in sync with CommandRegistry.cpp
// Last synced: 2026-05-19 (PR #226)
// If CommandRegistry.cpp helpers change, mirror those changes here.
// (Consider extracting to a shared header if more tests need these.)
namespace Aestra {
namespace Audio {
namespace {

std::optional<std::string_view> requireFlag(const std::unordered_map<std::string, std::string>& flags, const char* key) {
    auto it = flags.find(key);
    if (it == flags.end()) return std::nullopt;
    return it->second;
}

std::optional<int> safeStoi(std::string_view s) {
    if (s.empty()) return std::nullopt;
    if (std::isspace(static_cast<unsigned char>(s.front())) ||
        std::isspace(static_cast<unsigned char>(s.back()))) return std::nullopt;
    try {
        size_t pos = 0;
        int val = std::stoi(std::string(s), &pos);
        if (pos != s.size()) return std::nullopt;
        return val;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<float> safeStof(std::string_view s) {
    if (s.empty()) return std::nullopt;
    if (std::isspace(static_cast<unsigned char>(s.front())) ||
        std::isspace(static_cast<unsigned char>(s.back()))) return std::nullopt;
    try {
        size_t pos = 0;
        float val = std::stof(std::string(s), &pos);
        if (pos != s.size()) return std::nullopt;
        if (!std::isfinite(val)) return std::nullopt;
        return val;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<uint64_t> safeStoull(std::string_view s) {
    if (s.empty()) return std::nullopt;
    if (std::isspace(static_cast<unsigned char>(s.front())) ||
        std::isspace(static_cast<unsigned char>(s.back()))) return std::nullopt;
    if (s.front() == '-') return std::nullopt;
    try {
        size_t pos = 0;
        uint64_t val = std::stoull(std::string(s), &pos);
        if (pos != s.size()) return std::nullopt;
        return val;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace
} // namespace Audio
} // namespace Aestra

#define TEST(name) bool name()
#define PASS() return true
#define FAIL(msg) do { std::cout << "FAIL: " << msg << std::endl; return false; } while(0)

using namespace Aestra::Audio;

TEST(safeStoi_valid_integers) {
    if (safeStoi("42") != 42) FAIL("basic");
    if (safeStoi("-7") != -7) FAIL("negative");
    if (safeStoi("0") != 0) FAIL("zero");
    if (safeStoi("2147483647") != 2147483647) FAIL("INT_MAX");
    if (safeStoi("-2147483648") != std::numeric_limits<int>::min()) FAIL("INT_MIN");
    PASS();
}

TEST(safeStoi_rejects_malformed) {
    if (safeStoi("").has_value()) FAIL("empty");
    if (safeStoi("abc").has_value()) FAIL("letters");
    if (safeStoi("12.34").has_value()) FAIL("float");
    if (safeStoi("42abc").has_value()) FAIL("trailing");
    if (safeStoi(" 42").has_value()) FAIL("leading space");
    if (safeStoi("42 ").has_value()) FAIL("trailing space");
    PASS();
}

TEST(safeStof_valid_floats) {
    auto v1 = safeStof("3.14");
    if (!v1 || std::abs(*v1 - 3.14f) > 0.001f) FAIL("basic");
    auto v2 = safeStof("-2.5");
    if (!v2 || std::abs(*v2 - (-2.5f)) > 0.001f) FAIL("negative");
    auto v3 = safeStof("0.0");
    if (!v3 || *v3 != 0.0f) FAIL("zero");
    auto v4 = safeStof("120");
    if (!v4 || *v4 != 120.0f) FAIL("integer string");
    PASS();
}

TEST(safeStof_rejects_malformed) {
    if (safeStof("").has_value()) FAIL("empty");
    if (safeStof("abc").has_value()) FAIL("letters");
    if (safeStof("3.14abc").has_value()) FAIL("trailing");
    if (safeStof(" 3.14").has_value()) FAIL("leading space");
    if (safeStof("3.14 ").has_value()) FAIL("trailing space");
    PASS();
}

TEST(safeStof_rejects_nan_inf) {
    if (safeStof("nan").has_value()) FAIL("nan");
    if (safeStof("NaN").has_value()) FAIL("NaN mixed case");
    if (safeStof("inf").has_value()) FAIL("inf");
    if (safeStof("Inf").has_value()) FAIL("Inf mixed case");
    if (safeStof("-inf").has_value()) FAIL("-inf");
    if (safeStof("infinity").has_value()) FAIL("infinity");
    PASS();
}

TEST(safeStoull_valid_unsigned) {
    if (safeStoull("0") != 0ull) FAIL("zero");
    if (safeStoull("42") != 42ull) FAIL("basic");
    if (safeStoull("18446744073709551615") != 18446744073709551615ull) FAIL("ULLONG_MAX");
    PASS();
}

TEST(safeStoull_rejects_negative) {
    if (safeStoull("-1").has_value()) FAIL("-1");
    if (safeStoull("-42").has_value()) FAIL("-42");
    if (safeStoull(" -1").has_value()) FAIL("leading space then minus");
    PASS();
}

TEST(safeStoull_rejects_malformed) {
    if (safeStoull("").has_value()) FAIL("empty");
    if (safeStoull("abc").has_value()) FAIL("letters");
    if (safeStoull("42abc").has_value()) FAIL("trailing");
    if (safeStoull(" 42").has_value()) FAIL("leading space");
    if (safeStoull("42 ").has_value()) FAIL("trailing space");
    PASS();
}

TEST(requireFlag_finds_present) {
    std::unordered_map<std::string, std::string> flags = {{"key", "value"}};
    auto result = requireFlag(flags, "key");
    if (!result) FAIL("missing result");
    if (*result != "value") FAIL("wrong value");
    PASS();
}

TEST(requireFlag_returns_nullopt_for_missing) {
    std::unordered_map<std::string, std::string> flags = {{"key", "value"}};
    if (requireFlag(flags, "missing").has_value()) FAIL("should be nullopt");
    PASS();
}

int main() {
    std::cout << "CommandRegistry Parse Safety Tests" << std::endl;
    std::cout << "===================================" << std::endl;

    int passed = 0, failed = 0;

    auto run = [&](const char* name, bool (*fn)()) {
        std::cout << "  " << name << "... ";
        if (fn()) {
            std::cout << "PASS" << std::endl;
            passed++;
        } else {
            failed++;
        }
    };

    run("safeStoi_valid_integers", safeStoi_valid_integers);
    run("safeStoi_rejects_malformed", safeStoi_rejects_malformed);
    run("safeStof_valid_floats", safeStof_valid_floats);
    run("safeStof_rejects_malformed", safeStof_rejects_malformed);
    run("safeStof_rejects_nan_inf", safeStof_rejects_nan_inf);
    run("safeStoull_valid_unsigned", safeStoull_valid_unsigned);
    run("safeStoull_rejects_negative", safeStoull_rejects_negative);
    run("safeStoull_rejects_malformed", safeStoull_rejects_malformed);
    run("requireFlag_finds_present", requireFlag_finds_present);
    run("requireFlag_returns_nullopt_for_missing", requireFlag_returns_nullopt_for_missing);

    std::cout << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;

    return failed == 0 ? 0 : 1;
}
