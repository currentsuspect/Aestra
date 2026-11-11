// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// A malformed array must end the parse, not grow without bound.
//
// JSON::parseArray kept looping when the character after a value was neither ',' nor ']' and parseValue could not
// consume it: every turn appended an empty value at the same position until the process ran out of memory. Found
// 2026-10-30 when a quote inside a Muse verb description broke get_schema's text and MuseVerifyRenderTest grew to 12 GB.
// The same parser reads requests on the Muse socket, so one bad request could take the machine down.
//
// The test runs under a ctest TIMEOUT, and checks the arrays it gets back are small, so the old code fails either way.

#include "AestraJSON.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
int g_failures = 0;
void check(bool cond, const std::string& what) {
    std::cout << (cond ? "PASS: " : "FAIL: ") << what << "\n";
    if (!cond)
        ++g_failures;
}
} // namespace

int main() {
    using Aestra::JSON;

    // Each would loop forever on the old parser. Whatever comes back must be small.
    for (const std::string text : {"[x]", "[1 x]", "[1, x, 2]", "{\"a\": [1 }", "[\"a\" \"b\"]", "[[x]]"}) {
        JSON parsed = JSON::parse(text);
        const size_t n = parsed.isArray() ? parsed.size() : 0;
        check(n <= 3, "\"" + text + "\" ends with a bounded result (" + std::to_string(n) + " items)");
    }

    // Well-formed input is unchanged.
    JSON good = JSON::parse("[1, 2, [3, 4], {\"k\": [5]}]");
    check(good.isArray() && good.size() == 4, "a well-formed array still parses to 4 items");
    check(good[2].isArray() && good[2].size() == 2, "and its nested array to 2");

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
