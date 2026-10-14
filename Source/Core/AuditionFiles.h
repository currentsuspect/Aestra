// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace Aestra {

/**
 * @brief The files Audition lists (FD-26, V81-A): folders to move through and audio to listen to, and nothing else.
 * Plugins, projects, presets and the rest are not things you can listen to, so they are not here. Pure filesystem
 * reads, so it is tested against a temporary folder.
 */
namespace AuditionFiles {

struct Entry {
    std::string name;
    std::string path;
    bool folder = false;
    uint64_t bytes = 0;
};

struct Place {
    std::string name;
    std::string path;
};

inline std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

inline bool isAudio(const std::filesystem::path& p) {
    static const char* kExt[] = {".wav", ".flac", ".mp3", ".ogg", ".oga", ".opus", ".aif", ".aiff", ".aifc", ".m4a", ".aac"};
    const std::string ext = lower(p.extension().string());
    return std::any_of(std::begin(kExt), std::end(kExt), [&](const char* e) { return ext == e; });
}

/// Folders first, then audio files, each in name order (case-insensitive). Hidden entries and everything that is not
/// audio are left out; an unreadable folder lists as empty.
inline std::vector<Entry> list(const std::string& dir) {
    namespace fs = std::filesystem;
    std::vector<Entry> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        std::error_code e2;
        if (it->is_directory(e2)) out.push_back({name, it->path().string(), true, 0});
        else if (it->is_regular_file(e2) && isAudio(it->path())) out.push_back({name, it->path().string(), false, static_cast<uint64_t>(it->file_size(e2))});
    }
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        if (a.folder != b.folder) return a.folder;
        return lower(a.name) < lower(b.name);
    });
    return out;
}

/// The folders worth jumping to, the ones that exist: the project's own, then the usual places for music.
inline std::vector<Place> places(const std::string& projectDir, const std::string& home) {
    namespace fs = std::filesystem;
    std::vector<Place> out;
    const auto add = [&](const std::string& name, const fs::path& p) {
        std::error_code ec;
        if (!p.empty() && fs::is_directory(p, ec) && std::none_of(out.begin(), out.end(), [&](const Place& e) { return e.path == p.string(); })) out.push_back({name, p.string()});
    };
    if (!projectDir.empty()) add("Project", projectDir);
    if (!home.empty()) {
        add("Music", fs::path(home) / "Music");
        add("Downloads", fs::path(home) / "Downloads");
        add("Documents", fs::path(home) / "Documents");
        add("Desktop", fs::path(home) / "Desktop");
        add("Home", home);
    }
    return out;
}

inline std::string sizeLabel(uint64_t bytes) {
    char buf[32];
    if (bytes >= 1024ull * 1024ull) std::snprintf(buf, sizeof buf, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    else std::snprintf(buf, sizeof buf, "%llu KB", static_cast<unsigned long long>((bytes + 1023) / 1024));
    return buf;
}

} // namespace AuditionFiles
} // namespace Aestra
