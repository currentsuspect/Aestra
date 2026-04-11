// © 2025 Aestra Studios — All Rights Reserved.
// SEC-002: JSON parser stack exhaustion via deeply nested structures
//
// This test now exercises the real Aestra::JSON parser rather than a local
// recursive clone. It passes when excessive nesting is rejected gracefully.

#include "AestraJSON.h"

#include <iostream>
#include <string>

/**
 * @brief Executes the SEC-002 regression test for JSON parser stack exhaustion.
 *
 * Generates a JSON string with 50,000 nested array brackets, attempts to parse it
 * with Aestra::JSON::parse, reports whether the parser rejected the excessive
 * nesting without crashing, and prints a PASS/FAIL message to stdout.
 *
 * @return int Exit status: 0 if the parser rejected the excessively nested input (test pass),
 * 1 if the parser accepted the excessively nested input (test fail).
 */
int main() {
    std::cout << "=== SEC-002: JSON parser stack exhaustion ===" << std::endl;

    constexpr int depth = 50000;
    std::string json;
    json.reserve(static_cast<size_t>(depth) * 2u);
    for (int i = 0; i < depth; ++i) {
        json += '[';
    }
    for (int i = 0; i < depth; ++i) {
        json += ']';
    }

    std::cout << "  Generated JSON with " << depth << " nesting levels (" << json.size() << " bytes)" << std::endl;

    const Aestra::JSON parsed = Aestra::JSON::parse(json);
    const bool rejected = !parsed.isArray();
    if (rejected) {
        std::cout << "  [PASS] Parser rejected excessive nesting without crashing." << std::endl;
        return 0;
    }

    std::cout << "  [FAIL] Parser accepted excessively nested JSON." << std::endl;
    return 1;
}
