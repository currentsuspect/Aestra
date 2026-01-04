// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "PatternManager.h"

namespace Nomad {
namespace Audio {

PatternID PatternManager::createMidiPattern(const std::string& name, double lengthBeats, const MidiPayload& payload) {
    auto pattern = std::make_shared<PatternSource>();
    pattern->id = generateNextID();
    pattern->name = name;
    // pattern->isAudio set implicitly via payload variant
    pattern->lengthBeats = lengthBeats;
    pattern->payload = payload;
    pattern->version = 1;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_pool[pattern->id] = pattern;
    return pattern->id;
}

PatternID PatternManager::createAudioPattern(const std::string& name, double lengthBeats, const AudioSlicePayload& payload) {
    auto pattern = std::make_shared<PatternSource>();
    pattern->id = generateNextID();
    pattern->name = name;
    // pattern->isAudio set implicitly via payload variant
    pattern->lengthBeats = lengthBeats;
    pattern->payload = payload;
    pattern->version = 1;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_pool[pattern->id] = pattern;
    return pattern->id;
}

PatternID PatternManager::clonePattern(PatternID originalId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_pool.find(originalId);
    if (it == m_pool.end()) return 0;

    auto clone = std::make_shared<PatternSource>(*(it->second));
    clone->id = generateNextID();
    clone->version = 1;
    m_pool[clone->id] = clone;
    return clone->id;
}

void PatternManager::removePattern(PatternID id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pool.erase(id);
}

void PatternManager::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pool.clear();
    m_nextPatternId = 1;
}


PatternSource* PatternManager::getPattern(PatternID id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_pool.find(id);
    return (it != m_pool.end()) ? it->second.get() : nullptr;
}

const PatternSource* PatternManager::getPattern(PatternID id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_pool.find(id);
    return (it != m_pool.end()) ? it->second.get() : nullptr;
}

void PatternManager::applyPatch(PatternID id, std::function<void(PatternSource&)> patcher) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_pool.find(id);
    if (it != m_pool.end()) {
        patcher(*(it->second));
        it->second->version.increment();
    }
}


std::shared_ptr<const PatternSource> PatternManager::getSafeSnapshot(PatternID id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_pool.find(id);
    return (it != m_pool.end()) ? it->second : nullptr;
}

std::vector<std::shared_ptr<PatternSource>> PatternManager::getAllPatterns() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::shared_ptr<PatternSource>> patterns;
    patterns.reserve(m_pool.size());
    for (const auto& pair : m_pool) {
        patterns.push_back(pair.second);
    }
    return patterns;
}

PatternID PatternManager::generateNextID() {
    return m_nextPatternId++;
}

} // namespace Audio
} // namespace Nomad
