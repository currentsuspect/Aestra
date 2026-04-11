// © 2026 Aestra Studios — All Rights Reserved.
// RTM-016: Headless env var strtod silent failure — proof of fix
//
// Tests the validation pattern: strtod with endptr, rejecting malformed values.

#include <iostream>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>

// Reproduce the fixed parsing logic from HeadlessMain.cpp
bool parseMinPeakEnv(const char* env, float& out) {
    // [SEC-RTM-016] Use endptr + isfinite to detect malformed values
    char* end = nullptr;
    double val = std::strtod(env, &end);
    if (end == env || *end != '\0' || !std::isfinite(val)) {
        return false;  // Malformed or non-finite — reject
    }
    out = static_cast<float>(val);
    return true;
}

int main() {
    std::cout << "=== RTM-016: Headless env var strtod validation — proof of fix ===" << std::endl;

    struct Test { const char* input; bool expect; double expectedVal; const char* desc; };
    Test tests[] = {
        {"0.5", true, 0.5, "valid float"},
        {"-0.25", true, -0.25, "negative float"},
        {"1.0", true, 1.0, "unity"},
        {"0", true, 0.0, "zero"},
        {"1e-3", true, 0.001, "scientific notation"},
        {"abc", false, 0.0, "completely invalid"},
        {"", false, 0.0, "empty string"},
        {"123abc", false, 0.0, "trailing garbage"},
        {"0.5  ", false, 0.0, "trailing whitespace"},
        {"nan", false, 0.0, "NaN string (rejected by isfinite)"},
        {"inf", false, 0.0, "Inf string (rejected by isfinite)"},
        {"-inf", false, 0.0, "negative Inf (rejected by isfinite)"},
    };

    bool allPass = true;
    for (const auto& t : tests) {
        float out = 0.0f;
        bool result = parseMinPeakEnv(t.input, out);

        bool ok = (result == t.expect);
        if (result && t.expect) {
            ok = ok && (std::abs(out - t.expectedVal) < 0.0001f);
        }

        std::cout << "  [" << (ok ? "PASS" : "FAIL") << "] \"" << t.input << "\" → "
                  << (result ? "accepted (" + std::to_string(out) + ")" : "rejected")
                  << " (" << t.desc << ")" << std::endl;
        if (!ok) allPass = false;
    }

    // Verify that the old vulnerable pattern (nullptr endptr) would have accepted bad values
    std::cout << "\n[Test] Demonstrate old vulnerability (nullptr endptr)" << std::endl;
    {
        // Old code: std::strtod("abc", nullptr) → returns 0.0, no error
        char* dummy = nullptr;
        double oldWay = std::strtod("abc", &dummy);
        std::cout << "  Old way: strtod(\"abc\") = " << oldWay << " (silently 0.0 — CI criteria changed!)" << std::endl;
        std::cout << "  New way: rejected by endptr check — criteria unchanged" << std::endl;
    }

    std::cout << "\n[" << (allPass ? "PASS" : "FAIL") << "] RTM-016 env var validation verified." << std::endl;
    return allPass ? 0 : 1;
}
