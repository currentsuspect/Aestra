// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "FileBrowser.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace AestraUI {

/**
 * @brief Background index of every sound under the user's library roots, so
 * search can reach files in folders that are not open.
 *
 * The crawl runs on a DETACHED worker that owns its state: nothing on the UI
 * thread ever joins or waits for it, so a slow disk or network mount cannot
 * stall the UI, and rebuild() never blocks on the previous crawl (it is told
 * to stop and left to finish on its own). Results are published as immutable
 * snapshots, periodically during the crawl, so search works while it runs.
 *
 * Bounded: hidden folders and directory symlinks are not followed, depth and
 * entry count are capped. In memory only; rebuilt at startup and when the
 * roots change.
 */
class BrowserLibraryIndex {
public:
    static constexpr size_t kMaxEntries = 100000;
    static constexpr int kMaxDepth = 12;

    using Snapshot = std::shared_ptr<const std::vector<FileItem>>;

    BrowserLibraryIndex() = default;
    ~BrowserLibraryIndex();
    BrowserLibraryIndex(const BrowserLibraryIndex&) = delete;
    BrowserLibraryIndex& operator=(const BrowserLibraryIndex&) = delete;

    /// Start indexing @p roots (overlaps are indexed once), abandoning any
    /// crawl in progress. The previous snapshot stays readable until the new
    /// crawl publishes.
    void rebuild(std::vector<std::string> roots);

    /// Latest published entries (files only), or null before the first publish.
    Snapshot snapshot() const;
    /// Bumps on every publish; lets the UI re-run a live search.
    uint64_t revision() const;
    bool isCrawling() const;

private:
    // Everything the worker touches. The worker holds its own reference, so
    // destroying the index while a crawl runs is safe and immediate.
    struct Shared {
        mutable std::mutex mutex;
        Snapshot published;
        std::shared_ptr<std::atomic<bool>> currentStop; // the running crawl's cancel flag
        std::atomic<uint64_t> revision{0};
        std::atomic<bool> crawling{false};
    };

    static void crawl(const std::shared_ptr<Shared>& shared, const std::shared_ptr<std::atomic<bool>>& stop,
                      const std::vector<std::string>& roots);

    std::shared_ptr<Shared> m_shared = std::make_shared<Shared>();
};

/// Fill an audio FileItem's length/rate/channels/BPM/key from its header and
/// name. Cheap (a header read); safe on any thread.
void fillAudioFacts(FileItem& item);

} // namespace AestraUI
