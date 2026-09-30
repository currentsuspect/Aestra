// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The file browser's library: places outside the library root, Favorites and
// Collections as cross-folder views, and the state that has to survive a restart.
//
// Each case runs against a throwaway $HOME, because FileBrowser reads it at
// construction for both its library root (~/Documents/Aestra) and the system
// places (Home, Downloads, ...). Scans are asynchronous; waitForScan() pumps the
// worker's results the same way onUpdate() does every frame.

#include "../Support/NullRenderer.h"

#include "BrowserLibrary.h"
#include "FileBrowser.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using AestraUI::FileBrowser;
namespace BrowserLibrary = AestraUI::BrowserLibrary;

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                                                                                              \
    do {                                                                                                              \
        if (!(cond)) {                                                                                                \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                        \
            ++g_failures;                                                                                             \
        }                                                                                                             \
    } while (0)

void setEnv(const char* key, const std::string& value) {
#if defined(_WIN32)
    _putenv_s(key, value.c_str());
#else
    setenv(key, value.c_str(), 1);
#endif
}

void touch(const fs::path& p) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << "x";
}

std::string canon(const fs::path& p) {
    return fs::weakly_canonical(p).string();
}

// A fresh $HOME with a library root, a Downloads folder outside it and a Music
// folder, each holding one sound.
struct Sandbox {
    fs::path home;

