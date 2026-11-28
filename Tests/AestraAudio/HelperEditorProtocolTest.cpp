// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// `EDITOR OPEN <titleHex> [parentXid]`: the app may name its own X11 window so the helper can make the plugin's
// top-level window transient for it (WM_TRANSIENT_FOR: stays above Aestra, minimises and closes with it). The
// argument is optional and untrusted: a missing or malformed one means the old behaviour, never an error.

#include "../../AestraAudio/src/Plugin/HelperEditorProtocol.h"

#include <iostream>
#include <sstream>
#include <string>

using namespace AestraHelper;

namespace {
int g_failures = 0;
void expect(bool ok, const std::string& msg) {
    if (!ok) { std::cerr << "[FAIL] " << msg << "\n"; ++g_failures; }
}
} // namespace

int main() {
    struct Parse { const char* tok; std::uintptr_t want; };
    const Parse parses[] = {
        {"", 0},                  // missing: old behaviour
        {"0", 0},                 // none
        {"12345", 12345},
        {"4194305", 4194305},     // a typical X window id
        {"4294967295", 4294967295u}, // largest 32-bit id
        {"4294967296", 0},        // wider than an X11 Window id: ignored
        {"99999999999999999999999", 0}, // overflow
        {"-5", 0},
        {"abc", 0},
        {"12x", 0},               // trailing garbage
        {"0x10", 0},              // decimal only
        {" 7", 0},
    };
    for (const auto& p : parses)
        expect(parseTransientParent(p.tok) == p.want, std::string("parse '") + p.tok + "'");

    struct Fmt { const char* title; std::uintptr_t parent; const char* want; };
    const Fmt fmts[] = {
        {"41424344", 0, "EDITOR OPEN 41424344"},          // no parent: exactly the old command
        {"41424344", 4194305, "EDITOR OPEN 41424344 4194305"},
    };
    for (const auto& f : fmts)
        expect(formatEditorOpenCommand(f.title, f.parent) == f.want, std::string("format ") + f.want);

    // Round trip through the helper's tokenising: command, sub, titleHex, optional parent.
    for (const std::uintptr_t parent : {std::uintptr_t{0}, std::uintptr_t{4194305}}) {
        std::istringstream in(formatEditorOpenCommand("4142", parent));
        std::string cmd, sub, title, tok;
        in >> cmd >> sub >> title >> tok;
        expect(cmd == "EDITOR" && sub == "OPEN" && title == "4142", "round trip header");
        expect(parseTransientParent(tok) == parent, "round trip parent");
    }

    if (g_failures) { std::cerr << g_failures << " failure(s)\n"; return 1; }
    std::cout << "HelperEditorProtocolTest: all passed\n";
    return 0;
}
