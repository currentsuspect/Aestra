// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// JSON::asArray() const and asObject() const return the real contents. From
// #179 until this test, both returned an always-empty static, so any read
// through a const JSON silently saw nothing and callers had to bind non-const
// to get their data. A non-array / non-object still reads as empty.

#include "AestraJSON.h"

#include <cstdio>

using Aestra::JSON;

namespace {

int g_failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::printf("[FAIL] %s\n", message);
        ++g_failures;
    }
}

} // namespace

int main() {
    const JSON doc = JSON::parse(R"({"list": [1, 2, 3], "map": {"a": 1, "b": 2}, "n": 5})");

    check(doc["list"].asArray().size() == 3, "const asArray() returns the array's elements");
    check(doc["list"].asArray()[2].asNumber() == 3.0, "and their values");
    check(doc["map"].asObject().size() == 2, "const asObject() returns the object's members");
    check(doc["map"].asObject().count("b") == 1, "by key");

    check(doc["n"].asArray().empty(), "a number read as an array is empty");
    check(doc["n"].asObject().empty(), "a number read as an object is empty");
    check(doc["missing"].asArray().empty(), "a missing key reads as an empty array");

    JSON mutableDoc = JSON::parse(R"({"list": [1]})");
    const JSON& view = mutableDoc;
    check(view["list"].asArray().size() == mutableDoc["list"].asArray().size(),
          "const and non-const reads agree");

    if (g_failures == 0) {
        std::printf("JSONConstAccessTest: all checks passed\n");
        return 0;
    }
    std::printf("JSONConstAccessTest: %d check(s) failed\n", g_failures);
    return 1;
}