    explicit Sandbox(const char* name) {
        home = fs::temp_directory_path() /
               ("aestra_browser_" + std::string(name) + "_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(home / "Documents" / "Aestra");
        touch(home / "Downloads" / "kick.wav");
        touch(home / "Music" / "pad.wav");
        setEnv("HOME", home.string());
#if defined(_WIN32)
        setEnv("USERPROFILE", home.string());
#endif
        setEnv("XDG_CONFIG_HOME", (home / ".config").string());
    }
    ~Sandbox() {
        std::error_code ec;
        fs::remove_all(home, ec);
    }
};

bool waitForScan(FileBrowser& browser) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        browser.pollScanResults();
        if (!browser.isScanPending()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

std::vector<std::string> listedNames(const FileBrowser& browser) {
    std::vector<std::string> names;
    for (const auto* item : browser.getVisibleFiles()) {
        if (item) names.push_back(item->name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool lists(const FileBrowser& browser, const std::string& name) {
    const auto names = listedNames(browser);
    return std::find(names.begin(), names.end(), name) != names.end();
}

// 600px tall by default: a realistic docked browser, where the nav pane scrolls.
std::unique_ptr<FileBrowser> makeBrowser(float height = 600.0f) {
    auto browser = std::make_unique<FileBrowser>();
    browser->setBounds(AestraUI::NUIRect(0.0f, 0.0f, 900.0f, height));
    browser->onResize(900, static_cast<int>(height));
    return browser;
}

// Paint once so the nav pane lays out its rows, then click the first row with
// @p action (and @p label, when several rows share an action). Returns false if
// the row is missing or below the fold, where a user could not click it either.
bool clickNav(FileBrowser& browser, FileBrowser::BrowserNavAction action, const std::string& label = {},
              AestraUI::NUIMouseButton button = AestraUI::NUIMouseButton::Left) {
    Aestra::Testing::NullRenderer renderer;
    browser.onRender(renderer);
    for (const auto& hit : browser.getNavHits()) {
        if (hit.action != action || (!label.empty() && hit.label != label)) continue;
        if (hit.bounds.bottom() > browser.computeBrowserLayout().navPane.bottom()) return false;
        AestraUI::NUIMouseEvent press;
        press.type = AestraUI::NUIMouseEventType::Down;
        press.button = button;
        press.pressed = true;
        press.position = {hit.bounds.x + hit.bounds.width * 0.5f, hit.bounds.y + hit.bounds.height * 0.5f};
        browser.onMouseEvent(press);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Pure helpers
// ---------------------------------------------------------------------------

void testNaturalCompare() {
    using BrowserLibrary::naturalCompare;
    CHECK(naturalCompare("Kick 2", "Kick 10") < 0, "digit runs compare by value");
    CHECK(naturalCompare("Kick 10", "Kick 2") > 0, "digit runs compare by value (reversed)");
    CHECK(naturalCompare("apple", "Banana") < 0, "case-insensitive: apple before Banana");
    CHECK(naturalCompare("Zebra", "apple") > 0, "case-insensitive: Zebra after apple");
    CHECK(naturalCompare("Snare", "snare") == 0, "case-only difference ties (caller tie-breaks)");
    CHECK(naturalCompare("take01", "take1") == 0, "leading zeros tie");
    CHECK(naturalCompare("take", "take1") < 0, "prefix sorts first");
    CHECK(naturalCompare("808 bass", "90 bpm loop") > 0, "808 > 90 numerically");
}

void testXdgParsing() {
    const std::string content = "# comment\n"
                                "XDG_DOWNLOAD_DIR=\"$HOME/Incoming\"\n"
                                "XDG_MUSIC_DIR=\"/srv/music\"\n"
                                "XDG_DESKTOP_DIR=\"relative/not/allowed\"\n"
                                "garbage line\n";
    const auto dirs = BrowserLibrary::parseXdgUserDirs(content, "/home/me");
    CHECK(dirs.size() == 2, "two valid XDG entries");
    CHECK(dirs.size() >= 1 && dirs[0].first == "DOWNLOAD" && dirs[0].second == "/home/me/Incoming",
          "$HOME expands in XDG_DOWNLOAD_DIR");
    CHECK(dirs.size() >= 2 && dirs[1].first == "MUSIC" && dirs[1].second == "/srv/music", "absolute XDG path kept");
}

void testSystemPlaceDiscovery() {
    Sandbox box("places");
    // Downloads relocated by XDG; Desktop disabled (points at $HOME); Documents
    // exists; Music exists under its default name.
    fs::create_directories(box.home / "Incoming");
    fs::create_directories(box.home / "Desktop");
    const std::string xdg = "XDG_DOWNLOAD_DIR=\"$HOME/Incoming\"\nXDG_DESKTOP_DIR=\"$HOME/\"\n";
    const auto places = BrowserLibrary::discoverSystemPlaces(box.home.string(), xdg);

    std::vector<std::string> labels;
    for (const auto& p : places) labels.push_back(p.label);
    const std::vector<std::string> expected = {"Home", "Downloads", "Documents", "Music"};
    CHECK(labels == expected, "Home, relocated Downloads, Documents, Music; disabled Desktop dropped");
    const auto downloads = std::find_if(places.begin(), places.end(), [](const auto& p) { return p.label == "Downloads"; });
    CHECK(downloads != places.end() && canon(downloads->path) == canon(box.home / "Incoming"),
          "Downloads follows XDG_DOWNLOAD_DIR");
}

// ---------------------------------------------------------------------------
// FileBrowser
// ---------------------------------------------------------------------------

// The library root used to be a jail: every path outside ~/Documents/Aestra
// was rewritten to the root, so an added Downloads place navigated home.
void testBrowsesOutsideLibraryRoot() {
    Sandbox box("outside");
    auto browser = makeBrowser();
    CHECK(waitForScan(*browser), "initial scan finishes");
    CHECK(canon(browser->getCurrentPath()) == canon(box.home / "Documents" / "Aestra"), "starts at library root");

    browser->navigateTo((box.home / "Downloads").string());
    CHECK(waitForScan(*browser), "Downloads scan finishes");
    CHECK(canon(browser->getCurrentPath()) == canon(box.home / "Downloads"), "navigates outside the library root");
    CHECK(lists(*browser, "kick.wav"), "Downloads contents are listed");

    browser->navigateTo((box.home / "Documents" / "Aestra").string());
    CHECK(waitForScan(*browser), "root scan finishes");
    browser->navigateUp();
    CHECK(waitForScan(*browser), "parent scan finishes");
    CHECK(canon(browser->getCurrentPath()) == canon(box.home / "Documents"), "Up goes past the library root");
}

void testSystemPlaceRowNavigates() {
    Sandbox box("placerow");
    auto browser = makeBrowser();
    CHECK(waitForScan(*browser), "initial scan finishes");
    CHECK(clickNav(*browser, FileBrowser::BrowserNavAction::SystemPlace, "Downloads"),
          "Downloads row is on screen without scrolling the nav pane");
    CHECK(waitForScan(*browser), "Downloads scan finishes");
    CHECK(canon(browser->getCurrentPath()) == canon(box.home / "Downloads"), "clicking Downloads opens it");
}

void testFavoritesListAcrossFolders() {
    Sandbox box("favorites");
    auto browser = makeBrowser();
    CHECK(waitForScan(*browser), "initial scan finishes");
    const std::string startPath = browser->getCurrentPath();

    browser->addToFavorites((box.home / "Downloads" / "kick.wav").string());
    browser->addToFavorites((box.home / "Music").string());

    CHECK(clickNav(*browser, FileBrowser::BrowserNavAction::Favorites), "Favorites row is shown");
    CHECK(waitForScan(*browser), "favorites listing finishes");
    CHECK(browser->isShowingListing(), "Favorites opens a listing, not a menu");
    CHECK(listedNames(*browser) == (std::vector<std::string>{"Music", "kick.wav"}),
          "favorites from two folders are listed together");

    browser->removeFromFavorites((box.home / "Music").string());
    CHECK(waitForScan(*browser), "listing refresh finishes");
    CHECK(listedNames(*browser) == std::vector<std::string>{"kick.wav"}, "unfavorited row leaves the open list");

    browser->navigateUp();
    CHECK(waitForScan(*browser), "return scan finishes");
    CHECK(!browser->isShowingListing(), "Up leaves the Favorites list");
    CHECK(browser->getCurrentPath() == startPath, "and returns to the folder underneath");
}

void testCollectionListsTaggedItemsAnywhere() {
    Sandbox box("collection");
    auto browser = makeBrowser();
    CHECK(waitForScan(*browser), "initial scan finishes");

    // Tagged item lives outside the folder on screen: the old tag filter only
    // searched the current tree, so it could never appear.
    browser->toggleTag((box.home / "Music" / "pad.wav").string(), "Purple");
    CHECK(clickNav(*browser, FileBrowser::BrowserNavAction::Purple), "Purple row is shown");
    CHECK(waitForScan(*browser), "collection listing finishes");
    CHECK(browser->isShowingListing() && browser->getListingTitle() == "Purple", "Purple collection is open");
    CHECK(listedNames(*browser) == std::vector<std::string>{"pad.wav"}, "tagged file from another folder is listed");

    browser->toggleTag((box.home / "Music" / "pad.wav").string(), "Purple");
    CHECK(waitForScan(*browser), "collection refresh finishes");
    CHECK(listedNames(*browser).empty(), "untagging removes it from the open collection");
}

// Category rows used to navigate to User Library/<Category> AND filter by the
// same-named tag, hiding every untagged file the user put in that folder.
void testCategoryFolderShowsUntaggedFiles() {
    Sandbox box("category");
    touch(box.home / "Documents" / "Aestra" / "User Library" / "Drums" / "snare.wav");
    auto browser = makeBrowser(1100.0f); // tall enough that Categories need no nav scroll
    CHECK(waitForScan(*browser), "initial scan finishes");

    CHECK(clickNav(*browser, FileBrowser::BrowserNavAction::Drums, "Drums"), "Drums category row is shown");
    CHECK(waitForScan(*browser), "category scan finishes");
    CHECK(canon(browser->getCurrentPath()) == canon(box.home / "Documents" / "Aestra" / "User Library" / "Drums"),
          "Drums category opens its folder");
    CHECK(lists(*browser, "snare.wav"), "untagged file in the category folder is visible");
}

void testLibraryStateRoundTrip() {
    Sandbox box("persist");
    const std::string statePath = (box.home / "browser_library.json").string();
    const std::string kick = (box.home / "Downloads" / "kick.wav").string();
    const std::string downloads = (box.home / "Downloads").string();
    const std::string newRoot = (box.home / "Music").string();
    {
        auto browser = makeBrowser();
        CHECK(waitForScan(*browser), "initial scan finishes");
        browser->setStatePath(statePath);
        browser->addToFavorites(kick);
        browser->addPlace(downloads);
        browser->toggleTag(kick, "Drums");
        browser->setSortMode(FileBrowser::SortMode::Size);
        browser->setSortAscending(false);
        browser->setLibraryRoot(newRoot);
        CHECK(fs::exists(statePath), "library changes are written without an explicit save");
    }

    auto restored = makeBrowser();
    CHECK(waitForScan(*restored), "initial scan finishes");
    // Non-vacuous: a fresh browser starts with none of it.
    CHECK(restored->getFavorites().empty() && restored->getPlaces().empty(), "fresh browser has no library state");
    CHECK(restored->loadState(statePath), "state file loads");
    CHECK(restored->isFavorite(kick), "favorite survives");
    CHECK(restored->isPlace(downloads), "place survives");
    CHECK(restored->hasTag(kick, "Drums"), "collection tag survives");
    CHECK(restored->getSortMode() == FileBrowser::SortMode::Size && !restored->isSortAscending(), "sort survives");
    CHECK(canon(restored->getLibraryRoot()) == canon(newRoot), "library root survives");
}

// Builds before v3 only ever wrote ~/.config/aestra/browser_settings.json (on a
// place drop) and never read it back. The first v3 launch imports it once.
void testLegacySettingsImport() {
    Sandbox box("legacy");
    const fs::path legacy = box.home / ".config" / "aestra" / "browser_settings.json";
    fs::create_directories(legacy.parent_path());
    const std::string downloads = (box.home / "Downloads").string();
    {
        std::ofstream out(legacy);
        out << "{\"version\": 2, \"currentPath\": \"/nowhere\", \"customPlaces\": [\"" << downloads
            << "\"], \"favorites\": [], \"tagsByPath\": {}}";
    }
    const std::string statePath = (box.home / "browser_library.json").string();

    auto browser = makeBrowser();
    CHECK(waitForScan(*browser), "initial scan finishes");
    browser->initLibraryState(statePath);
    CHECK(browser->isPlace(downloads), "legacy place imported");
    CHECK(fs::exists(statePath), "import written forward to the v3 file");
    CHECK(fs::exists(legacy), "legacy file left untouched");
}

void testSortIsNaturalAndCaseInsensitive() {
    Sandbox box("sort");
    const fs::path dir = box.home / "Documents" / "Aestra";
    for (const char* name : {"Kick 10.wav", "kick 2.wav", "Zap.wav", "air.wav"}) touch(dir / name);
    auto browser = makeBrowser();
    CHECK(waitForScan(*browser), "initial scan finishes");

    std::vector<std::string> order;
    for (const auto* item : browser->getVisibleFiles()) order.push_back(item->name);
    const std::vector<std::string> expected = {"air.wav", "kick 2.wav", "Kick 10.wav", "Zap.wav"};
    CHECK(order == expected, "name sort is natural and case-insensitive");
}

} // namespace

int main() {
    testNaturalCompare();
    testXdgParsing();
    testSystemPlaceDiscovery();
    testBrowsesOutsideLibraryRoot();
    testSystemPlaceRowNavigates();
    testFavoritesListAcrossFolders();
    testCollectionListsTaggedItemsAnywhere();
    testCategoryFolderShowsUntaggedFiles();
    testLibraryStateRoundTrip();
    testLegacySettingsImport();
    testSortIsNaturalAndCaseInsensitive();

    if (g_failures == 0) {
        std::printf("FileBrowserLibraryTest: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "FileBrowserLibraryTest: %d check(s) failed\n", g_failures);
    return 1;
}
