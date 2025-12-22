// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "ClipSource.h"
#include "NomadLog.h"
#include <unordered_map>
#include <mutex>

namespace Nomad {
namespace Audio {

// =============================================================================
// ClipSource Implementation
// =============================================================================

ClipSource::ClipSource(ClipSourceID id, const std::string& name)
    : m_id(id)
    , m_name(name)
{
}

void ClipSource::setBuffer(std::shared_ptr<AudioBufferData> buffer) {
    m_buffer = std::move(buffer);
    if (m_buffer && m_buffer->isValid()) {
        Log::info("ClipSource '" + m_name + "' buffer set: " + 
                  std::to_string(m_buffer->numFrames) + " frames, " +
                  std::to_string(m_buffer->sampleRate) + " Hz, " +
                  std::to_string(m_buffer->numChannels) + " ch");
    }
}

// =============================================================================
// SourceManager Implementation
// =============================================================================

struct SourceManager::Impl {
    std::unordered_map<uint32_t, std::unique_ptr<ClipSource>> sources;
    std::unordered_map<std::string, ClipSourceID> pathToId;
    std::atomic<uint32_t> nextId{1};
    mutable std::mutex mutex;
};

SourceManager::SourceManager()
    : m_impl(std::make_unique<Impl>())
{
    Log::info("SourceManager created");
}

SourceManager::~SourceManager() {
    clear();
    Log::info("SourceManager destroyed");
}

ClipSourceID SourceManager::createSource(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    ClipSourceID id;
    id.value = m_impl->nextId.fetch_add(1);
    
    auto source = std::make_unique<ClipSource>(id, name);
    m_impl->sources[id.value] = std::move(source);
    
    Log::info("SourceManager: Created source ID " + std::to_string(id.value) + 
              " '" + name + "'");
    
    return id;
}

ClipSourceID SourceManager::getOrCreateSource(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    // Check if source already exists for this path
    auto it = m_impl->pathToId.find(filePath);
    if (it != m_impl->pathToId.end()) {
        return it->second;
    }
    
    // Create new source
    ClipSourceID id;
    id.value = m_impl->nextId.fetch_add(1);
    
    // Extract name from path
    std::string name = filePath;
    size_t lastSlash = filePath.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        name = filePath.substr(lastSlash + 1);
    }
    
    auto source = std::make_unique<ClipSource>(id, name);
    source->setFilePath(filePath);
    
    m_impl->sources[id.value] = std::move(source);
    m_impl->pathToId[filePath] = id;
    
    Log::info("SourceManager: Created source ID " + std::to_string(id.value) + 
              " for file '" + filePath + "'");
    
    return id;
}

ClipSource* SourceManager::getSource(ClipSourceID id) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto it = m_impl->sources.find(id.value);
    if (it != m_impl->sources.end()) {
        return it->second.get();
    }
    return nullptr;
}

const ClipSource* SourceManager::getSource(ClipSourceID id) const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto it = m_impl->sources.find(id.value);
    if (it != m_impl->sources.end()) {
        return it->second.get();
    }
    return nullptr;
}

ClipSource* SourceManager::getSourceByPath(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto it = m_impl->pathToId.find(filePath);
    if (it != m_impl->pathToId.end()) {
        auto sourceIt = m_impl->sources.find(it->second.value);
        if (sourceIt != m_impl->sources.end()) {
            return sourceIt->second.get();
        }
    }
    return nullptr;
}

bool SourceManager::removeSource(ClipSourceID id) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto it = m_impl->sources.find(id.value);
    if (it == m_impl->sources.end()) {
        return false;
    }
    
    // Remove from path map if present
    const std::string& filePath = it->second->getFilePath();
    if (!filePath.empty()) {
        m_impl->pathToId.erase(filePath);
    }
    
    m_impl->sources.erase(it);
    
    Log::info("SourceManager: Removed source ID " + std::to_string(id.value));
    
    return true;
}

std::vector<ClipSourceID> SourceManager::getAllSourceIDs() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    std::vector<ClipSourceID> ids;
    ids.reserve(m_impl->sources.size());
    
    for (const auto& pair : m_impl->sources) {
        ClipSourceID id;
        id.value = pair.first;
        ids.push_back(id);
    }
    
    return ids;
}

size_t SourceManager::getSourceCount() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->sources.size();
}

void SourceManager::clear() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    size_t count = m_impl->sources.size();
    m_impl->sources.clear();
    m_impl->pathToId.clear();
    
    Log::info("SourceManager: Cleared " + std::to_string(count) + " sources");
}

size_t SourceManager::getTotalMemoryUsage() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    size_t total = 0;
    for (const auto& pair : m_impl->sources) {
        const auto* buffer = pair.second->getRawBuffer();
        if (buffer) {
            total += buffer->sizeInBytes();
        }
    }
    
    return total;
}

} // namespace Audio
} // namespace Nomad
