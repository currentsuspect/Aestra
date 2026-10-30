// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// CliArgs Tests — strict numeric flag parsing for muse-agent. Pure, no sockets.

#include "CliArgs.h"

#include <iostream>
#include <string>

using Aestra::MuseAgent::parseBoundedInt;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& label) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << label << "\n";
    if (!condition) ++g_failures;
}

} // namespace

int main() {
    check(parseBoundedInt("900", 1, 604800) == 900, "plain number parses");
    check(parseBoundedInt("1", 1, 10) == 1, "lower bound accepted");
    check(parseBoundedInt("10", 1, 10) == 10, "upper bound accepted");
    check(!parseBoundedInt("900s", 1, 604800), "trailing text rejected");
    check(!parseBoundedInt("5x", 1, 100), "trailing letter rejected");
    check(!parseBoundedInt("41952.5", 1, 65535), "fraction rejected");
    check(!parseBoundedInt("abc", 1, 100), "garbage rejected without throwing");
    check(!parseBoundedInt("", 1, 100), "empty rejected");
    check(!parseBoundedInt(" 5", 1, 100), "leading space rejected");
    check(!parseBoundedInt("5 ", 1, 100), "trailing space rejected");
    check(!parseBoundedInt("+5", 1, 100), "leading plus rejected");
    check(!parseBoundedInt("-1", 1, 100), "negative rejected");
    check(!parseBoundedInt("0", 1, 100), "zero rejected when min is 1");
    check(!parseBoundedInt("101", 1, 100), "above max rejected");
    check(!parseBoundedInt("99999999999999999999", 1, 100), "overflow rejected");
    check(!parseBoundedInt("0x10", 1, 100), "hex rejected");

    std::cout << (g_failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string(g_failures))
              << std::endl;
    return g_failures == 0 ? 0 : 1;
}
