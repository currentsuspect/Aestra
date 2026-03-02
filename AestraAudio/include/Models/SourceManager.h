#pragma once
#include "ClipSource.h"
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Manages audio sources (clip sources) for the project
 */
class SourceManager {
public:
    SourceManager() = default;
    
    /**
     * @brief Get or create a source for a file path
     * @return Source ID (returns ClipSourceID{0} on failure)
     */
    ClipSourceID getOrCreateSource(const std::string& filePath) {
        auto it = m_pathToId.find(filePath);
        if (it != m_pathToId.end()) {
            return it->second;
        }
        
        // Create new source
        ClipSourceID id{nextId++};
        auto source = std::make_unique<ClipSource>(id, filePath);
        source->setFilePath(filePath);
        
        m_sources[id.value] = std::move(source);
        m_pathToId[filePath] = id;
        
        return id;
    }
    
    /**
     * @brief Get a source by ID
     * @return Pointer to source, or nullptr if not found
     */
    ClipSource* getSource(ClipSourceID id) {
        auto it = m_sources.find(id.value);
        if (it != m_sources.end()) {
            return it->second.get();
        }
        return nullptr;
    }
    
    const ClipSource* getSource(ClipSourceID id) const {
        auto it = m_sources.find(id.value);
        if (it != m_sources.end()) {
            return it->second.get();
        }
        return nullptr;
    }
    
    /**
     * @brief Get all source IDs
     */
    std::vector<ClipSourceID> getAllSourceIDs() const {
        std::vector<ClipSourceID> ids;
        ids.reserve(m_sources.size());
        for (const auto& [id, _] : m_sources) {
            ids.emplace_back(id);
        }
        return ids;
    }
    
    /**
     * @brief Clear all sources
     */
    void clear() {
        m_sources.clear();
        m_pathToId.clear();
    }

private:
    uint64_t nextId{1};
    std::unordered_map<uint64_t, std::unique_ptr<ClipSource>> m_sources;
    std::unordered_map<std::string, ClipSourceID> m_pathToId;
};

} // namespace Audio
} // namespace Aestra