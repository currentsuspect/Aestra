#pragma once
#include "ClipSource.h"

#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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
    ClipSourceID getOrCreateSource(const std::string& filePath, const std::string& displayName = {}) {
        auto it = m_pathToId.find(filePath);
        if (it != m_pathToId.end()) {
            return it->second;
        }

        // Create new source
        ClipSourceID id{nextId++};
        auto source = std::make_unique<ClipSource>(id, displayName.empty() ? makeDisplayName(filePath) : displayName);
        source->setFilePath(filePath);
        source->setOwner(this); // so a later readiness flip can bump the revision

        m_sources[id.value] = std::move(source);
        m_pathToId[filePath] = id;
        ++m_revision;

        return id;
    }

    /**
     * @brief Get or create a source restoring a serialized identity (#446).
     *
     * Path is still the dedupe key: an existing source for @p filePath wins
     * regardless of the requested ID. Otherwise a valid, unused requested ID
     * is restored verbatim (advancing the mint counter past it) and an
     * invalid or taken ID falls back to a fresh mint. UINT64_MAX is never
     * restored: bumping the counter past it would wrap to zero and poison
     * every later mint.
     * @param requestedId Serialized source ID to restore (invalid = mint).
     * @param filePath Source file path (dedupe key).
     * @param displayName Optional user-facing name.
     * @return Source ID (returns ClipSourceID{0} on failure)
     */
    ClipSourceID getOrCreateSourceWithId(ClipSourceID requestedId, const std::string& filePath,
                                         const std::string& displayName = {}) {
        auto it = m_pathToId.find(filePath);
        if (it != m_pathToId.end()) {
            return it->second;
        }

        ClipSourceID id{};
        if (requestedId.isValid() && requestedId.value != std::numeric_limits<uint64_t>::max()
            && m_sources.find(requestedId.value) == m_sources.end()) {
            id = requestedId;
            if (requestedId.value >= nextId) {
                nextId = requestedId.value + 1;
            }
        } else {
            id = ClipSourceID{nextId++};
        }

        auto source = std::make_unique<ClipSource>(id, displayName.empty() ? makeDisplayName(filePath) : displayName);
        source->setFilePath(filePath);
        source->setOwner(this); // so a later readiness flip can bump the revision

        m_sources[id.value] = std::move(source);
        m_pathToId[filePath] = id;
        ++m_revision;

        return id;
    }

    /**
     * @brief Canonical entry point for handing decoded audio to a managed source.
     *
     * Wraps ClipSource::setBuffer with the bookkeeping callers must not be able
     * to forget: (re)binding ownership and moving the revision counter the
     * timeline's waveform-cache sweep watches (TrackManagerUIRender compares it
     * once per frame). A not-ready -> ready flip bumps through the ClipSource
     * back-pointer; replacing the buffer of an already-ready source bumps here,
     * because its old waveform cache was just discarded without any readiness
     * change. A failed attach — no source, or a buffer that leaves the source
     * unready — bumps nothing and leaves the retryable state untouched.
     * @param source Managed source receiving the buffer (null is a no-op).
     * @param buffer Decoded audio to attach.
     */
    void attachBuffer(ClipSource* source, std::shared_ptr<AudioBufferData> buffer) {
        if (!source) {
            return;
        }
        // Reject invalid payloads before they can clobber ready content: a
        // failed attach must never discard valid audio and its cache.
        if (!buffer || !buffer->isValid()) {
            return;
        }
        // First manager to attach owns the source; a later attach through a
        // different manager still proceeds (single-manager process today),
        // and this manager's revision is what the sweep watches.
        source->setOwner(this);
        const bool wasReady = source->isValid();
        source->setBuffer(std::move(buffer)); // bumps itself on a readiness flip
        if (wasReady && source->isValid()) {
            ++m_revision; // ready -> ready replacement still discards the cache
        }
    }

    /**
     * @brief Create or replace a source for a recorded take.
     * @param filePath Stable file path written for the take.
     * @param displayName User-facing clip name.
     * @param buffer In-memory audio buffer for immediate playback.
     * @return Source ID (returns ClipSourceID{0} on failure)
     */
    ClipSourceID createRecordedSource(const std::string& filePath, const std::string& displayName,
                                      std::shared_ptr<AudioBufferData> buffer) {
        if (!buffer || !buffer->isValid()) {
            return ClipSourceID{};
        }

        ClipSourceID id = getOrCreateSource(filePath, displayName);
        auto* source = getSource(id);
        if (!source) {
            return ClipSourceID{};
        }

        // Canonical attach: bumps the revision whether this flips an existing
        // path-deduped source to ready or replaces an earlier take's buffer —
        // both are transitions waveform-cache builders watch for.
        attachBuffer(source, std::move(buffer));
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
     * @brief Rebind a source to a new file path (T-7 relink/recovery, C-004).
     *
     * For sources whose asset went missing or moved: moves the path-dedupe
     * key to @p newFilePath (a stale old key would make a later
     * getOrCreateSource(oldPath) mint a duplicate) and repoints the source.
     * Rejects a path another source already holds — merging two sources is a
     * decision, not a side effect of a relink. Buffer attachment stays the
     * caller's job via attachBuffer(), so a failed decode leaves the source
     * untouched and retryable.
     * @return True when the source now lives at @p newFilePath.
     */
    bool relinkSource(ClipSourceID id, const std::string& newFilePath) {
        if (newFilePath.empty()) {
            return false;
        }
        auto it = m_sources.find(id.value);
        if (it == m_sources.end()) {
            return false;
        }
        auto taken = m_pathToId.find(newFilePath);
        if (taken != m_pathToId.end() && taken->second != id) {
            return false;
        }
        const std::string oldPath = it->second->getFilePath();
        it->second->setFilePath(newFilePath);
        if (oldPath != newFilePath) {
            m_pathToId.erase(oldPath);
            m_pathToId[newFilePath] = id;
            ++m_revision;
        }
        return true;
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
     * @brief Look up an existing source by file path without creating one.
     *
     * Lets a caller tell "this file was already in the project" from "I am the
     * one who introduced it", which decides whether it may be taken back out
     * again on failure.
     * @return An invalid ID when no source holds that path.
     */
    ClipSourceID findSourceByPath(const std::string& filePath) const {
        auto it = m_pathToId.find(filePath);
        return it != m_pathToId.end() ? it->second : ClipSourceID{};
    }

    /**
     * @brief Remove a source nothing references yet.
     *
     * Only safe for a source whose registration is being rolled back before it
     * ever reached a pattern or a built AudioGraph — an import that failed
     * partway. Never call it to "clean up" a source a project object still
     * points at.
     *
     * Same reasoning as clear(): buffers are co-owned via
     * shared_ptr<AudioBufferData>, so any in-flight graph keeps its audio
     * alive regardless.
     * @return True when a source was removed.
     */
    bool removeSource(ClipSourceID id) {
        auto it = m_sources.find(id.value);
        if (it == m_sources.end()) {
            return false;
        }
        const std::string path = it->second ? it->second->getFilePath() : std::string{};
        m_sources.erase(it);
        auto pathIt = m_pathToId.find(path);
        if (pathIt != m_pathToId.end() && pathIt->second.value == id.value) {
            m_pathToId.erase(pathIt);
        }
        ++m_revision;
        return true;
    }

    /**
     * @brief Clear all sources
     *
     * No mutex required: ClipSource buffers are now co-owned via
     * shared_ptr<AudioBufferData> held by ClipRenderState in each live AudioGraph.
     * When clear() destroys all ClipSource objects, the shared_ptr refcounts held
     * by any in-flight AudioGraph keep AudioBufferData alive until the graph is
     * replaced. The atomic swap in EngineState ensures the audio thread never
     * sees a partially-constructed graph.
     */
    void clear() {
        m_sources.clear();
        m_pathToId.clear();
        ++m_revision;
    }

    /**
     * @brief Wake every revision watcher (the timeline's waveform-cache sweep).
     *
     * Called by ClipSource::setBuffer when a buffer attachment flips a managed
     * source from not-ready to ready — the transition that makes a cache build
     * necessary. Watchers compare snapshots, so an extra bump only costs one
     * redundant sweep pass, never a missed one.
     */
    void bumpRevision() { ++m_revision; }

    /**
     * @brief Monotonic counter bumped whenever the source set or a source's
     *        readiness changes.
     *
     * Lets a UI change-gate an O(n) sweep over sources (such as building
     * missing waveform caches) down to an O(1) comparison per frame, without
     * every import path having to remember to notify anyone.
     */
    uint64_t getRevision() const {
        return m_revision;
    }

private:
    static std::string makeDisplayName(const std::string& filePath) {
        try {
            std::filesystem::path p(filePath);
            const std::string stem = p.stem().string();
            if (!stem.empty()) {
                return stem;
            }
            const std::string filename = p.filename().string();
            if (!filename.empty()) {
                return filename;
            }
        } catch (const std::filesystem::filesystem_error&) {
        }
        return filePath;
    }

    uint64_t nextId{1};
    uint64_t m_revision{0};
    std::unordered_map<uint64_t, std::unique_ptr<ClipSource>> m_sources;
    std::unordered_map<std::string, ClipSourceID> m_pathToId;
};

} // namespace Audio
} // namespace Aestra
