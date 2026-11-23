// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Guard: the selection / accent colours come from the theme's named tokens, never from a wash or a literal.
//
// Violet (or whatever a theme's accent is) means "selected", "the playhead" and "accent text". Those are the theme's
// opaque tokens: selected, selectedText, selectionFill, selectionTint, selectionEdge, hoverSelected, accentText,
// playhead, controlGuide. A draw site that tints the primary with an alpha, lerps toward it, lightens it, or spells a
// violet hex has its own idea of the colour: a theme swap does not move it, and a low alpha over content is the
// "washed-out purple" the Ink theme removed. This test reads the source tree and fails on:
//
//   R1  getColor("<accent name>").withAlpha / lightened / darkened
//   R2  <theme>.primary / .accentPrimary / .secondary ... .withAlpha / lightened / darkened
//   R3  lerp(... primary ...)
//   R4  a local or member assigned from an accent colour and then given an alpha / lightened / darkened
//   R5  a violet literal (0xRRGGBB / 0xAARRGGBB / "#rrggbb" / NUIColor(r, g, b) whose hue is 250-300 degrees)
//
// Files that legitimately define colours are the allowlist below, each with its reason. Anything else must use a
// token. The scanner is also run over small fixtures first, so the rules cannot go quiet without this test failing.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// CMake passes the repository root (target_compile_definitions in Tests/CMakeLists.txt). Without it (an IDE that does not
// read the compile commands) the scan would find no files, and the file-count check below fails, so a build that lost
// the definition cannot pass by scanning nothing.
#ifndef AESTRA_SOURCE_DIR
#define AESTRA_SOURCE_DIR "."
#endif

namespace {
int g_failures = 0;
int g_checks = 0;
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        std::cout << "  FAIL " << what << "\n";
        ++g_failures;
    }
}

struct Finding {
    int line;
    std::string rule;
    std::string text;
};

bool violet(float r, float g, float b) {
    const float mx = std::max({r, g, b}), mn = std::min({r, g, b});
    if (mx < 0.15f || (mx - mn) / mx < 0.25f) return false;
    float h;
    const float d = mx - mn;
    if (mx == r) h = std::fmod((g - b) / d, 6.0f);
    else if (mx == g) h = (b - r) / d + 2.0f;
    else h = (r - g) / d + 4.0f;
    h *= 60.0f;
    if (h < 0) h += 360.0f;
    return h >= 250.0f && h <= 300.0f;
}

