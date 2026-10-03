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
#include "BrowserLibraryIndex.h"
#include "FileBrowser.h"

#include <algorithm>
#include <cmath>
#include <cstring>
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
#if !defined(_WIN32) // XDG user-dirs is a POSIX contract; "/srv/music" is not absolute on Windows
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
#endif
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
    CHECK(clickNav(*browser, FileBrowser::BrowserNavAction::Collection, "Purple"), "Purple row is shown");
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

// Collections are the user's, not a hardcoded four: create, rename (members
// follow), delete (members ungrouped, files untouched), and they persist.
void testUserCollections() {
    Sandbox box("usercollections");
    const std::string statePath = (box.home / "browser_library.json").string();
    const std::string kick = (box.home / "Downloads" / "kick.wav").string();
    {
        auto browser = makeBrowser();
        CHECK(waitForScan(*browser), "initial scan finishes");
        browser->setStatePath(statePath);
        CHECK(browser->createCollection("Kicks"), "create a collection");
        CHECK(!browser->createCollection("Kicks"), "names are unique");
        CHECK(!browser->createCollection("   "), "blank names refused");
        browser->toggleTag(kick, "Kicks");
        CHECK(browser->renameCollection("Kicks", " 808s "), "rename");
        CHECK(browser->hasTag(kick, "808s") && !browser->hasTag(kick, "Kicks"), "members follow a rename");
        CHECK(!browser->renameCollection("808s", "Purple"), "rename onto an existing name refused");
    }
    auto restored = makeBrowser();
    CHECK(waitForScan(*restored), "initial scan finishes");
    CHECK(restored->loadState(statePath), "state loads");
    const auto& names = restored->getCollections();
    CHECK(std::find(names.begin(), names.end(), "808s") != names.end(), "a user collection survives restart");
    CHECK(restored->hasTag(kick, "808s"), "its member survives restart");

    restored->deleteCollection("808s");
    CHECK(!restored->hasTag(kick, "808s"), "deleting ungroups the member");
    CHECK(fs::exists(kick), "and leaves the file alone");
    const auto& after = restored->getCollections();
    CHECK(std::find(after.begin(), after.end(), "808s") == after.end(), "the collection is gone");
}

// "+ New Collection" creates one and opens the inline name editor; a click
// elsewhere commits, and the editor is released on the next paint.
void testNewCollectionRowOpensEditor() {
    Sandbox box("newcollection");
    auto browser = makeBrowser(1100.0f);
    CHECK(waitForScan(*browser), "initial scan finishes");
    CHECK(clickNav(*browser, FileBrowser::BrowserNavAction::AddCollection), "+ New Collection row is shown");
    const auto& names = browser->getCollections();
    CHECK(std::find(names.begin(), names.end(), "New Collection") != names.end(), "a collection was created");
    CHECK(browser->isRenamingCollection(), "its name editor is open");

    browser->blurSearchIfPressOutside({-50.0f, -50.0f});
    CHECK(!browser->isRenamingCollection(), "a click elsewhere commits the name");
    Aestra::Testing::NullRenderer renderer;
    browser->onRender(renderer); // releases the retired editor; must not crash
    CHECK(clickNav(*browser, FileBrowser::BrowserNavAction::Collection, "New Collection"),
          "the new collection has a nav row");
}

// Current Project used to open the library root. It lists the project's audio.
void testCurrentProjectListsProjectAudio() {
    Sandbox box("project");
    auto browser = makeBrowser(1100.0f);
    CHECK(waitForScan(*browser), "initial scan finishes");
    const std::string kick = (box.home / "Downloads" / "kick.wav").string();
    const std::string pad = (box.home / "Music" / "pad.wav").string();
    const std::string gone = (box.home / "Music" / "deleted.wav").string();
    browser->setProjectFilesProvider([=]() { return std::vector<std::string>{pad, kick, gone, kick}; });

    CHECK(clickNav(*browser, FileBrowser::BrowserNavAction::CurrentProject), "Current Project row is shown");
    CHECK(waitForScan(*browser), "project listing finishes");
    CHECK(browser->isShowingListing() && browser->getListingTitle() == "Current Project", "Current Project is a list");
    CHECK(listedNames(*browser) == (std::vector<std::string>{"kick.wav", "pad.wav"}),
          "the project's files, once each, missing ones left out");
}

