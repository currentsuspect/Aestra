#include "UnitManager.h"

#include "Plugin/PluginManager.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace Aestra {
namespace Audio {
namespace {
std::string unitGroupName(UnitGroup group) {
    switch (group) {
    case UnitGroup::Synth: return "Synth";
    case UnitGroup::Drums: return "Drums";
    case UnitGroup::Audio: return "Audio";
    default: return "";
    }
}

UnitGroup unitGroupFromJson(const JSON& groupJson) {
    if (groupJson.isObject()) {
        if (groupJson.has("id")) {
            return static_cast<UnitGroup>(groupJson["id"].asInt());
        }
        if (groupJson.has("name")) {
            const std::string name = groupJson["name"].asString();
            if (name == "Synth") return UnitGroup::Synth;
            if (name == "Drums") return UnitGroup::Drums;
            if (name == "Audio") return UnitGroup::Audio;
        }
    } else if (groupJson.isNumber()) {
        return static_cast<UnitGroup>(groupJson.asInt());
    } else if (groupJson.isString()) {
        const std::string name = groupJson.asString();
        if (name == "Synth") return UnitGroup::Synth;
        if (name == "Drums") return UnitGroup::Drums;
        if (name == "Audio") return UnitGroup::Audio;
    }
    return UnitGroup::Unknown;
}

std::string bytesToHex(const std::vector<uint8_t>& bytes) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (uint8_t byte : bytes) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    if (hex.size() % 2 != 0) {
        return bytes;
    }

    bytes.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        const char hi = static_cast<char>(std::tolower(static_cast<unsigned char>(hex[i])));
        const char lo = static_cast<char>(std::tolower(static_cast<unsigned char>(hex[i + 1])));
        auto hexValue = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
            return -1;
        };
        const int high = hexValue(hi);
        const int low = hexValue(lo);
        if (high < 0 || low < 0) {
            return {};
        }
        bytes.push_back(static_cast<uint8_t>((high << 4) | low));
    }
    return bytes;
}
} // namespace

std::shared_ptr<const AudioArsenalSnapshot> UnitManager::getAudioSnapshot() const {
    auto snapshot = std::make_shared<AudioArsenalSnapshot>();
    snapshot->units.reserve(m_unitOrder.size());

    for (UnitID id : m_unitOrder) {
        const auto* unit = getUnit(id);
        if (!unit) {
            continue;
        }

        UnitState state{};
        state.id = static_cast<int>(unit->id);
        state.enabled = unit->enabled || unit->isEnabled;
        state.plugin = unit->plugin;
        state.routeId = unit->targetMixerRoute;
        snapshot->units.push_back(std::move(state));
    }

    return snapshot;
}

UnitInfo* UnitManager::getUnit(UnitID id) {
    auto it = m_units.find(id);
    return it != m_units.end() ? &it->second : nullptr;
}

const UnitInfo* UnitManager::getUnit(UnitID id) const {
    auto it = m_units.find(id);
    return it != m_units.end() ? &it->second : nullptr;
}

UnitID UnitManager::createUnit() {
    return createUnit("", UnitGroup::Unknown);
}

UnitID UnitManager::createUnit(const std::string& name, UnitGroup group) {
    UnitID id = nextId++;
    UnitInfo info;
    info.id = id;
    info.name = name;
    info.group = std::move(group);
    m_units[id] = std::move(info);
    m_unitOrder.push_back(id);
    return id;
}

void UnitManager::reorderUnit(UnitID id, size_t newIndex) {
    auto it = std::find(m_unitOrder.begin(), m_unitOrder.end(), id);
    if (it == m_unitOrder.end()) {
        return;
    }
    const size_t oldIndex = static_cast<size_t>(std::distance(m_unitOrder.begin(), it));
    if (oldIndex == newIndex) {
        return;
    }
    UnitID value = *it;
    m_unitOrder.erase(it);
    if (newIndex > m_unitOrder.size()) {
        newIndex = m_unitOrder.size();
    }
    m_unitOrder.insert(m_unitOrder.begin() + static_cast<std::ptrdiff_t>(newIndex), value);
}

void UnitManager::clear() {
    m_units.clear();
    m_unitOrder.clear();
    nextId = 1;
}

void UnitManager::setUnitName(UnitID id, const std::string& name) { if (auto* u = getUnit(id)) u->name = name; }
void UnitManager::setUnitMute(UnitID id, bool muted) { if (auto* u = getUnit(id)) u->isMuted = muted; }
void UnitManager::setUnitSolo(UnitID id, bool solo) { if (auto* u = getUnit(id)) u->isSolo = solo; }
void UnitManager::setUnitArmed(UnitID id, bool armed) { if (auto* u = getUnit(id)) u->isArmed = armed; }
void UnitManager::setUnitEnabled(UnitID id, bool enabled) {
    if (auto* u = getUnit(id)) {
        u->isEnabled = enabled;
        u->enabled = enabled;
        if (u->plugin) {
            if (enabled) {
                if (!u->plugin->isActive()) {
                    u->plugin->activate();
                }
            } else {
                if (u->plugin->isActive()) {
                    u->plugin->deactivate();
                }
            }
        }
    }
}
void UnitManager::setUnitMixerChannel(UnitID id, int channel) { if (auto* u = getUnit(id)) u->targetMixerRoute = channel; }
void UnitManager::setUnitAudioClip(UnitID id, const std::string& path) { if (auto* u = getUnit(id)) u->audioClipPath = path; }
void UnitManager::setUnitColor(UnitID id, uint32_t color) { if (auto* u = getUnit(id)) u->color = color; }
void UnitManager::setUnitGroup(UnitID id, UnitGroup group) { if (auto* u = getUnit(id)) u->group = std::move(group); }