std::vector<Finding> scan(const std::string& text) {
    static const std::string accent = "(accentPrimary|primary|secondary|accentSecondary|focusRing|selected|primaryHover|primaryPressed)";
    static const std::regex r1("getColor\\(\"" + accent + "\"\\)\\s*\\.\\s*(withAlpha|lightened|darkened)\\s*\\(");
    static const std::regex r2("\\b(primary|accentPrimary|secondary|accentSecondary|primaryHover|primaryPressed)\\s*\\.\\s*(withAlpha|lightened|darkened)\\s*\\(");
    static const std::regex r3("lerp\\([^;]*\\b(primary|accentPrimary|secondary|accentSecondary)\\b");
    static const std::regex assign("(?:auto|NUIColor)\\s*&?\\s+(\\w+)\\s*=\\s*[^;]*(?:getColor\\(\"" + accent + "\"\\)|\\b(?:primary|accentPrimary|secondary|accentSecondary)\\b\\s*;)");
    static const std::regex memberAssign("\\b(m_\\w+|\\w+_)\\s*=\\s*[^;]*getColor\\(\"" + accent + "\"\\)\\s*;");
    static const std::regex hex("0x([0-9a-fA-F]{6,8})\\b");
    static const std::regex str("\"#([0-9a-fA-F]{6})\"");
    static const std::regex flt("NUIColor\\(\\s*([0-9.]+)f?\\s*,\\s*([0-9.]+)f?\\s*,\\s*([0-9.]+)f?");

    std::vector<Finding> out;
    std::vector<std::string> lines;
    {
        std::stringstream ss(text);
        for (std::string l; std::getline(ss, l);) lines.push_back(l);
    }
    std::set<std::string> names;
    for (const std::string& l : lines) {
        std::smatch m;
        if (std::regex_search(l, m, assign)) names.insert(m[1]);
        if (std::regex_search(l, m, memberAssign)) names.insert(m[1]);
    }
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& l = lines[i];
        const int n = static_cast<int>(i) + 1;
        const size_t cut = l.find("//");
        const std::string code = cut == std::string::npos ? l : l.substr(0, cut);
        if (std::regex_search(code, r1)) out.push_back({n, "R1", l});
        if (std::regex_search(code, r2)) out.push_back({n, "R2", l});
        if (std::regex_search(code, r3)) out.push_back({n, "R3", l});
        for (const std::string& v : names) {
            const std::regex use("\\b" + v + "\\s*(\\.|->)\\s*(withAlpha|lightened|darkened)\\s*\\(|lerp\\([^;]*\\b" + v + "\\b");
            if (std::regex_search(code, use)) {
                out.push_back({n, "R4", l});
                break;
            }
        }
        for (auto it = std::sregex_iterator(code.begin(), code.end(), hex); it != std::sregex_iterator(); ++it) {
            const std::string h = (*it)[1];
            const auto byte = [&](size_t at) { return std::stoi(h.substr(at, 2), nullptr, 16) / 255.0f; };
            bool v = violet(byte(0), byte(2), byte(4));
            if (h.size() == 8) v = v || violet(byte(2), byte(4), byte(6));
            if (v) out.push_back({n, "R5", l});
        }
        for (auto it = std::sregex_iterator(code.begin(), code.end(), str); it != std::sregex_iterator(); ++it) {
            const std::string h = (*it)[1];
            const auto byte = [&](size_t at) { return std::stoi(h.substr(at, 2), nullptr, 16) / 255.0f; };
            if (violet(byte(0), byte(2), byte(4))) out.push_back({n, "R5", l});
        }
        for (auto it = std::sregex_iterator(code.begin(), code.end(), flt); it != std::sregex_iterator(); ++it) {
            const float r = std::stof((*it)[1]), g = std::stof((*it)[2]), b = std::stof((*it)[3]);
            if (r <= 1.0f && g <= 1.0f && b <= 1.0f && violet(r, g, b)) out.push_back({n, "R5", l});
        }
    }
    return out;
}

bool hasRule(const std::vector<Finding>& f, const std::string& rule) {
    return std::any_of(f.begin(), f.end(), [&](const Finding& x) { return x.rule == rule; });
}

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Files allowed to name accent colours directly, and why.
const std::map<std::string, std::string>& allowlist() {
    static const std::map<std::string, std::string> list = {
        {"AestraUI/Core/NUITitleBarConfig.h", "config fallback strings for the custom title bar (the bar reads its colours from the theme)"},
        {"AestraUI/Base/NUICheckbox.h", "default member initialiser; the constructor overwrites it from the theme"},
        {"AestraUI/Base/NUIProgressBar.h", "default member initialiser; the constructor overwrites it from the theme"},
        {"AestraUI/Base/NUITextInput.h", "default member initialiser; the constructor overwrites it from the theme"},
        {"AestraUI/Base/NUISlider.h", "default member initialiser; the constructor overwrites it from the theme"},
        {"AestraUI/Core/NUIThemeSystem.cpp", "defines the theme presets and derives the token family from the primary"},
        {"AestraUI/Widgets/UnitRowLook.h", "the Arsenal's headless colour table: derives its row/pad colours from the primary, opaque"},
        {"AestraUI/Widgets/TrackColorPalette.h", "identity palette data (legacy violet entries only decode saved projects)"},
        {"Source/Core/VersionLanes.h", "lane identity palette data"},
        {"Source/Panels/AuditionCover.h", "generated cover art palettes (artwork, not UI chrome)"},
        {"AestraUI/Widgets/NUIStepSequencer.cpp", "step cell intensity is the note velocity as alpha"},
        {"Source/Settings/SettingsDialog.cpp", "the dialog's focus blink animates the focus ring's alpha"},
    };
    return list;
}
} // namespace

