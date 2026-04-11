// © 2026 Aestra Studios — All Rights Reserved.
// RTM-013 + RTM-014: JSON parseNumber stod + asArray/asObject mutable static
// Proof-of-fix tests that exercise the actual AestraJSON.h parser.

#include <iostream>
#include <cstdint>
#include <string>
#include <vector>
#include <map>

// Include the actual JSON parser header
#include "AestraJSON.h"

int main() {
    std::cout << "=== RTM-013 + RTM-014: JSON parser hardening — proof of fix ===" << std::endl;

    // ── RTM-013: Malformed number strings ──
    std::cout << "\n[Test 1] RTM-013: Malformed number strings" << std::endl;

    struct NumTest { const char* input; double expected; bool expectGuard; const char* desc; };
    NumTest numTests[] = {
        // Valid numbers — should parse correctly
        {"42", 42.0, false, "integer"},
        {"-3.14", -3.14, false, "negative float"},
        {"1e5", 100000.0, false, "exponent"},
        {"1.5E-2", 0.015, false, "scientific notation"},
        {"0", 0.0, false, "zero"},
        {"-0.0", 0.0, false, "negative zero"},
        {"100", 100.0, false, "three digit integer"},
        // Malformed — should return 0.0 (guard), not crash
        {"1e", 0.0, true, "trailing e"},
        {"1E", 0.0, true, "trailing E"},
        {"1e+", 0.0, true, "incomplete exponent sign"},
        {"1E+", 0.0, true, "incomplete exponent sign (uppercase)"},
        {"-1e", 0.0, true, "negative trailing e"},
    };

    bool numPass = true;
    for (const auto& t : numTests) {
        // Parse the number by embedding in a JSON array
        std::string fullStr = "[" + std::string(t.input) + "]";
        Aestra::JSON j = Aestra::JSON::parse(fullStr);
        double actual = j.asArray()[0].asNumber();
        bool ok;
        if (t.expectGuard) {
            // Malformed inputs should be caught by guard → 0.0
            ok = (actual == 0.0);
        } else {
            ok = (std::abs(actual - t.expected) < 0.001);
        }
        std::cout << "  [" << (ok ? "PASS" : "FAIL") << "] \"" << t.input << "\" → " << actual
                  << " (expected ~" << t.expected << ") " << t.desc << std::endl;
        if (!ok) numPass = false;
    }

    // ── RTM-014: asArray() / asObject() mutable static ──
    std::cout << "\n[Test 2] RTM-014: asArray/asObject no mutable global static" << std::endl;

    // On a non-array JSON value, asArray() should return an empty vector by value.
    // Modifying it should not affect subsequent calls.
    Aestra::JSON notAnArray = Aestra::JSON::parse("\"hello\"");
    auto arr1 = notAnArray.asArray();
    arr1.push_back(Aestra::JSON(999.0));  // Modify our copy
    auto arr2 = notAnArray.asArray();
    bool arrIsolated = arr2.empty();  // Should be empty — our modification didn't leak
    std::cout << "  [" << (arrIsolated ? "PASS" : "FAIL") << "] asArray() on string returns independent empty copy" << std::endl;

    Aestra::JSON notAnObject = Aestra::JSON::parse("42");
    auto obj1 = notAnObject.asObject();
    obj1["injected"] = Aestra::JSON("pwned");
    auto obj2 = notAnObject.asObject();
    bool objIsolated = obj2.empty();
    std::cout << "  [" << (objIsolated ? "PASS" : "FAIL") << "] asObject() on number returns independent empty copy" << std::endl;

    // Verify that actual arrays/objects still work correctly
    Aestra::JSON realArray = Aestra::JSON::parse("[1,2,3]");
    bool realArrayOk = realArray.asArray().size() == 3;
    std::cout << "  [" << (realArrayOk ? "PASS" : "FAIL") << "] Real array still works (size=3)" << std::endl;

    Aestra::JSON realObject = Aestra::JSON::parse("{\"a\":1}");
    bool realObjectOk = realObject.asObject().size() == 1;
    std::cout << "  [" << (realObjectOk ? "PASS" : "FAIL") << "] Real object still works (size=1)" << std::endl;

    // ── Verdict ──
    bool allPass = numPass && arrIsolated && objIsolated && realArrayOk && realObjectOk;
    std::cout << "\n[" << (allPass ? "PASS" : "FAIL") << "] RTM-013 + RTM-014 verified." << std::endl;
    return allPass ? 0 : 1;
}
