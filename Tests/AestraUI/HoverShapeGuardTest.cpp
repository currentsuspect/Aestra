// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Guard: a hover or pressed fill follows the shape of what is hovered (owner, 21 Nov 2026: the hovers were plain rectangles over
// rounded pills, chips and panels, so their corners poked out). The rule is one helper, Depth::hoverFill (the component's own
// radius, a capsule stays a capsule), or fillRoundedRect with that radius. This test reads the source tree and fails on a
// fillRect line that names a hover, hot or pressed state, outside the short list of files whose components are sharp by design
// (Design Spec: radius none, or a plain data cell). A rounded component added later cannot slip a rectangle in.
//
// It runs over fixtures first so the rule cannot go quiet, and fails if the scan reaches fewer than 500 files.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef AESTRA_SOURCE_DIR
#define AESTRA_SOURCE_DIR "."
#endif

namespace fs = std::filesystem;

namespace {
int g_failures = 0, g_checks = 0;
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        std::cout << "  FAIL " << what << "\n";
        ++g_failures;
    }
}

std::vector<int> offending(const std::string& text) {
    static const std::regex fill("\\bfillRect\\s*\\(");
    static const std::regex state("\\b(\\w*[Hh]over\\w*|\\w*[Hh]ot|\\w*[Pp]ressed\\w*|isHovered)\\b|\"hover\"|\"pressed\"");
    std::vector<int> lines;
    std::stringstream ss(text);
    int n = 0;
    for (std::string l; std::getline(ss, l);) {
        ++n;
        const size_t cut = l.find("//");
        const std::string code = cut == std::string::npos ? l : l.substr(0, cut);
        if (std::regex_search(code, fill) && std::regex_search(code, state)) lines.push_back(n);
    }
    return lines;
}

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Components that are sharp by design, so their hover rectangle IS their shape.
const std::map<std::string, std::string>& allowlist() {
    static const std::map<std::string, std::string> list = {
        {"AestraUI/Base/NUICustomTitleBar.cpp", "the version chip is a sharp filled and stroked rectangle"},
        {"AestraUI/Widgets/NUIStepSequencer.cpp", "step cells are sharp rectangles with a 1 px stroke"},
        {"AestraUI/Widgets/PianoRollGrid.cpp", "a hovered pitch row of the note grid"},
        {"AestraUI/Widgets/PluginPresetBar.cpp", "2 px dots of the more-menu glyph"},
        {"AestraUI/Widgets/UIInsertRoutePicker.cpp", "sharp trigger and list rows (Design Spec: dropdown lists are radius none)"},
        {"AestraUI/Widgets/UIMixerPluginDropdown.cpp", "dropdown list rows are radius none"},
        {"AestraUI/Widgets/UIRoutingMapDraw.cpp", "routing list rows are full-width sharp rows"},
        {"Source/Components/TrackUIComponent.cpp", "a track header is a full-width lane row"},
        {"Source/Panels/AuditionFilesPage.cpp", "the Audition pages draw sharp rectangular buttons and rows"},
        {"Source/Panels/AuditionHub.cpp", "the Audition pages draw sharp rectangular buttons, nav rows and cover art"},
        {"Source/Panels/AuditionHubItem.cpp", "the Audition item page draws a sharp rectangular button"},
        {"Source/Panels/AuditionQueuePage.cpp", "the Audition pages draw sharp rectangular buttons and rows"},
    };
    return list;
}
} // namespace

int main() {
    check(!offending("r.fillRect(chip, hovered ? a : b);").empty(), "a fillRect over a hovered chip is caught");
    check(!offending("if (hot) r.fillRect(row, c);").empty(), "a fillRect under a hot condition is caught");
    check(!offending("r.fillRect(b, m_hoverColor);").empty(), "a hover colour in a fillRect is caught");
    check(!offending("r.fillRect(b, theme.getColor(\"pressed\"));").empty(), "the pressed token in a fillRect is caught");
    check(offending("Depth::hoverFill(r, chip, 10.0f, c);\nr.fillRoundedRect(chip, 3.0f, hovered ? a : b);\nr.fillRect(bg, card);").empty(),
          "hoverFill, fillRoundedRect and a plain fillRect are quiet");

    const fs::path root = AESTRA_SOURCE_DIR;
    int files = 0;
    std::set<std::string> used;
    for (const char* top : {"AestraUI", "Source"}) {
        if (!fs::exists(root / top)) {
            check(false, std::string("source folder not found: ") + (root / top).string());
            continue;
        }
        for (const auto& e : fs::recursive_directory_iterator(root / top)) {
            if (!e.is_regular_file()) continue;
            const std::string ext = e.path().extension().string();
            if (ext != ".cpp" && ext != ".h" && ext != ".mm") continue;
            const std::string rel = fs::relative(e.path(), root).generic_string();
            if (rel.find("/External/") != std::string::npos) continue;
            ++files;
            const auto lines = offending(slurp(e.path()));
            if (lines.empty()) continue;
            if (allowlist().count(rel)) {
                used.insert(rel);
                continue;
            }
            for (int n : lines) check(false, rel + ":" + std::to_string(n) + " fills a hover/pressed state with fillRect: use Depth::hoverFill with the component's radius");
        }
    }
    check(files > 500, "the scan reached the source tree (" + std::to_string(files) + " files)");
    for (const auto& [file, why] : allowlist()) check(used.count(file) == 1, "allowlist entry is still needed: " + file + " (" + why + ")");

    std::cout << g_checks << " checks, " << g_failures << " failure(s)\n";
    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
