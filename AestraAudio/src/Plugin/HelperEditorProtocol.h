// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The sandbox helper's `EDITOR OPEN <titleHex> [parentXid]` line, kept pure so the app and the helper agree and a
// table test pins it. parentXid is the app's own X11 window: the helper makes the plugin's top-level window
// transient for it (WM_TRANSIENT_FOR) so the window manager keeps it above Aestra and minimises/closes it with
// Aestra. The argument is optional and untrusted; absent or malformed means the old behaviour.

#include <cstdint>
#include <string>

namespace AestraHelper {

/// A decimal X11 window id, or 0 for anything else (empty, "0", sign, hex, trailing text, wider than 32 bits).
inline std::uintptr_t parseTransientParent(const std::string& token) {
    if (token.empty() || token.size() > 10) return 0;
    std::uint64_t value = 0;
    for (const char c : token) {
        if (c < '0' || c > '9') return 0;
        value = value * 10 + static_cast<std::uint64_t>(c - '0');
    }
    return value <= 0xFFFFFFFFull ? static_cast<std::uintptr_t>(value) : 0;
}

/// The app's command line; with no parent it is exactly the pre-existing `EDITOR OPEN <titleHex>`.
inline std::string formatEditorOpenCommand(const std::string& titleHex, std::uintptr_t parentXid) {
    std::string line = "EDITOR OPEN " + titleHex;
    if (parentXid != 0) line += " " + std::to_string(static_cast<unsigned long long>(parentXid));
    return line;
}

} // namespace AestraHelper
