// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UnitManager.h"
#include <algorithm>

namespace Nomad {
namespace Audio {

UnitManager::UnitManager() {
    m_audioSnapshot = std::make_shared<AudioArsenalSnapshot>();
}

UnitID UnitManager::createUnit(const std::string& name, UnitGroup group) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    ArsenalUnit unit;
    unit.id = m_nextId++;
    unit.name = name;
    unit.group = group;
    unit.color = 0xFF00A8E8; // Default teal
    unit.targetMixerRoute = 0; // Default to mixer channel 0
    unit.runtimeEnabled.store(true, std::memory_order_relaxed);
    
    m_units[unit.id] = unit;
    m_unitOrder.push_back(unit.id);
    
    updateAudioSnapshot();
    return unit.id;
}

void UnitManager::removeUnit(UnitID id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_units.find(id);
    if (it != m_units.end()) {
        m_units.erase(it);
        
        // Remove from order list
        m_unitOrder.erase(
            std::remove(m_unitOrder.begin(), m_unitOrder.end(), id),
            m_unitOrder.end()
        );
        
        updateAudioSnapshot();
    }
}

void UnitManager::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_units.clear();
    m_unitOrder.clear();
    m_nextId = 1;
    updateAudioSnapshot();
}

ArsenalUnit* UnitManager::getUnit(UnitID id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_units.find(id);
    if (it != m_units.end()) {
        return &it->second;
    }
    return nullptr;
}

void UnitManager::setUnitEnabled(UnitID id, bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_units.count(id)) {
        m_units[id].isEnabled = enabled;
        m_units[id].runtimeEnabled.store(enabled);
        updateAudioSnapshot();
    }
}

void UnitManager::setUnitArmed(UnitID id, bool armed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_units.count(id)) {
        m_units[id].isArmed = armed;
        updateAudioSnapshot();
    }
}

void UnitManager::setUnitSolo(UnitID id, bool solo) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_units.count(id)) {
        m_units[id].isSolo = solo;
        updateAudioSnapshot();
    }
}

void UnitManager::setUnitMute(UnitID id, bool muted) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_units.count(id)) {
        m_units[id].isMuted = muted;
        updateAudioSnapshot();
    }
}

void UnitManager::setUnitRoute(UnitID id, MixerRouteID route) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_units.count(id)) {
        m_units[id].targetMixerRoute = route;
        updateAudioSnapshot();
    }
}

void UnitManager::setUnitAudioClip(UnitID id, const std::string& clipPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_units.count(id)) {
        m_units[id].audioClipPath = clipPath;
        // No need to update audio snapshot for this (it's UI data)
    }
}

void UnitManager::setUnitMixerChannel(UnitID id, int channelIndex) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_units.count(id)) {
        m_units[id].targetMixerRoute = channelIndex;
        updateAudioSnapshot();
    }
}

void UnitManager::setActivePattern(UnitID id, PatternID pid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_units.count(id)) {
        // Update active pattern
        m_units[id].activePattern = pid;
        
        // Add to associated patterns if not already present
        auto& patterns = m_units[id].associatedPatterns;
        if (std::find(patterns.begin(), patterns.end(), pid) == patterns.end()) {
            patterns.push_back(pid);
        }
        
        // No audio snapshot update needed as active pattern is mainly for UI/Sequencing context,
        // unless the engine needs to know which pattern to play (which is usually handled by the Playlist)
    }
}

std::shared_ptr<const AudioArsenalSnapshot> UnitManager::getAudioSnapshot() const {
    // Atomic load of shared_ptr
    return std::atomic_load(&m_audioSnapshot);
}

void UnitManager::updateAudioSnapshot() {
    // Creates a new snapshot and atomically swaps it in
    // Must be called with lock held
    
    auto newSnapshot = std::make_shared<AudioArsenalSnapshot>();
    newSnapshot->units.reserve(m_unitOrder.size());
    
    for (const auto& id : m_unitOrder) {
        if (m_units.find(id) == m_units.end()) continue;
        
        const auto& unit = m_units.at(id);
        AudioArsenalSnapshot::UnitState state;
        state.id = unit.id;
        state.enabled = unit.runtimeEnabled.load();
        state.armed = unit.isArmed;
        state.solo = unit.isSolo;
        state.muted = unit.isMuted;
        state.routeId = unit.targetMixerRoute;
        
        newSnapshot->units.push_back(state);
    }
    
    std::atomic_store(&m_audioSnapshot, newSnapshot);
}

Nomad::JSON UnitManager::saveToJSON() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    Nomad::JSON json = Nomad::JSON::array();
    
    // Save in order
    for (const auto& id : m_unitOrder) {
        const auto& unit = m_units.at(id);
        Nomad::JSON u = Nomad::JSON::object();
        u.set("id", Nomad::JSON(static_cast<double>(unit.id)));
        u.set("name", Nomad::JSON(unit.name));
        u.set("color", Nomad::JSON(static_cast<double>(unit.color)));
        u.set("group", Nomad::JSON(static_cast<double>(static_cast<uint8_t>(unit.group))));
        
        u.set("enabled", Nomad::JSON(unit.isEnabled));
        u.set("armed", Nomad::JSON(unit.isArmed));
        u.set("solo", Nomad::JSON(unit.isSolo));
        u.set("muted", Nomad::JSON(unit.isMuted));
        
        u.set("routeId", Nomad::JSON(static_cast<double>(unit.targetMixerRoute)));
        
        // Patterns
        Nomad::JSON pats = Nomad::JSON::array();
        for (auto pid : unit.associatedPatterns) {
            pats.push(Nomad::JSON(static_cast<double>(pid)));
        }
        u.set("associatedPatterns", pats);
        u.set("activePattern", Nomad::JSON(static_cast<double>(unit.activePattern)));
        
        json.push(u);
    }
    
    return json;
}

void UnitManager::loadFromJSON(const Nomad::JSON& json) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Inline clear to avoid deadlock
    m_units.clear();
    m_unitOrder.clear();
    m_nextId = 1;
    
    if (json.isArray()) {
        for (size_t i = 0; i < json.size(); ++i) {
            const Nomad::JSON& u = json[i];
            ArsenalUnit unit;
            unit.id = static_cast<UnitID>(u["id"].asNumber());
            if (unit.id >= m_nextId) m_nextId = unit.id + 1;
            
            unit.name = u["name"].asString();
            unit.color = static_cast<uint32_t>(u["color"].asNumber());
            unit.group = static_cast<UnitGroup>(static_cast<uint8_t>(u["group"].asNumber()));
            
            unit.isEnabled = u["enabled"].asBool();
            unit.isArmed = u["armed"].asBool();
            unit.isSolo = u["solo"].asBool();
            unit.isMuted = u["muted"].asBool();
            unit.runtimeEnabled.store(unit.isEnabled);
            
            unit.targetMixerRoute = static_cast<MixerRouteID>(u["routeId"].asNumber());
            
            const Nomad::JSON& pats = u["associatedPatterns"];
            if (pats.isArray()) {
                for (size_t p = 0; p < pats.size(); ++p) {
                    unit.associatedPatterns.push_back(static_cast<PatternID>(pats[p].asNumber()));
                }
            }
            unit.activePattern = static_cast<PatternID>(u["activePattern"].asNumber());
            
            m_units[unit.id] = unit;
            m_unitOrder.push_back(unit.id);
        }
    }
    updateAudioSnapshot();
}

} // namespace Audio
} // namespace Nomad