// Samples used to be a second "Sounds" (an audio filter). It is a place now.
void testSamplesIsAFolder() {
    Sandbox box("samples");
    touch(box.home / "Documents" / "Aestra" / "User Library" / "Samples" / "hat.wav");
    auto browser = makeBrowser(1100.0f);
    CHECK(waitForScan(*browser), "initial scan finishes");
    CHECK(clickNav(*browser, FileBrowser::BrowserNavAction::Samples), "Samples row is shown");
    CHECK(waitForScan(*browser), "Samples scan finishes");
    CHECK(canon(browser->getCurrentPath()) == canon(box.home / "Documents" / "Aestra" / "User Library" / "Samples"),
          "Samples opens its folder");
    CHECK(lists(*browser, "hat.wav"), "its sounds are listed");
}

// ---------------------------------------------------------------------------
// Audio facts and library search (commit 4)
// ---------------------------------------------------------------------------

void putLE(std::string& b, uint32_t v, int n) {
    for (int i = 0; i < n; ++i) b.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
void putBE(std::string& b, uint32_t v, int n) {
    for (int i = n - 1; i >= 0; --i) b.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}

// A 16-bit PCM WAV header describing @p seconds of audio. The data chunk is
// declared but not written: the reader must not need the samples. @p tempo > 0
// adds an ACID chunk with that tempo, placed after data (as loop tools do).
void writeWav(const fs::path& p, uint32_t rate, uint16_t channels, double seconds, float tempo = 0.0f) {
    fs::create_directories(p.parent_path());
    const uint32_t blockAlign = channels * 2u;
    const uint32_t dataBytes = static_cast<uint32_t>(seconds * rate) * blockAlign;
    std::string b = "RIFF";
    putLE(b, 36 + dataBytes, 4);
    b += "WAVEfmt ";
    putLE(b, 16, 4);
    putLE(b, 1, 2);
    putLE(b, channels, 2);
    putLE(b, rate, 4);
    putLE(b, rate * blockAlign, 4);
    putLE(b, blockAlign, 2);
    putLE(b, 16, 2);
    b += "data";
    putLE(b, dataBytes, 4);
    b.append(dataBytes, '\0');
    if (tempo > 0.0f) {
        b += "acid";
        putLE(b, 24, 4);
        b.append(20, '\0');
        uint32_t bits = 0;
        std::memcpy(&bits, &tempo, sizeof(bits));
        putLE(b, bits, 4);
    }
    std::ofstream(p, std::ios::binary) << b;
}

void writeAiff(const fs::path& p, uint16_t channels, uint32_t frames) {
    std::string b = "FORM";
    putBE(b, 4 + 8 + 18, 4);
    b += "AIFFCOMM";
    putBE(b, 18, 4);
    putBE(b, channels, 2);
    putBE(b, frames, 4);
    putBE(b, 16, 2);
    // 48000 as an 80-bit extended: exponent 16383+15, mantissa 48000 << 48.
    putBE(b, 16383 + 15, 2);
    putBE(b, 0xBB800000u, 4);
    putBE(b, 0, 4);
    std::ofstream(p, std::ios::binary) << b;
}

void writeFlac(const fs::path& p, uint32_t rate, uint16_t channels, uint64_t samples) {
    std::string b = "fLaC";
    b.push_back(static_cast<char>(0x80)); // last block, STREAMINFO
    putBE(b, 34, 3);
    b.append(10, '\0'); // block + frame sizes
    const uint64_t packed = (uint64_t(rate) << 44) | (uint64_t(channels - 1) << 41) | (uint64_t(15) << 36) | samples;
    for (int i = 7; i >= 0; --i) b.push_back(static_cast<char>((packed >> (8 * i)) & 0xFF));
    b.append(16, '\0'); // MD5
    std::ofstream(p, std::ios::binary) << b;
}

void testAudioHeaders() {
    Sandbox box("headers");
    BrowserLibrary::AudioInfo info;
    writeWav(box.home / "loop.wav", 44100, 2, 2.0, 124.0f);
    CHECK(BrowserLibrary::readAudioInfo((box.home / "loop.wav").string(), info), "WAV header reads");
    CHECK(info.sampleRate == 44100 && info.channels == 2, "WAV rate/channels");
    CHECK(std::abs(info.durationSec - 2.0) < 1e-6, "WAV length from the data chunk");
    CHECK(std::abs(info.tempo - 124.0f) < 1e-3, "ACID tempo after the data chunk");

    writeAiff(box.home / "pad.aif", 1, 96000);
    info = {};
    CHECK(BrowserLibrary::readAudioInfo((box.home / "pad.aif").string(), info), "AIFF header reads");
    CHECK(info.sampleRate == 48000 && info.channels == 1 && std::abs(info.durationSec - 2.0) < 1e-6,
          "AIFF 80-bit rate, frames, channels");

    writeFlac(box.home / "hit.flac", 96000, 2, 48000);
    info = {};
    CHECK(BrowserLibrary::readAudioInfo((box.home / "hit.flac").string(), info), "FLAC STREAMINFO reads");
    CHECK(info.sampleRate == 96000 && info.channels == 2 && std::abs(info.durationSec - 0.5) < 1e-6,
          "FLAC rate, channels, length");

    touch(box.home / "notaudio.wav");
    CHECK(!BrowserLibrary::readAudioInfo((box.home / "notaudio.wav").string(), info), "a non-WAV .wav is refused");
}

void testNameFacts() {
    using BrowserLibrary::parseKeyFromFilename;
    CHECK(parseKeyFromFilename("Loop_Am_120bpm.wav") == "Am", "Am");
    CHECK(parseKeyFromFilename("pad F#min.wav") == "F#m", "F#min -> F#m");
    CHECK(parseKeyFromFilename("bass Ebmaj.wav") == "Eb", "Ebmaj -> Eb");
    CHECK(parseKeyFromFilename("Kick A.wav").empty(), "a bare letter is not a key");
    CHECK(parseKeyFromFilename("Snare C 02.wav").empty(), "a bare C is not a key");
    CHECK(BrowserLibrary::parseBpmFromFilename("loop 128 BPM.wav") == 128, "filename BPM");
    CHECK(BrowserLibrary::parseBpmFromFilename("Drum Loop Vintage 90 Kit 120bpm.wav") == 120,
          "a later 'bpm' does not claim an earlier number");

    const auto q = BrowserLibrary::parseSearchQuery("Dark Pad bpm:90-100 len:<4 key:am foo:bar");
    CHECK(q.text == "dark pad foo:bar", "name text keeps plain words and unknown fields");
    CHECK(q.bpmMin == 90 && q.bpmMax == 100, "bpm range");
    CHECK(q.lenMin < 0.0 && q.lenMax == 4.0, "len upper bound");
    CHECK(q.key == "Am", "key normalised");
    const auto shortHits = BrowserLibrary::parseSearchQuery("len:2");
    CHECK(shortHits.lenMin < 0.0 && shortHits.lenMax == 2.0, "a bare length means up to");
}

std::vector<std::string> visibleNames(FileBrowser& b) {
    std::vector<std::string> names;
    for (const auto* item : b.getVisibleFiles()) names.push_back(item->name);
    std::sort(names.begin(), names.end());
    return names;
}

void testMetadataSearchAndSort() {
    Sandbox box("metasearch");
    const fs::path dir = box.home / "Documents" / "Aestra";
    writeWav(dir / "loop_Am_120bpm.wav", 44100, 2, 4.0);
    writeWav(dir / "kick.wav", 44100, 1, 0.3);
    writeWav(dir / "pad_Dm.wav", 48000, 2, 8.0, 95.0f); // tempo only from ACID
    auto browser = makeBrowser();
    CHECK(waitForScan(*browser), "initial scan finishes");

    browser->setSearchWholeLibrary(false);
    browser->setSearchQuery("len:<1");
    CHECK(visibleNames(*browser) == std::vector<std::string>{"kick.wav"}, "len:<1 finds the one-shot");
    browser->setSearchQuery("bpm:90-100");
    CHECK(visibleNames(*browser) == std::vector<std::string>{"pad_Dm.wav"}, "bpm from the ACID chunk is searchable");
    browser->setSearchQuery("key:Am");
    CHECK(visibleNames(*browser) == std::vector<std::string>{"loop_Am_120bpm.wav"}, "key from the filename");
    browser->setSearchQuery("");

    browser->setSortMode(FileBrowser::SortMode::Length);
    std::vector<std::string> order;
    for (const auto* item : browser->getVisibleFiles()) order.push_back(item->name);
    CHECK(order == (std::vector<std::string>{"kick.wav", "loop_Am_120bpm.wav", "pad_Dm.wav"}), "sort by length");
}

// Search used to reach only folders already expanded on screen.
void testSearchReachesTheWholeLibrary() {
    Sandbox box("librarysearch");
    const fs::path elsewhere = box.home / "Samples" / "Deep" / "Folder";
    writeWav(elsewhere / "zebra_snare.wav", 44100, 1, 0.2);
    auto browser = makeBrowser();
    CHECK(waitForScan(*browser), "initial scan finishes");
    browser->addPlace((box.home / "Samples").string());
    browser->rescanLibrary();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (browser->isLibraryIndexing() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(!browser->isLibraryIndexing(), "index finishes");

    browser->setSearchQuery("zebra");
    CHECK(visibleNames(*browser) == std::vector<std::string>{"zebra_snare.wav"},
          "a file three folders deep in an unopened place is found");
    browser->setSearchWholeLibrary(false);
    CHECK(visibleNames(*browser).empty(), "This Folder scope does not reach it");
}

// Second and later launches: a state file exists, and loading it must still
// start the library index, or whole-library search finds nothing.
void testExistingStateStartsTheIndex() {
    Sandbox box("statelaunch");
    const std::string statePath = (box.home / "browser_library.json").string();
    writeWav(box.home / "Samples" / "Deep" / "yak_tom.wav", 44100, 1, 0.1);
    {
        auto first = makeBrowser();
        CHECK(waitForScan(*first), "initial scan finishes");
        first->initLibraryState(statePath);
        first->addPlace((box.home / "Samples").string());
        CHECK(fs::exists(statePath), "first launch writes the state file");
    }
    auto again = makeBrowser();
    CHECK(waitForScan(*again), "initial scan finishes");
    again->initLibraryState(statePath);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (again->isLibraryIndexing() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    again->setSearchQuery("yak");
    CHECK(visibleNames(*again) == std::vector<std::string>{"yak_tom.wav"},
          "a launch with an existing state file still indexes the library");
}

// Overlapping roots are resolved once, up front: a root inside another, and the
// same folder spelled twice, must not index a file twice. A sibling whose name
// merely starts the same ("Samples2" next to "Samples") is NOT inside it.
void testOverlappingRootsIndexOnce() {
    Sandbox box("libraryoverlap");
    const fs::path samples = box.home / "Samples";
    writeWav(samples / "Deep" / "kick.wav", 44100, 1, 0.1);
    writeWav(box.home / "Samples2" / "hat.wav", 44100, 1, 0.1);

    AestraUI::BrowserLibraryIndex index;
    index.rebuild({samples.string(), (samples / "Deep").string(), (samples / "." / "Deep" / "..").string(),
                   (box.home / "Samples2").string()});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (index.isCrawling() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(!index.isCrawling(), "overlap crawl finishes");
    const auto snap = index.snapshot();
    CHECK(snap != nullptr, "overlap crawl publishes");
    std::vector<std::string> names;
    if (snap) {
        for (const auto& item : *snap)
            names.push_back(item.name);
    }
    std::sort(names.begin(), names.end());
    CHECK(names == (std::vector<std::string>{"hat.wav", "kick.wav"}),
          "each file indexed exactly once across nested and duplicate roots; the Samples2 sibling is kept");
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
    testUserCollections();
    testNewCollectionRowOpensEditor();
    testCurrentProjectListsProjectAudio();
    testSamplesIsAFolder();
    testAudioHeaders();
    testNameFacts();
    testMetadataSearchAndSort();
    testSearchReachesTheWholeLibrary();
    testOverlappingRootsIndexOnce();
    testExistingStateStartsTheIndex();

    if (g_failures == 0) {
        std::printf("FileBrowserLibraryTest: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "FileBrowserLibraryTest: %d check(s) failed\n", g_failures);
    return 1;
}
