// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Presets/LegacyPresets.h"

#include "AestraJSON.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace Aestra {
namespace Audio {
namespace Presets {

namespace fs = std::filesystem;

namespace {
constexpr const char* kImportedMarker = ".imported-aeverb";
} // namespace

fs::path legacyVerbPresetDir() {
    const char* home = std::getenv("HOME");
#ifdef _WIN32
    if (!home || !*home) home = std::getenv("USERPROFILE");
#endif
    return fs::path(home && *home ? home : ".") / "Documents" / "Aestra" / "presets" / "AestraVerb";
}

std::optional<Preset> presetFromLegacyVerb(const std::string& text, const std::string& fallbackName) {
    const JSON j = JSON::parse(text);
    if (!j.isObject()) return std::nullopt;
    auto num = [&](const char* key, double fallback) {
        return j.has(key) && j[key].isNumber() ? j[key].asNumber() : fallback;
    };
    Preset p;
    p.name = j.has("name") && j["name"].isString() && !j["name"].asString().empty() ? j["name"].asString()
                                                                                     : fallbackName;
    p.pluginId = kVerbPluginId;
    p.pluginName = "Aestra Verb";
    p.source = PresetSource::User;
    // Indices were stored as whole numbers; the parameters are normalized steps (9 modes, 7 pre-delay syncs,
    // 3 mod characters). modDepth falls back to the old 0.14 default so a file saved before it existed sounds as it did.
    auto add = [&](const char* name, double v) {
        p.params.push_back({PresetParam::kNoId, name, static_cast<float>(v < 0 ? 0 : (v > 1 ? 1 : v))});
    };
    add("Mode", num("mode", 0) / 8.0);
    add("Size", num("size", 0.5));
    add("Decay", num("decay", 0.5));
    add("Damping", num("damping", 0.5));
    add("Diffusion", num("diffusion", 0.7));
    add("Mod Rate", num("modRate", 0.4));
    add("Mod Depth", num("modDepth", 0.14));
    add("Width", num("width", 0.68));
    add("Mix", num("mix", 0.36));
    add("Attack", num("attack", 0.0));
    add("Shape", num("shape", 0.5));
    add("Pre Sync", num("predelaySync", 0) / 6.0);
    add("Mod Char", num("modCharacter", 0) / 2.0);
    return p;
}

int importLegacyVerbPresets(const PresetLibrary& library, const fs::path& legacyDir) {
    std::error_code ec;
    const fs::path marker = library.userRoot() / kImportedMarker;
    if (fs::exists(marker, ec) || !fs::is_directory(legacyDir, ec)) return 0;
    int imported = 0;
    for (fs::directory_iterator it(legacyDir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != ".aeverb") continue;
        std::ifstream in(it->path(), std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        auto preset = presetFromLegacyVerb(ss.str(), it->path().stem().string());
        if (preset && library.saveUser(*preset, false)) ++imported; // a same-named one already there wins
    }
    fs::create_directories(library.userRoot(), ec);
    std::ofstream(marker) << "Aestra Verb presets from " << legacyDir.string() << " were copied here.\n";
    return imported;
}

} // namespace Presets
} // namespace Audio
} // namespace Aestra
