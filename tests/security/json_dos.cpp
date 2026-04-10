// © 2025 Aestra Studios — All Rights Reserved.
// SEC-002: JSON parser stack exhaustion via deeply nested structures
//
// Proof: A crafted JSON file with ~50,000 levels of nesting causes stack
// overflow in the recursive descent parser, crashing the process.
//
// After fix: The parser enforces a maximum nesting depth (e.g., 1024 levels)
// and returns a parse error instead of recursing indefinitely.

#include <iostream>
#include <string>
#include <cstdint>
#include <cstdlib>

// Reproduce the vulnerable parsing logic from AestraJSON.h
// This is a simplified standalone version that mirrors the exact recursion pattern.
struct JSONNode {
    bool isObject;
    bool isArray;
};

static int g_maxDepth = 0;

static bool parseValue(const std::string& str, size_t& pos, int depth) {
    if (depth > g_maxDepth) g_maxDepth = depth;
    if (pos >= str.size()) return true;
    char c = str[pos];
    if (c == '[') {
        pos++;
        return parseValue(str, pos, depth + 1);  // recursive, no depth limit
    }
    if (c == ']') {
        pos++;
        return true;
    }
    // skip non-bracket chars
    pos++;
    return parseValue(str, pos, depth + 1);
}

// Simulate the vulnerable parser
bool vulnerableJsonParse(const std::string& json) {
    g_maxDepth = 0;
    size_t pos = 0;
    return parseValue(json, pos, 0);
}

int main() {
    std::cout << "=== SEC-002: JSON parser stack exhaustion ===" << std::endl;

    // Build a deeply nested JSON array: [[[[[...]]]]]
    // With ~50000 levels, this will overflow a typical 8MB stack.
    const int depth = 50000;
    std::string json;
    json.reserve(depth * 2 + 10);
    for (int i = 0; i < depth; i++) json += '[';
    for (int i = 0; i < depth; i++) json += ']';

    std::cout << "  Generated JSON with " << depth << " nesting levels (" << json.size() << " bytes)" << std::endl;
    std::cout << "  Attempting to parse (this may crash if vulnerable)..." << std::endl;

    // On a vulnerable parser, this would crash with stack overflow.
    // Our test simulates the recursion and checks if depth exceeds safe limits.
    bool result = vulnerableJsonParse(json);
    (void)result;

    std::cout << "  Max recursion depth reached: " << g_maxDepth << std::endl;

    // A safe parser would enforce a max depth of ~1024.
    // If g_maxDepth == depth (50000), the parser has no depth limit.
    if (g_maxDepth >= 10000) {
        std::cout << "\n[FAIL] Vulnerability confirmed: no recursion depth limit." << std::endl;
        std::cout << "  Max depth reached: " << g_maxDepth << " (limit should be ~1024)" << std::endl;
        std::cout << "Fix: add depth counter in AestraJSON.h parseValue/parseObject/parseArray" << std::endl;
        return 1;
    }

    std::cout << "\n[PASS] Recursion depth limited to " << g_maxDepth << "." << std::endl;
    return 0;
}
