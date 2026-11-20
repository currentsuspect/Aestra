// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "ArsenalUnitNaming.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        std::exit(1);
    }
}

using Aestra::ArsenalUnitNaming::nextUnitName;

void testFirstUnitIsOne() {
    require(nextUnitName("Sampler ", {}) == "Sampler 1", "the first unit is number 1");
}

void testAppendsAfterAContiguousRun() {
    require(nextUnitName("Sampler ", {"Sampler 1", "Sampler 2", "Sampler 3"}) == "Sampler 4",
            "no gaps: the next number follows the run");
}

void testFillsTheLowestGap() {
    // The reported bug: 4 was deleted, leaving 1,2,3,5,6. Count + 1 made a second "Sampler 6".
    require(nextUnitName("Sampler ", {"Sampler 1", "Sampler 2", "Sampler 3", "Sampler 5", "Sampler 6"}) == "Sampler 4",
            "a deleted number is reused and a name never repeats");
    require(nextUnitName("Sampler ", {"Sampler 2", "Sampler 3"}) == "Sampler 1",
            "a deleted first unit leaves 1 free");
}

void testOrderDoesNotMatter() {
    require(nextUnitName("Sampler ", {"Sampler 3", "Sampler 1"}) == "Sampler 2", "list order is irrelevant");
}

void testRenamedAndOtherKindsDoNotTakeNumbers() {
    require(nextUnitName("Sampler ", {"Kick", "Sampler 1x", "Sampler", "808 1"}) == "Sampler 1",
            "renamed units and other prefixes do not use a Sampler number");
    require(nextUnitName("808 ", {"808 1", "Sampler 2"}) == "808 2", "each kind counts its own numbers");
}

void testNeverZeroOrHugeNumbers() {
    require(nextUnitName("Sampler ", {"Sampler 0"}) == "Sampler 1", "Sampler 0 is not a taken number");
    require(nextUnitName("Sampler ", {"Sampler 99999999999"}) == "Sampler 1", "an absurd number is ignored");
}

} // namespace

int main() {
    testFirstUnitIsOne();
    testAppendsAfterAContiguousRun();
    testFillsTheLowestGap();
    testOrderDoesNotMatter();
    testRenamedAndOtherKindsDoNotTakeNumbers();
    testNeverZeroOrHugeNumbers();
    std::cout << "ArsenalUnitNamingTest: all checks passed\n";
    return 0;
}
