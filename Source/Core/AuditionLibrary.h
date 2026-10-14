// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "TakeManager.h"
#include "VersionBounce.h"
#include "VersionTime.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace Aestra {

/**
 * @brief What Audition lists to listen to (FD-26, V81-A): the project's own bounces, newest first, and the
 * reference tracks the user has imported. Pure: manifests and files in, items out, so it is tested without a window.
 */
namespace AuditionLibrary {

struct Item {
    enum class Kind { Version, Reference, Stem };
    Kind kind = Kind::Version;
    std::string id;        ///< stable: "v:<version id>" or "r:<path>"
    std::string title;
    std::string subtitle;
    std::string badge;     ///< "v5", "REF"
    std::string path;      ///< the audio file
    double seconds = 0.0;
    double lufs = 0.0;
    bool hasLufs = false;
    uint32_t seed = 0;     ///< picks the generated cover
};

/// FNV-1a: the same name always gets the same cover.
inline uint32_t seedOf(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    return h;
}

/// The project's bounces, newest first.
inline std::vector<Item> versionItems(const std::string& projectName, const std::string& projectPath,
                                      const TakeManager::Manifest& manifest, uint64_t nowMs) {
    std::vector<Item> out;
    if (!manifest.ok) return out;
    // Only versions that were bounced are listed, and they are numbered among themselves, oldest = v1.
    struct Bounced { const TakeManager::TakeEntry* version; std::string wav; VersionBounceInfo info; };
    std::vector<Bounced> bounced;
    for (const auto& v : manifest.takes) {
        if (v.kind == TakeManager::TakeEntry::Kind::Autosave) continue;
        const std::string wav = TakeManager::resolveBouncePath(projectPath, v);
        VersionBounceInfo info;
        if (wav.empty() || !std::filesystem::exists(wav) || !VersionBounce::readInfo(wav, info)) continue;
        bounced.push_back({&v, wav, info});
    }
    std::stable_sort(bounced.begin(), bounced.end(), [](const Bounced& a, const Bounced& b) { return a.version->createdAtMs < b.version->createdAtMs; });
    for (size_t i = bounced.size(); i-- > 0;) {
        const auto& v = *bounced[i].version;
        Item item;
        item.kind = Item::Kind::Version;
        item.id = "v:" + v.id;
        item.title = projectName;
        item.badge = "v" + std::to_string(i + 1);
        item.subtitle = (v.name.empty() ? v.id : v.name) + " \xc2\xb7 " + VersionTime::ago(v.updatedAtMs ? v.updatedAtMs : v.createdAtMs, nowMs);
        item.path = bounced[i].wav;
        item.seconds = bounced[i].info.seconds;
        item.lufs = bounced[i].info.lufs;
        item.hasLufs = bounced[i].info.lufs > -69.0;
        item.seed = seedOf(projectName);  // a project's versions share a cover, as in the concept
        out.push_back(std::move(item));
    }
    return out;
}

struct Reference {
    std::string path;
    std::string title;
    double seconds = 0.0;
    double lufs = 0.0;
    bool hasLufs = false;
    uint64_t addedMs = 0;
};

/// What an item is called when it stands beside another: a reference by its title, a version by its number and name.
inline std::string displayName(const Item& item) {
    if (item.kind == Item::Kind::Reference) return item.title;
    const size_t dot = item.subtitle.find(" \xc2\xb7 ");
    return item.badge + " " + (dot == std::string::npos ? item.subtitle : item.subtitle.substr(0, dot));
}

inline Item itemOf(const Reference& r) {
    Item item;
    item.kind = Item::Kind::Reference;
    item.id = "r:" + r.path;
    item.title = r.title;
    item.badge = "REF";
    item.path = r.path;
    item.seconds = r.seconds;
    item.lufs = r.lufs;
    item.hasLufs = r.hasLufs;
    item.seed = seedOf(r.title);
    char buf[64];
    const int mins = static_cast<int>(r.seconds) / 60, secs = static_cast<int>(r.seconds) % 60;
    if (r.hasLufs) std::snprintf(buf, sizeof buf, "%.1f LUFS \xc2\xb7 %d:%02d", r.lufs, mins, secs);
    else std::snprintf(buf, sizeof buf, "%d:%02d", mins, secs);
    item.subtitle = buf;
    return item;
}

/// One reference per line: path, title, seconds, LUFS (or "-"), added time, tab separated. Tabs in names become spaces.
inline bool save(const std::string& file, const std::vector<Reference>& refs) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(file).parent_path(), ec);
    std::ofstream out(file, std::ios::trunc);
    if (!out) return false;
    const auto clean = [](std::string s) { std::replace(s.begin(), s.end(), '\t', ' '); std::replace(s.begin(), s.end(), '\n', ' '); return s; };
    for (const auto& r : refs)
        out << clean(r.path) << '\t' << clean(r.title) << '\t' << r.seconds << '\t' << (r.hasLufs ? std::to_string(r.lufs) : "-") << '\t' << r.addedMs << '\n';
    return static_cast<bool>(out);
}

inline std::vector<Reference> load(const std::string& file) {
    std::vector<Reference> refs;
    std::ifstream in(file);
    for (std::string line; std::getline(in, line);) {
        std::vector<std::string> f;
        std::stringstream ss(line);
        for (std::string cell; std::getline(ss, cell, '\t');) f.push_back(cell);
        if (f.size() < 5 || f[0].empty()) continue;
        Reference r;
        r.path = f[0];
        r.title = f[1];
        try {
            r.seconds = std::stod(f[2]);
            if (f[3] != "-") { r.lufs = std::stod(f[3]); r.hasLufs = true; }
            r.addedMs = static_cast<uint64_t>(std::stoull(f[4]));
        } catch (...) { continue; }
        refs.push_back(std::move(r));
    }
    return refs;
}

} // namespace AuditionLibrary
} // namespace Aestra
