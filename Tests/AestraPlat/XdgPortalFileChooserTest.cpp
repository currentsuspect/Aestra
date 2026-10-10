// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The pure halves of the XDG portal file chooser: translating Aestra's
// Win32-style filter strings into portal filters, and decoding the file://
// URIs the portal answers with. The D-Bus round trip itself needs a desktop
// session and is exercised by hand, not here.

#include "../../AestraPlat/src/Linux/XdgPortalFileChooser.h"

#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                                                                                              \
    do {                                                                                                              \
        if (!(cond)) {                                                                                                \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                        \
            ++g_failures;                                                                                             \
        }                                                                                                             \
    } while (0)

using namespace Aestra::XdgPortal;

std::string filterString(const char* literal, size_t sizeWithTerminator) {
    return std::string(literal, sizeWithTerminator - 1);
}

void testParsesMultiPatternFilters() {
    const char raw[] = "Audio Files\0*.wav;*.flac;*.mp3\0All Files\0*.*\0";
    const auto filters = parseWin32Filter(filterString(raw, sizeof(raw)));
    CHECK(filters.size() == 2, "two filters");
    CHECK(filters.size() == 2 && filters[0].name == "Audio Files", "first filter name");
    CHECK(filters.size() == 2 && filters[0].patterns == (std::vector<std::string>{"*.wav", "*.flac", "*.mp3"}),
          "semicolon-separated patterns split");
    CHECK(filters.size() == 2 && filters[1].patterns == std::vector<std::string>{"*"},
          "*.* becomes * (a glob that also matches extensionless files)");
}

void testEmptyFilterIsNoFilter() {
    CHECK(parseWin32Filter("").empty(), "empty filter string yields no filters");
}

void testDecodesFileUris() {
    CHECK(pathFromFileUri("file:///home/me/Downloads") == "/home/me/Downloads", "plain file URI");
    CHECK(pathFromFileUri("file:///home/me/My%20Samples/K%C3%BCche") == "/home/me/My Samples/K\xC3\xBC" "che",
          "percent-escapes decode, UTF-8 bytes included");
    CHECK(pathFromFileUri("file://localhost/srv/music") == "/srv/music", "localhost authority");
    CHECK(pathFromFileUri("file://otherhost/srv/music").empty(), "remote host is not a local path");
    CHECK(pathFromFileUri("https://example.com/x").empty(), "non-file scheme rejected");
    CHECK(pathFromFileUri("file:///tmp/100%").substr(0, 9) == "/tmp/100%", "stray % kept literally");
}

} // namespace

int main() {
    testParsesMultiPatternFilters();
    testEmptyFilterIsNoFilter();
    testDecodesFileUris();
    if (g_failures == 0) {
        std::printf("XdgPortalFileChooserTest: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "XdgPortalFileChooserTest: %d check(s) failed\n", g_failures);
    return 1;
}
