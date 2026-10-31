// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Presets/PresetLibrary.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

namespace Aestra {
namespace Audio {
namespace Presets {

namespace fs = std::filesystem;

/// Defined in FactoryPresets.cpp: fills the table with every built-in plugin's factory set.
void addBuiltInFactoryPresets(std::map<std::string, std::vector<Preset>>& table);

namespace {

std::mutex& factoryMutex() {
    static std::mutex m;
    return m;
}

std::map<std::string, std::vector<Preset>>& factoryTable() {
    static std::map<std::string, std::vector<Preset>> table;
    return table;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> lowerWords(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            cur += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (!cur.empty()) {
            out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

/// "vox", "vocals" and "vocal" are one word to a producer; so are "drums" and "drum", "keys" and "key".
std::string stem(std::string w) {
    if (w == "vox" || w == "vocals" || w == "vocal" || w == "vocalist") return "vocal";
    if (w.size() > 3 && w.back() == 's') w.pop_back();
    return w;
}

std::optional<Preset> readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::stringstream ss;
    ss << in.rdbuf();
    auto preset = fromJson(ss.str());
    if (!preset) return std::nullopt;
    preset->source = PresetSource::User;
    preset->path = path.string();
    return preset;
}

void byName(std::vector<Preset>& v) {
    std::stable_sort(v.begin(), v.end(), [](const Preset& a, const Preset& b) { return lower(a.name) < lower(b.name); });
}

} // namespace

void registerFactoryPresets(const std::string& pluginId, std::vector<Preset> presets) {
    for (auto& p : presets) {
        p.pluginId = pluginId;
        p.source = PresetSource::Factory;
        p.path.clear();
    }
    std::lock_guard<std::mutex> lock(factoryMutex());
    if (factoryTable().empty()) addBuiltInFactoryPresets(factoryTable());
    factoryTable()[pluginId] = std::move(presets);
}

std::vector<Preset> factoryPresets(const std::string& pluginId) {
    std::lock_guard<std::mutex> lock(factoryMutex());
    auto& table = factoryTable();
    if (table.empty()) {
        addBuiltInFactoryPresets(table); // first use: Aestra's own plugins' sets (FactoryPresets.cpp)
    }
    auto it = table.find(pluginId);
    return it == table.end() ? std::vector<Preset>{} : it->second;
}

PresetLibrary::PresetLibrary(fs::path userRoot) : m_root(std::move(userRoot)) {}

fs::path PresetLibrary::defaultUserRoot() {
    const char* home = std::getenv("HOME");
#ifdef _WIN32
    if (!home || !*home) home = std::getenv("USERPROFILE");
#endif
    const fs::path base = home && *home ? fs::path(home) : fs::current_path();
    return base / "Documents" / "Aestra" / "Presets";
}

std::vector<Preset> PresetLibrary::userPresets(const std::string& pluginId) const {
    std::vector<Preset> out;
    std::error_code ec;
    if (!fs::is_directory(m_root, ec)) return out;
    for (fs::recursive_directory_iterator it(m_root, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != kPresetExtension) continue;
        auto preset = readFile(it->path());
        if (preset && preset->pluginId == pluginId) out.push_back(std::move(*preset));
    }
    byName(out);
    return out;
}

std::vector<Preset> PresetLibrary::list(const std::string& pluginId) const {
    std::vector<Preset> out = factoryPresets(pluginId);
    auto mine = userPresets(pluginId);
    out.insert(out.end(), std::make_move_iterator(mine.begin()), std::make_move_iterator(mine.end()));
    return out;
}

fs::path PresetLibrary::pathFor(const Preset& preset) const {
    const std::string folder = safeFileStem(preset.pluginName.empty() ? preset.pluginId : preset.pluginName);
    return m_root / folder / (safeFileStem(preset.name) + kPresetExtension);
}

std::optional<Preset> PresetLibrary::saveUser(Preset preset, bool replace, std::string* error) const {
    auto fail = [&](const char* why) -> std::optional<Preset> {
        if (error) *error = why;
        return std::nullopt;
    };
    if (preset.name.empty()) return fail("A preset needs a name.");
    if (preset.pluginId.empty()) return fail("A preset must belong to a plugin.");

    // The same name for the same plugin is the same preset, wherever its file is.
    fs::path target;
    for (const auto& existing : userPresets(preset.pluginId)) {
        if (lower(existing.name) != lower(preset.name)) continue;
        if (!replace) return fail("You already have a preset with that name.");
        target = existing.path;
    }
    if (target.empty()) {
        target = pathFor(preset);
        std::error_code ec;
        // Two names can share a file stem ("a/b" and "a_b"); never write over a file that is not this preset.
        for (int i = 2; fs::exists(target, ec); ++i)
            target = target.parent_path() / (safeFileStem(preset.name) + " " + std::to_string(i) + kPresetExtension);
    }

    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    if (ec) return fail("Aestra could not make the presets folder.");
    preset.source = PresetSource::User;
    const fs::path tmp = target.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return fail("Aestra could not write the preset file.");
        out << toJson(preset);
        if (!out.flush()) return fail("Aestra could not write the preset file.");
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return fail("Aestra could not write the preset file.");
    }
    preset.path = target.string();
    return preset;
}

bool PresetLibrary::removeUser(const Preset& preset) const {
    if (preset.source != PresetSource::User || preset.path.empty()) return false;
    std::error_code ec;
    const fs::path path(preset.path);
    // Only a file inside this library, and only a preset file.
    const auto rel = fs::relative(path, m_root, ec);
    if (ec || rel.empty() || *rel.begin() == "..") return false;
    if (path.extension() != kPresetExtension) return false;
    return fs::remove(path, ec) && !ec;
}

std::vector<Preset> orderForTrack(std::vector<Preset> presets, const std::string& trackName) {
    std::vector<std::string> trackWords;
    for (const auto& w : lowerWords(trackName)) trackWords.push_back(stem(w));
    if (trackWords.empty()) return presets;
    auto fits = [&](const Preset& p) {
        std::string text = p.name + " " + p.category;
        for (const auto& t : p.tags) text += " " + t;
        for (const auto& w : lowerWords(text))
            if (std::find(trackWords.begin(), trackWords.end(), stem(w)) != trackWords.end()) return true;
        return false;
    };
    std::stable_partition(presets.begin(), presets.end(), fits);
    return presets;
}

} // namespace Presets
} // namespace Audio
} // namespace Aestra