int main() {
    // The rules fire on what they exist to catch, and stay quiet on the tokens.
    check(hasRule(scan("r.fillRect(b, theme.getColor(\"accentPrimary\").withAlpha(0.2f));"), "R1"), "R1 catches getColor(accentPrimary).withAlpha");
    check(hasRule(scan("auto c = t.primary.withAlpha(0.3f);"), "R2"), "R2 catches primary.withAlpha");
    check(hasRule(scan("auto c = NUIColor::lerp(bed, t.primary, 0.2f);"), "R3"), "R3 catches lerp toward the primary");
    check(hasRule(scan("const auto accent = theme.getColor(\"accentPrimary\");\nr.fill(b, accent.withAlpha(0.1f));"), "R4"),
          "R4 catches an alpha on a local assigned from the accent");
    check(hasRule(scan("m_x = theme.getColor(\"secondary\");\nauto d = m_x.lightened(0.2f);"), "R4"), "R4 catches a member too");
    check(hasRule(scan("auto v = NUIColor::fromHex(0x7c3aed);"), "R5"), "R5 catches a violet hex");
    check(hasRule(scan("auto v = NUIColor::fromHex(0x1c1530);"), "R5"), "R5 catches a dark violet hex");
    check(hasRule(scan("auto v = NUIColor(0.498f, 0.353f, 0.941f, 1.0f);"), "R5"), "R5 catches a violet float colour");
    check(hasRule(scan("set(\"#a78bfa\");"), "R5"), "R5 catches a violet string");
    check(scan("auto a = theme.getColor(\"selectionFill\"); r.fill(b, a);\nauto t = theme.getColor(\"textPrimary\").withAlpha(0.5f);\n"
               "auto g = NUIColor::fromHex(0x06c49d); auto w = NUIColor::lerp(a, theme.getColor(\"textPrimary\"), 0.3f);").empty(),
          "tokens, text alphas and non-violet colours are quiet");

    const fs::path root = AESTRA_SOURCE_DIR;
    int files = 0;
    std::set<std::string> used;
    std::vector<std::string> bad;
    for (const char* top : {"AestraUI", "Source"}) {
        if (!fs::exists(root / top)) {
            check(false, std::string("source folder not found: ") + (root / top).string() + " (AESTRA_SOURCE_DIR)");
            continue;
        }
        for (const auto& entry : fs::recursive_directory_iterator(root / top)) {
            if (!entry.is_regular_file()) continue;
            const std::string ext = entry.path().extension().string();
            if (ext != ".cpp" && ext != ".h" && ext != ".mm") continue;
            const std::string rel = fs::relative(entry.path(), root).generic_string();
            if (rel.find("/External/") != std::string::npos) continue;
            ++files;
            const auto findings = scan(slurp(entry.path()));
            if (findings.empty()) continue;
            if (allowlist().count(rel)) {
                used.insert(rel);
                continue;
            }
            for (const auto& f : findings) bad.push_back(rel + ":" + std::to_string(f.line) + " [" + f.rule + "] " + f.text.substr(0, 150));
        }
    }
    check(files > 500, "the scan reached the source tree (" + std::to_string(files) + " files)");
    for (const auto& b : bad) check(false, b);
    // An allowlist entry that no longer needs it is removed, so it cannot hide a later regression.
    for (const auto& [file, why] : allowlist()) check(used.count(file) == 1, "allowlist entry is still needed: " + file + " (" + why + ")");

    std::cout << g_checks << " checks, " << g_failures << " failure(s)\n";
    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
