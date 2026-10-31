// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Presets/Preset.h"

#include "AestraJSON.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace Aestra {
namespace Audio {
namespace Presets {

namespace {

constexpr float kSameValue = 1.0e-4f; // a knob that moved less than this has not changed

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string toBase64(const std::vector<uint8_t>& bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const uint32_t b0 = bytes[i];
        const uint32_t b1 = i + 1 < bytes.size() ? bytes[i + 1] : 0;
        const uint32_t b2 = i + 2 < bytes.size() ? bytes[i + 2] : 0;
        const uint32_t n = (b0 << 16) | (b1 << 8) | b2;
        out += kB64[(n >> 18) & 63];
        out += kB64[(n >> 12) & 63];
        out += i + 1 < bytes.size() ? kB64[(n >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? kB64[n & 63] : '=';
    }
    return out;
}

std::optional<std::vector<uint8_t>> fromBase64(const std::string& text) {
    auto decode = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    if (text.size() % 4 != 0) return std::nullopt;
    std::vector<uint8_t> out;
    out.reserve(text.size() / 4 * 3);
    for (size_t i = 0; i < text.size(); i += 4) {
        int v[4];
        int pad = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = text[i + k];
            if (c == '=' && i + 4 == text.size() && k >= 2) {
                v[k] = 0;
                ++pad;
                continue;
            }
            if (pad > 0) return std::nullopt; // data after padding
            v[k] = decode(c);
            if (v[k] < 0) return std::nullopt;
        }
        const uint32_t n = (uint32_t(v[0]) << 18) | (uint32_t(v[1]) << 12) | (uint32_t(v[2]) << 6) | uint32_t(v[3]);
        out.push_back(uint8_t(n >> 16));
        if (pad < 2) out.push_back(uint8_t((n >> 8) & 0xFF));
        if (pad < 1) out.push_back(uint8_t(n & 0xFF));
    }
    return out;
}

bool presetWritable(const PluginParameter& p) { return !p.isBypass && !p.isReadOnly; }

/// The plugin parameter a preset param means: same id and name first, then the name alone (an id renumbered
/// between versions), then the id alone (a parameter renamed).
const PluginParameter* findParam(const std::vector<PluginParameter>& params, const PresetParam& wanted) {
    for (const auto& p : params)
        if (p.id == wanted.id && p.name == wanted.name) return &p;
    if (!wanted.name.empty())
        for (const auto& p : params)
            if (p.name == wanted.name) return &p;
    if (wanted.id == PresetParam::kNoId) return nullptr;
    for (const auto& p : params)
        if (p.id == wanted.id) return &p;
    return nullptr;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

const char* presetSourceName(PresetSource source) {
    switch (source) {
    case PresetSource::Factory: return "Factory";
    case PresetSource::User: return "Yours";
    case PresetSource::Project: return "This project";
    }
    return "";
}

std::string toJson(const Preset& preset) {
    JSON root = JSON::object();
    root.set("aestraPreset", JSON(static_cast<double>(kPresetFormatVersion)));
    root.set("name", JSON(preset.name));
    JSON plugin = JSON::object();
    plugin.set("id", JSON(preset.pluginId));
    plugin.set("name", JSON(preset.pluginName));
    root.set("plugin", plugin);
    if (!preset.category.empty()) root.set("category", JSON(preset.category));
    if (!preset.description.empty()) root.set("description", JSON(preset.description));
    JSON tags = JSON::array();
    for (const auto& t : preset.tags) tags.push(JSON(t));
    root.set("tags", tags);
    JSON params = JSON::array();
    for (const auto& p : preset.params) {
        JSON row = JSON::object();
        if (p.id != PresetParam::kNoId) row.set("id", JSON(static_cast<double>(p.id)));
        row.set("name", JSON(p.name));
        row.set("value", JSON(static_cast<double>(p.value)));
        params.push(row);
    }
    root.set("params", params);
    if (!preset.state.empty()) root.set("state", JSON(toBase64(preset.state)));
    return root.toString(2);
}

std::optional<Preset> fromJson(const std::string& text) {
    bool consumedAll = false;
    const JSON root = JSON::parseStrict(text, consumedAll);
    if (!consumedAll || !root.isObject() || !root.has("aestraPreset") || !root["aestraPreset"].isNumber())
        return std::nullopt;
    const double version = root["aestraPreset"].asNumber();
    if (version < 1 || version > kPresetFormatVersion) return std::nullopt;

    Preset p;
    if (root.has("name") && root["name"].isString()) p.name = trim(root["name"].asString());
    if (root.has("plugin") && root["plugin"].isObject()) {
        const JSON& plugin = root["plugin"];
        if (plugin.has("id") && plugin["id"].isString()) p.pluginId = plugin["id"].asString();
        if (plugin.has("name") && plugin["name"].isString()) p.pluginName = plugin["name"].asString();
    }
    if (p.name.empty() || p.pluginId.empty()) return std::nullopt;
    if (root.has("category") && root["category"].isString()) p.category = root["category"].asString();
    if (root.has("description") && root["description"].isString()) p.description = root["description"].asString();
    if (root.has("tags") && root["tags"].isArray())
        for (const auto& t : root["tags"].asArray())
            if (t.isString() && !t.asString().empty()) p.tags.push_back(t.asString());
    if (root.has("params") && root["params"].isArray()) {
        for (const auto& row : root["params"].asArray()) {
            if (!row.isObject() || !row.has("value") || !row["value"].isNumber()) continue;
            PresetParam pp;
            if (row.has("id") && row["id"].isNumber() && row["id"].asNumber() >= 0 &&
                row["id"].asNumber() < static_cast<double>(PresetParam::kNoId))
                pp.id = static_cast<uint32_t>(row["id"].asNumber());
            if (row.has("name") && row["name"].isString()) pp.name = row["name"].asString();
            const double v = row["value"].asNumber();
            if (!std::isfinite(v)) continue;
            pp.value = static_cast<float>(v);
            p.params.push_back(std::move(pp));
        }
    }
    if (root.has("state") && root["state"].isString()) {
        auto bytes = fromBase64(root["state"].asString());
        if (!bytes) return std::nullopt; // a damaged state is a damaged preset, not an empty one
        p.state = std::move(*bytes);
    }
    if (p.params.empty() && p.state.empty()) return std::nullopt;
    return p;
}

Preset capture(const IPluginInstance& plugin, const std::string& name) {
    Preset p;
    p.name = trim(name);
    p.pluginId = plugin.getInfo().id;
    p.pluginName = plugin.getInfo().name;
    for (const auto& param : plugin.getParameters()) {
        if (!presetWritable(param)) continue;
        p.params.push_back({param.id, param.name, plugin.getParameter(param.id)});
    }
    // Aestra's own plugins are fully described by their parameters; anything else keeps its own state too.
    if (plugin.getInfo().format != PluginFormat::Internal || p.params.empty()) p.state = plugin.saveState();
    return p;
}

bool apply(IPluginInstance& plugin, const Preset& preset) {
    if (preset.pluginId != plugin.getInfo().id) return false;
    bool applied = false;
    const auto params = plugin.getParameters();

    // A state blob carries bypass too; keep the plugin's own on/off across the load.
    const PluginParameter* bypass = nullptr;
    for (const auto& p : params)
        if (p.isBypass) bypass = &p;
    const float bypassBefore = bypass ? plugin.getParameter(bypass->id) : 0.0f;

    if (!preset.state.empty() && plugin.loadState(preset.state)) {
        applied = true;
        if (bypass) plugin.setParameter(bypass->id, bypassBefore);
    }
    for (const auto& want : preset.params) {
        const PluginParameter* p = findParam(params, want);
        if (!p || !presetWritable(*p)) continue;
        plugin.setParameter(p->id, std::clamp(want.value, p->minValue, p->maxValue));
        applied = true;
    }
    return applied;
}

int countChanges(const IPluginInstance& plugin, const Preset& preset) {
    if (preset.params.empty()) return preset.state.empty() || plugin.saveState() == preset.state ? 0 : 1;
    const auto params = plugin.getParameters();
    int changed = 0;
    for (const auto& want : preset.params) {
        const PluginParameter* p = findParam(params, want);
        if (!p || !presetWritable(*p)) continue;
        const float target = std::clamp(want.value, p->minValue, p->maxValue);
        if (std::fabs(plugin.getParameter(p->id) - target) > kSameValue) ++changed;
    }
    return changed;
}

std::string suggestName(const std::string& trackName, const std::string& startedFrom,
                        const std::vector<std::string>& taken) {
    const std::string track = trim(trackName);
    const std::string base = trim(startedFrom);
    std::string name;
    if (track.empty()) {
        name = base.empty() ? "My Preset" : base;
    } else if (base.empty()) {
        name = track;
    } else {
        // Keep the track's words and add the preset's words the track does not already say:
        // "Lead Vox" + "Vocal Plate" -> "Lead Vox Plate" (vocal/vox mean the same thing here).
        auto said = [&](const std::string& w) {
            const std::string lw = lower(w);
            for (const auto& t : words(track)) {
                const std::string lt = lower(t);
                if (lt == lw) return true;
                const bool vocalWord = lw == "vocal" || lw == "vocals" || lw == "vox";
                const bool vocalTrack = lt == "vocal" || lt == "vocals" || lt == "vox";
                if (vocalWord && vocalTrack) return true;
            }
            return false;
        };
        name = track;
        for (const auto& w : words(base))
            if (!said(w)) name += " " + w;
    }
    auto isTaken = [&](const std::string& n) {
        const std::string ln = lower(n);
        return std::any_of(taken.begin(), taken.end(), [&](const std::string& t) { return lower(trim(t)) == ln; });
    };
    if (!isTaken(name)) return name;
    for (int i = 2;; ++i) {
        const std::string candidate = name + " " + std::to_string(i);
        if (!isTaken(candidate)) return candidate;
    }
}

std::string safeFileStem(const std::string& name) {
    std::string out;
    for (char c : trim(name)) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 32 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|')
            out += '_';
        else
            out += c;
    }
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    while (!out.empty() && out.front() == '.') out.erase(out.begin());
    if (out.empty()) out = "Preset";
    if (out.size() > 120) out.resize(120);
    return out;
}

} // namespace Presets
} // namespace Audio
} // namespace Aestra
