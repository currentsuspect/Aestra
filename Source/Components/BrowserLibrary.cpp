// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "BrowserLibrary.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace AestraUI::BrowserLibrary {

namespace {

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

char foldCase(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::string compareKey(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(p, ec);
    std::string s = (ec ? p.lexically_normal() : canonical).generic_string();
    while (s.size() > 1 && s.back() == '/') s.pop_back();
#if defined(_WIN32)
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return s;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in.is_open()) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

int naturalCompare(std::string_view a, std::string_view b) {
    size_t i = 0;
    size_t j = 0;
    while (i < a.size() && j < b.size()) {
        if (isDigit(a[i]) && isDigit(b[j])) {
            // Compare whole digit runs by value: skip leading zeros, then the
            // longer run is the larger number, then the first differing digit.
            while (i < a.size() && a[i] == '0') ++i;
            while (j < b.size() && b[j] == '0') ++j;
            const size_t runA = i;
            const size_t runB = j;
            while (i < a.size() && isDigit(a[i])) ++i;
            while (j < b.size() && isDigit(b[j])) ++j;
            const size_t lenA = i - runA;
            const size_t lenB = j - runB;
            if (lenA != lenB) return lenA < lenB ? -1 : 1;
            const int cmp = a.substr(runA, lenA).compare(b.substr(runB, lenB));
            if (cmp != 0) return cmp < 0 ? -1 : 1;
            continue;
        }
        const char ca = foldCase(a[i]);
        const char cb = foldCase(b[j]);
        if (ca != cb) return static_cast<unsigned char>(ca) < static_cast<unsigned char>(cb) ? -1 : 1;
        ++i;
        ++j;
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

std::vector<std::pair<std::string, std::string>> parseXdgUserDirs(const std::string& content,
                                                                  const std::string& homeDir) {
    std::vector<std::pair<std::string, std::string>> dirs;
    std::istringstream lines(content);
    std::string line;
    while (std::getline(lines, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));

        static constexpr std::string_view kPrefix = "XDG_";
        static constexpr std::string_view kSuffix = "_DIR";
        if (key.size() <= kPrefix.size() + kSuffix.size() || key.compare(0, kPrefix.size(), kPrefix) != 0 ||
            key.compare(key.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0) {
            continue;
        }
        key = key.substr(kPrefix.size(), key.size() - kPrefix.size() - kSuffix.size());

        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        static constexpr std::string_view kHomeVar = "$HOME";
        if (value.compare(0, kHomeVar.size(), kHomeVar) == 0) {
            value.replace(0, kHomeVar.size(), homeDir);
        }
        // The spec only allows $HOME-relative or absolute paths.
        if (value.empty() || !std::filesystem::path(value).is_absolute()) continue;

        dirs.emplace_back(std::move(key), std::move(value));
    }
    return dirs;
}

std::vector<SystemPlace> discoverSystemPlaces(const std::string& homeDir, const std::string& xdgUserDirs) {
    std::vector<SystemPlace> places;
    if (homeDir.empty()) return places;

    const std::filesystem::path home(homeDir);
    const auto xdg = parseXdgUserDirs(xdgUserDirs, homeDir);
    const auto xdgPath = [&](const char* key, const char* fallbackName) -> std::filesystem::path {
        for (const auto& [k, v] : xdg) {
            if (k == key) return std::filesystem::path(v);
        }
        return home / fallbackName;
    };

    const std::vector<std::pair<std::string, std::filesystem::path>> candidates = {
        {"Home", home},
        {"Desktop", xdgPath("DESKTOP", "Desktop")},
        {"Downloads", xdgPath("DOWNLOAD", "Downloads")},
        {"Documents", xdgPath("DOCUMENTS", "Documents")},
        {"Music", xdgPath("MUSIC", "Music")},
    };

    const std::string homeKey = compareKey(home);
    std::vector<std::string> seen;
    for (const auto& [label, path] : candidates) {
        std::error_code ec;
        if (!std::filesystem::is_directory(path, ec) || ec) continue;
        const std::string key = compareKey(path);
        // XDG disables a folder by pointing it at $HOME; don't list Home twice.
        if (label != "Home" && key == homeKey) continue;
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        seen.push_back(key);
        places.push_back({label, path.string()});
    }
    return places;
}

std::vector<SystemPlace> discoverSystemPlaces() {
#if defined(_WIN32)
    const char* home = std::getenv("USERPROFILE");
    return discoverSystemPlaces(home ? home : "", "");
#else
    const char* home = std::getenv("HOME");
    if (!home || !*home) return {};

    std::string userDirs;
#if !defined(__APPLE__)
    std::filesystem::path configHome;
    if (const char* xdgConfig = std::getenv("XDG_CONFIG_HOME"); xdgConfig && *xdgConfig) {
        configHome = xdgConfig;
    } else {
        configHome = std::filesystem::path(home) / ".config";
    }
    userDirs = readFile(configHome / "user-dirs.dirs");
#endif
    return discoverSystemPlaces(home, userDirs);
#endif
}

} // namespace AestraUI::BrowserLibrary
