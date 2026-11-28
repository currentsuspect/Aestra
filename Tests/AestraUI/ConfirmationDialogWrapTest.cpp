// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// ConfirmationDialogWrapTest — the dialog's message wrapping never lets a line run past the message area, and never
// loses text (CodeRabbit on #1493).
//
// The first-load prompt shows the plugin's path to someone deciding whether to trust it. A path has no spaces, so it is
// one token; when that token was longer than a line it was kept whole on its own line, drawn past the message area,
// and the end of the path (the file name, the part that says which plugin it is) was cut off.

#include "ConfirmationDialogWrap.h"

#include <iostream>
#include <string>
#include <vector>

using Aestra::wrapMessageLines;

namespace {
int g_failures = 0;

void check(bool condition, const std::string& label) {
    if (!condition) {
        std::cerr << "FAIL: " << label << "\n";
        ++g_failures;
    }
}

// A width and font size that give exactly 20 characters per line (charWidth = fontSize * 0.5).
constexpr float kFont = 10.0f;
constexpr float kWidth = 100.0f;
constexpr size_t kMaxChars = 20;

std::string joinedWithoutBreaks(const std::vector<std::string>& lines) {
    std::string out;
    for (const auto& l : lines) out += l;
    return out;
}

bool allWithin(const std::vector<std::string>& lines, size_t maxChars) {
    for (const auto& l : lines) {
        if (l.size() > maxChars) return false;
    }
    return true;
}

// A line must never begin in the middle of a UTF-8 character (a continuation byte is 10xxxxxx).
bool noLineStartsMidCharacter(const std::vector<std::string>& lines) {
    for (const auto& l : lines) {
        if (!l.empty() && (static_cast<unsigned char>(l[0]) & 0xC0) == 0x80) return false;
    }
    return true;
}
} // namespace

int main() {
    // --- what already worked must keep working -------------------------------------------------------------------
    {
        const auto lines = wrapMessageLines("one two three four five six", kWidth, kFont);
        check(allWithin(lines, kMaxChars), "spaced words wrap within the width");
        check(lines.size() > 1, "a sentence longer than a line wraps onto more lines");
        std::string rejoined;
        for (size_t i = 0; i < lines.size(); ++i) rejoined += (i ? " " : "") + lines[i];
        check(rejoined == "one two three four five six", "wrapping spaced words loses no text");
    }
    {
        const auto lines = wrapMessageLines("first\nsecond", kWidth, kFont);
        check(lines.size() == 2 && lines[0] == "first" && lines[1] == "second", "an explicit newline starts a new line");
    }
    {
        const auto lines = wrapMessageLines("short", kWidth, kFont);
        check(lines.size() == 1 && lines[0] == "short", "a short message is one untouched line");
    }

    // --- the finding: one token longer than a line --------------------------------------------------------------
    {
        const std::string path = "/home/producer/.clap/Some Vendor Pack/VeryLongPluginName-1.2.3-linux.clap";
        const std::string token = "/home/producer/.clap/Some_Vendor_Pack/VeryLongPluginName-1.2.3-linux.clap";
        const auto lines = wrapMessageLines(token, kWidth, kFont);
        check(allWithin(lines, kMaxChars), "a path longer than a line is split to fit, not drawn past the edge");
        check(joinedWithoutBreaks(lines) == token, "splitting a long path loses no characters (the file name survives)");
        (void)path;
    }
    {
        const std::string msg = "Allow /a/very/long/path/with/no/spaces/at/all/plugin-name.clap to load?";
        const auto lines = wrapMessageLines(msg, kWidth, kFont);
        check(allWithin(lines, kMaxChars), "a long token inside a sentence is split, the rest still wraps");
        std::string rejoined;
        for (size_t i = 0; i < lines.size(); ++i) rejoined += lines[i];
        std::string expectedNoSpaces;
        for (char c : msg) if (c != ' ') expectedNoSpaces += c;
        std::string gotNoSpaces;
        for (char c : rejoined) if (c != ' ') gotNoSpaces += c;
        check(gotNoSpaces == expectedNoSpaces, "no text is lost around a split token");
    }

    // --- UTF-8: never cut a character in half -------------------------------------------------------------------
    {
        std::string token;
        for (int i = 0; i < 30; ++i) token += "\xC3\xA9"; // 30 x 'é' (2 bytes each): 60 bytes, no spaces
        const auto lines = wrapMessageLines(token, kWidth, kFont);
        check(allWithin(lines, kMaxChars), "a multibyte token is split to fit");
        check(noLineStartsMidCharacter(lines), "no line starts in the middle of a UTF-8 character");
        check(joinedWithoutBreaks(lines) == token, "a split multibyte token loses no bytes");
    }
    {
        // A width of 1 char with 2-byte characters must still make progress (no empty fragments, no endless loop).
        std::string token;
        for (int i = 0; i < 5; ++i) token += "\xC3\xA9";
        const auto lines = wrapMessageLines(token, /*maxWidth=*/5.0f, kFont);
        check(noLineStartsMidCharacter(lines), "a one-character width still splits on character boundaries");
        check(joinedWithoutBreaks(lines) == token, "a one-character width still loses no bytes");
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "ConfirmationDialogWrapTest: all checks passed\n";
    return 0;
}