void UnitManager::attachPlugin(UnitID id, const std::string& pluginId, std::shared_ptr<IPluginInstance> plugin) {
    if (auto* u = getUnit(id)) {
        u->pluginId = pluginId;
        u->plugin = std::move(plugin);
        if (u->plugin) {
            if ((u->enabled || u->isEnabled) && !u->plugin->isActive()) {
                u->plugin->activate();
            }
            u->pluginState = u->plugin->saveState();
        } else {
            u->pluginState.clear();
        }
    }
}

void UnitManager::setUnitPluginState(UnitID id, const std::vector<uint8_t>& state) {
    if (auto* u = getUnit(id)) {
        u->pluginState = state;
        if (u->plugin && !state.empty()) {
            u->plugin->loadState(state);
        }
    }
}

void UnitManager::captureUnitPluginState(UnitID id) {
    if (auto* u = getUnit(id); u && u->plugin) {
        u->pluginState = u->plugin->saveState();
    }
}

std::shared_ptr<IPluginInstance> UnitManager::getUnitPlugin(UnitID id) const {
    const auto* u = getUnit(id);
    return u ? u->plugin : nullptr;
}

std::string UnitManager::getUnitPluginId(UnitID id) const {
    const auto* u = getUnit(id);
    return u ? u->pluginId : std::string{};
}

JSON UnitManager::saveToJSON() const {
    JSON root = JSON::object();
    root.set("nextId", JSON(static_cast<double>(nextId)));

    JSON units = JSON::array();
    for (UnitID id : m_unitOrder) {
        const auto* unit = getUnit(id);
        if (!unit) continue;

        JSON u = JSON::object();
        u.set("id", JSON(static_cast<double>(unit->id)));
        u.set("name", JSON(unit->name));
        u.set("enabled", JSON(unit->enabled || unit->isEnabled));
        u.set("targetMixerRoute", JSON(static_cast<double>(unit->targetMixerRoute)));
        u.set("color", JSON(std::to_string(unit->color)));
        u.set("muted", JSON(unit->isMuted));
        u.set("solo", JSON(unit->isSolo));
        u.set("armed", JSON(unit->isArmed));
        u.set("audioClipPath", JSON(unit->audioClipPath));

        JSON group = JSON::object();
        group.set("id", JSON(static_cast<double>(static_cast<uint32_t>(unit->group))));
        group.set("name", JSON(unitGroupName(unit->group)));
        u.set("group", group);

        if (!unit->pluginId.empty()) {
            u.set("pluginId", JSON(unit->pluginId));
        }

        std::vector<uint8_t> state = unit->pluginState;
        if (unit->plugin) {
            state = unit->plugin->saveState();
        }
        if (!state.empty()) {
            u.set("pluginStateHex", JSON(bytesToHex(state)));
        }

        units.push(u);
    }

    root.set("units", units);
    return root;
}

void UnitManager::loadFromJSON(const JSON& json) {
    clear();
    if (!json.isObject()) {
        return;
    }

    if (json.has("nextId")) {
        nextId = static_cast<UnitID>(json["nextId"].asNumber());
        if (nextId == 0) nextId = 1;
    }

    if (!json.has("units") || !json["units"].isArray()) {
        return;
    }

    auto& pluginManager = PluginManager::getInstance();
    const JSON& units = json["units"];
    UnitID maxSeenId = 0;

    for (size_t i = 0; i < units.size(); ++i) {
        const JSON& ju = units[i];
        if (!ju.isObject() || !ju.has("id")) {
            continue;
        }

        UnitInfo unit;
        unit.id = static_cast<UnitID>(ju["id"].asNumber());
        unit.name = ju.has("name") ? ju["name"].asString() : std::string{};
        unit.enabled = ju.has("enabled") ? ju["enabled"].asBool() : false;
        unit.isEnabled = unit.enabled;
        unit.targetMixerRoute = ju.has("targetMixerRoute") ? ju["targetMixerRoute"].asInt() : -1;
        if (ju.has("color")) {
            unit.color = ju["color"].isString() ? static_cast<uint32_t>(std::stoul(ju["color"].asString()))
                                                 : static_cast<uint32_t>(ju["color"].asNumber());
        }
        unit.isMuted = ju.has("muted") ? ju["muted"].asBool() : false;
        unit.isSolo = ju.has("solo") ? ju["solo"].asBool() : false;
        unit.isArmed = ju.has("armed") ? ju["armed"].asBool() : false;
        unit.audioClipPath = ju.has("audioClipPath") ? ju["audioClipPath"].asString() : std::string{};

        if (ju.has("group")) {
            unit.group = unitGroupFromJson(ju["group"]);
        }

        unit.pluginId = ju.has("pluginId") ? ju["pluginId"].asString() : std::string{};
        if (ju.has("pluginStateHex") && ju["pluginStateHex"].isString()) {
            unit.pluginState = hexToBytes(ju["pluginStateHex"].asString());
        }

        if (!unit.pluginId.empty()) {
            unit.plugin = pluginManager.createInstanceById(unit.pluginId);
            if (unit.plugin) {
                unit.plugin->initialize(48000.0, 512);
                if (unit.enabled || unit.isEnabled) {
                    unit.plugin->activate();
                }
                if (!unit.pluginState.empty()) {
                    unit.plugin->loadState(unit.pluginState);
                }
            }
        }

        maxSeenId = std::max(maxSeenId, unit.id);
        m_unitOrder.push_back(unit.id);
        m_units[unit.id] = std::move(unit);
    }

    if (nextId <= maxSeenId) {
        nextId = maxSeenId + 1;
    }
}

} // namespace Audio
} // namespace Aestra
