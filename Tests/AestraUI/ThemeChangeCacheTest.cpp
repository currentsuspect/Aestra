// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// A theme change reaches everything that keeps theme colours or theme-made pixels (21 Nov 2026: after switching theme
// in Settings the timeline and the file browser kept the old theme until a scroll or a hover redrew them).
//
// Root cause, found on the real renderer: TrackManagerUI::onThemeChanged set the legacy cache flags but its FBO cache
// is invalidated by a different flag, so the old pixels were drawn again; FileBrowser read its colours once in the
// constructor and had no onThemeChanged at all. The fix is one rule: every theme change bumps a generation
// (NUIThemeManager::themeGeneration), every render-cache entry records the generation it was drawn under
// (RenderCacheTheme.h) and is stale when it moved, and every component is told through NUIComponent::onThemeChanged.
//
// Part 1 checks the generation and the stale rule, part 2 the dispatch through a component tree the way the window
// manager does it, part 3 reads the source: a class that copies theme colours into members in its constructor must
// have an onThemeChanged, so a new panel cannot repeat the bug.

#include "NUIComponent.h"
#include "NUIThemeSystem.h"
#include "RenderCacheTheme.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace AestraUI;
namespace fs = std::filesystem;

// CMake passes the repository root (target_compile_definitions in Tests/CMakeLists.txt). Without it (an IDE that does not
// read the compile commands) the scan would find no files, and the file-count check below fails, so a build that lost
// the definition cannot pass by scanning nothing.
#ifndef AESTRA_SOURCE_DIR
#define AESTRA_SOURCE_DIR "."
#endif

namespace {
int g_failures = 0, g_checks = 0;
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        std::cout << "  FAIL " << what << "\n";
        ++g_failures;
    }
}

// A stand-in for a panel with a colour cache: reads once, refreshes only through onThemeChanged when told to.
class Probe : public NUIComponent {
public:
    explicit Probe(bool refreshes) : refreshes_(refreshes) { cached_ = NUIThemeManager::getInstance().getColor("backgroundPrimary"); }
    void onThemeChanged(const NUIThemeProperties& theme) override {
        if (refreshes_) cached_ = NUIThemeManager::getInstance().getColor("backgroundPrimary");
        ++calls_;
        NUIComponent::onThemeChanged(theme);
    }
    NUIColor cached_;
    int calls_ = 0;
    bool refreshes_;
};

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
} // namespace

int main() {
    auto& tm = NUIThemeManager::getInstance();
    tm.setActiveTheme("ink-dark");

    // Part 1: the generation and the stale rule.
    const uint64_t g0 = tm.themeGeneration();
    check(tm.setActiveTheme("ink-dark") && tm.themeGeneration() == g0, "re-selecting the active theme changes nothing");
    check(tm.setActiveTheme("ink-light") && tm.themeGeneration() == g0 + 1, "a theme switch moves the generation on");
    const uint64_t g1 = tm.themeGeneration();
    bool valid = true;
    uint64_t built = g1;
    check(!invalidateIfThemeChanged(valid, built, tm.themeGeneration()) && valid, "a cache drawn under this theme stays valid");
    check(tm.setActiveTheme("ember-dark") && tm.themeGeneration() == g1 + 1, "the next switch moves it on again");
    check(invalidateIfThemeChanged(valid, built, tm.themeGeneration()) && !valid && built == tm.themeGeneration(),
          "a cache drawn under the previous theme is stale, and is stamped with the new generation");
    valid = true;
    check(!invalidateIfThemeChanged(valid, built, tm.themeGeneration()) && valid, "...and is then left alone until the next change");
    // Editing the active theme (a custom theme saved over it) counts as a change too.
    const uint64_t g2 = tm.themeGeneration();
    tm.setCustomTheme("ember-dark", NUIThemePresets::createKitTheme("ember-dark"));
    check(tm.themeGeneration() == g2 + 1, "editing the active theme moves the generation");

    // Part 2: a switch reaches a component tree through onThemeChanged, as AestraWindowManager drives it.
    tm.setActiveTheme("ink-dark");
    auto root = std::make_shared<NUIComponent>();
    auto good = std::make_shared<Probe>(true);
    auto stale = std::make_shared<Probe>(false); // a panel that forgot: the bug
    auto nested = std::make_shared<Probe>(true);
    good->addChild(nested);
    root->addChild(good);
    root->addChild(stale);
    const NUIColor darkBed = tm.getColor("backgroundPrimary");
    const auto subscription = tm.subscribeToThemeChanges([&](const NUIThemeProperties& t) { root->onThemeChanged(t); });
    tm.setActiveTheme("ink-light");
    const NUIColor lightBed = tm.getColor("backgroundPrimary");
    check(good->calls_ == 1 && nested->calls_ == 1 && stale->calls_ == 1, "every component in the tree hears the change once");
    check(good->cached_.r == lightBed.r && nested->cached_.r == lightBed.r, "a panel that refreshes shows the new theme at once, with no hover");
    check(stale->cached_.r == darkBed.r && stale->cached_.r != lightBed.r,
          "(negative control) a panel that only reads in its constructor keeps the old theme: the bug this fixes");
    tm.unsubscribeFromThemeChanges(subscription);

    // Part 3: no panel may copy theme colours into members without an onThemeChanged.
    const fs::path root_dir = AESTRA_SOURCE_DIR;
    const std::regex memberRead("^\\s+(m_\\w+|\\w+_)\\s*=\\s*[^=;]*(getColor|getCurrentTheme\\(\\)\\.)");
    const std::set<std::string> exempt = {
        "AestraUI/Widgets/UnitRow.cpp", // its m_look is recomputed from the theme on every render; nothing is kept across frames
    };
    int scanned = 0;
    for (const char* top : {"AestraUI/Widgets", "AestraUI/Base", "Source/Components", "Source/Panels", "Source/Settings", "Source/ui", "Source/HUD"}) {
        if (!fs::exists(root_dir / top)) {
            check(false, std::string("source folder not found: ") + (root_dir / top).string() + " (AESTRA_SOURCE_DIR)");
            continue;
        }
        for (const auto& e : fs::recursive_directory_iterator(root_dir / top)) {
            if (!e.is_regular_file() || e.path().extension() != ".cpp") continue;
            ++scanned;
            const std::string rel = fs::relative(e.path(), root_dir).generic_string();
            const std::string body = slurp(e.path());
            std::stringstream ss(body);
            bool reads = false;
            for (std::string line; std::getline(ss, line);) {
                if (std::regex_search(line, memberRead)) { reads = true; break; }
            }
            if (!reads || exempt.count(rel)) continue;
            fs::path header = e.path();
            header.replace_extension(".h");
            const bool handles = body.find("onThemeChanged") != std::string::npos ||
                                 (fs::exists(header) && slurp(header).find("onThemeChanged") != std::string::npos);
            check(handles, rel + " copies theme colours into members but has no onThemeChanged");
        }
    }
    check(scanned > 100, "the source scan reached the widgets and panels (" + std::to_string(scanned) + " files)");

    std::cout << g_checks << " checks, " << g_failures << " failure(s)\n";
    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
