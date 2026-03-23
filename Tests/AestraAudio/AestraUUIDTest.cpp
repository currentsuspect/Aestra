// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraUUID Unit Tests
// Tests: construction, generation, uniqueness, equality, toString, std::hash

#include "AestraUUID.h"

#include <cassert>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using Aestra::Audio::AestraUUID;

// =============================================================================
// Tests
// =============================================================================

void testDefaultConstruction() {
    std::cout << "TEST: AestraUUID default construction... ";

    AestraUUID id;
    assert(id.low == 0);
    assert(id.high == 0);

    std::cout << "✅ PASS\n";
}

void testValueConstruction() {
    std::cout << "TEST: AestraUUID value construction... ";

    AestraUUID id(42);
    assert(id.low == 42);
    // high is zero-initialized
    assert(id.high == 0);

    std::cout << "✅ PASS\n";
}

void testGenerateReturnsNonZero() {
    std::cout << "TEST: AestraUUID::generate returns non-zero... ";

    AestraUUID id = AestraUUID::generate();
    // The counter starts at 1, so low should be >= 1
    assert(id.low != 0 || id.high != 0);

    std::cout << "✅ PASS\n";
}

void testGenerateIncreasesCounter() {
    std::cout << "TEST: AestraUUID::generate counter increments... ";

    AestraUUID id1 = AestraUUID::generate();
    AestraUUID id2 = AestraUUID::generate();
    AestraUUID id3 = AestraUUID::generate();

    // Sequential generates must have increasing low values
    assert(id2.low == id1.low + 1);
    assert(id3.low == id2.low + 1);

    std::cout << "✅ PASS\n";
}

void testGenerateUniqueness() {
    std::cout << "TEST: AestraUUID::generate uniqueness... ";

    const int count = 1000;
    std::unordered_set<uint64_t> seen;

    for (int i = 0; i < count; ++i) {
        AestraUUID id = AestraUUID::generate();
        // low is the counter - every value must be unique
        assert(seen.find(id.low) == seen.end());
        seen.insert(id.low);
    }

    assert(seen.size() == count);

    std::cout << "✅ PASS\n";
}

void testSameSaltForSameProcess() {
    std::cout << "TEST: AestraUUID same process salt (high) across generates... ";

    AestraUUID id1 = AestraUUID::generate();
    AestraUUID id2 = AestraUUID::generate();

    // All UUIDs generated in the same process share the same high (salt)
    assert(id1.high == id2.high);

    std::cout << "✅ PASS\n";
}

void testEqualityOperator() {
    std::cout << "TEST: AestraUUID equality operator... ";

    AestraUUID a(10);
    AestraUUID b(10);
    AestraUUID c(20);

    assert(a == b); // Same value
    assert(!(a == c)); // Different value

    std::cout << "✅ PASS\n";
}

void testInequalityOperator() {
    std::cout << "TEST: AestraUUID inequality operator... ";

    AestraUUID a(5);
    AestraUUID b(5);
    AestraUUID c(6);

    assert(!(a != b)); // Same value - not unequal
    assert(a != c);   // Different value

    std::cout << "✅ PASS\n";
}

void testDefaultConstructedEquality() {
    std::cout << "TEST: AestraUUID default constructed equality... ";

    AestraUUID a;
    AestraUUID b;
    // Both are zero - they are equal
    assert(a == b);
    assert(!(a != b));

    std::cout << "✅ PASS\n";
}

void testToStringLength() {
    std::cout << "TEST: AestraUUID toString length... ";

    AestraUUID id = AestraUUID::generate();
    std::string s = id.toString();

    // 16 hex chars for high + 16 hex chars for low = 32 chars
    assert(s.length() == 32);

    std::cout << "✅ PASS\n";
}

void testToStringIsHexadecimal() {
    std::cout << "TEST: AestraUUID toString is hexadecimal... ";

    AestraUUID id = AestraUUID::generate();
    std::string s = id.toString();

    for (char c : s) {
        bool isHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        assert(isHex);
    }

    std::cout << "✅ PASS\n";
}

void testToStringDistinctForDifferentIds() {
    std::cout << "TEST: AestraUUID toString distinct for different IDs... ";

    AestraUUID id1 = AestraUUID::generate();
    AestraUUID id2 = AestraUUID::generate();

    assert(id1.toString() != id2.toString());

    std::cout << "✅ PASS\n";
}

void testHashSpecialization() {
    std::cout << "TEST: AestraUUID std::hash specialization... ";

    AestraUUID id1 = AestraUUID::generate();
    AestraUUID id2 = AestraUUID::generate();

    std::hash<AestraUUID> hasher;
    size_t h1 = hasher(id1);
    size_t h2 = hasher(id2);

    // Different IDs should (almost certainly) have different hashes
    assert(h1 != h2);

    // Same ID hashes must match
    assert(hasher(id1) == hasher(id1));

    std::cout << "✅ PASS\n";
}

void testUsableAsUnorderedMapKey() {
    std::cout << "TEST: AestraUUID usable as unordered_map key... ";

    std::unordered_map<AestraUUID, int> map;

    AestraUUID id1 = AestraUUID::generate();
    AestraUUID id2 = AestraUUID::generate();
    AestraUUID id3 = AestraUUID::generate();

    map[id1] = 100;
    map[id2] = 200;
    map[id3] = 300;

    assert(map.size() == 3);
    assert(map[id1] == 100);
    assert(map[id2] == 200);
    assert(map[id3] == 300);

    // Lookup by value
    AestraUUID copyOfId1 = id1;
    assert(map.count(copyOfId1) == 1);
    assert(map[copyOfId1] == 100);

    std::cout << "✅ PASS\n";
}

void testZeroUuidHash() {
    std::cout << "TEST: AestraUUID zero UUID hash does not crash... ";

    AestraUUID zero;
    std::hash<AestraUUID> hasher;
    size_t h = hasher(zero);
    (void)h; // Just verify it doesn't throw or crash

    std::cout << "✅ PASS\n";
}

void testCopyConstruction() {
    std::cout << "TEST: AestraUUID copy construction... ";

    AestraUUID original = AestraUUID::generate();
    AestraUUID copy = original;

    assert(copy == original);
    assert(copy.low == original.low);
    assert(copy.high == original.high);

    std::cout << "✅ PASS\n";
}

void testHighLowFieldsInequality() {
    std::cout << "TEST: AestraUUID high/low field inequality... ";

    // Two UUIDs with same low but different high are not equal
    AestraUUID a;
    a.low = 42;
    a.high = 1;

    AestraUUID b;
    b.low = 42;
    b.high = 2;

    assert(a != b);
    assert(!(a == b));

    std::cout << "✅ PASS\n";
}

// =============================================================================
// MAIN
// =============================================================================

int main() {
    std::cout << "\n=== Aestra UUID Unit Tests ===\n\n";

    testDefaultConstruction();
    testValueConstruction();
    testGenerateReturnsNonZero();
    testGenerateIncreasesCounter();
    testGenerateUniqueness();
    testSameSaltForSameProcess();
    testEqualityOperator();
    testInequalityOperator();
    testDefaultConstructedEquality();
    testToStringLength();
    testToStringIsHexadecimal();
    testToStringDistinctForDifferentIds();
    testHashSpecialization();
    testUsableAsUnorderedMapKey();
    testZeroUuidHash();
    testCopyConstruction();
    testHighLowFieldsInequality();

    std::cout << "\nAll AestraUUID tests passed.\n";
    return 0;
}