// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "BrowserLibraryIndex.h"

#include "BrowserLibrary.h"

#include <filesystem>
#include <thread>

namespace AestraUI {

namespace {

bool isAudioType(FileType type) {
    return type == FileType::AudioFile || type == FileType::MusicFile || type == FileType::WavFile ||
           type == FileType::Mp3File || type == FileType::FlacFile || type == FileType::OggFile;
}

std::filesystem::path canonicalOrNormal(const std::filesystem::path& p) {
    std::error_code ec;
    const auto canonical = std::filesystem::weakly_canonical(p, ec);
    return ec ? p.lexically_normal() : canonical;
}

bool isSameOrInside(const std::filesystem::path& inner, const std::filesystem::path& outer) {
    auto o = outer.begin();
    auto i = inner.begin();
    for (; o != outer.end(); ++o, ++i) {
        if (o->empty())
            break; // trailing separator
        if (i == inner.end() || *i != *o)
            return false;
    }
    return true;
}

// Overlapping roots (one inside another, or two spellings of one folder) are
// resolved ONCE, here, instead of canonicalising every file found: realpath is
// several syscalls, and a full library is 100k files. Directory symlinks are not
// followed during the crawl, so two disjoint canonical roots cannot reach the
// same file.
std::vector<std::filesystem::path> distinctRoots(const std::vector<std::string>& roots) {
    std::vector<std::filesystem::path> canon;
    for (const auto& r : roots) {
        std::error_code ec;
        if (r.empty() || !std::filesystem::is_directory(r, ec))
            continue;
        canon.push_back(canonicalOrNormal(r));
    }
    std::vector<std::filesystem::path> out;
    for (size_t i = 0; i < canon.size(); ++i) {
        bool covered = false;
        for (size_t j = 0; j < canon.size() && !covered; ++j) {
            if (i == j)
                continue;
            // Inside another root, or an exact duplicate that a lower index keeps.
            const bool inside = isSameOrInside(canon[i], canon[j]);
            covered = inside && (canon[i] != canon[j] || j < i);
        }
        if (!covered)
            out.push_back(canon[i]);
    }
    return out;
}

// Publish every this-many new entries while crawling, so search works early.
constexpr size_t kPublishEvery = 2000;

} // namespace

void fillAudioFacts(FileItem& item) {
    if (item.isDirectory || !isAudioType(item.type)) return;
    BrowserLibrary::AudioInfo info;
    if (BrowserLibrary::readAudioInfo(item.path, info)) {
        item.durationSec = info.durationSec;
        item.sampleRate = info.sampleRate;
        item.channels = info.channels;
    }
    item.detectedBpm = BrowserLibrary::parseBpmFromFilename(item.name);
    if (item.detectedBpm == 0 && info.tempo > 0.0f) item.detectedBpm = static_cast<int>(info.tempo + 0.5f);
    item.musicalKey = BrowserLibrary::parseKeyFromFilename(item.name);
}

BrowserLibraryIndex::~BrowserLibraryIndex() {
    std::lock_guard<std::mutex> lock(m_shared->mutex);
    if (m_shared->currentStop) m_shared->currentStop->store(true);
}

void BrowserLibraryIndex::rebuild(std::vector<std::string> roots) {
    auto stop = std::make_shared<std::atomic<bool>>(false);
    {
        std::lock_guard<std::mutex> lock(m_shared->mutex);
        if (m_shared->currentStop) m_shared->currentStop->store(true);
        m_shared->currentStop = stop;
        m_shared->crawling.store(true);
    }
    std::thread([shared = m_shared, stop, roots = std::move(roots)]() { crawl(shared, stop, roots); }).detach();
}

BrowserLibraryIndex::Snapshot BrowserLibraryIndex::snapshot() const {
    std::lock_guard<std::mutex> lock(m_shared->mutex);
    return m_shared->published;
}

uint64_t BrowserLibraryIndex::revision() const {
    return m_shared->revision.load();
}

bool BrowserLibraryIndex::isCrawling() const {
    return m_shared->crawling.load();
}

void BrowserLibraryIndex::crawl(const std::shared_ptr<Shared>& shared, const std::shared_ptr<std::atomic<bool>>& stop,
                                const std::vector<std::string>& roots) {
    namespace fs = std::filesystem;
    std::vector<FileItem> entries;
    size_t sincePublish = 0;

    const auto publish = [&](bool final) {
        auto snap = std::make_shared<const std::vector<FileItem>>(entries);
        std::lock_guard<std::mutex> lock(shared->mutex);
        if (stop->load()) return; // superseded: never overwrite the newer crawl
        shared->published = std::move(snap);
        shared->revision.fetch_add(1);
        if (final) shared->crawling.store(false);
    };

    for (const auto& root : distinctRoots(roots)) {
        if (stop->load() || entries.size() >= kMaxEntries) break;
        std::error_code ec;

        // Default options: directory symlinks are not followed (no loops).
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        const fs::recursive_directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            if (stop->load() || entries.size() >= kMaxEntries) break;
            const fs::directory_entry& entry = *it;
            const std::string name = entry.path().filename().string();
            std::error_code typeEc;
            const bool isDir = entry.is_directory(typeEc);
            if (!name.empty() && name[0] == '.') {
                if (isDir) it.disable_recursion_pending();
                continue;
            }
            if (isDir) {
                if (it.depth() >= kMaxDepth) it.disable_recursion_pending();
                continue;
            }
            if (typeEc || !entry.is_regular_file(typeEc)) continue;
            const std::string path = entry.path().string();
            // getType by extension, not isAllowed(): the entry is already known
            // to be a regular file, and isAllowed() stats the path again to ask
            // whether it is a directory -- one wasted syscall per file, 100k on
            // a full library. For a file the two agree exactly: anything
            // isAllowed() accepts maps to a known type.
            const FileType type = FileFilter::getType(path, false);
            if (type == FileType::Unknown) continue;

            std::error_code sizeEc;
            const auto size = entry.file_size(sizeEc);
            FileItem item(name, path, type, false, sizeEc ? 0 : static_cast<size_t>(size));
            std::error_code timeEc;
            const auto modified = entry.last_write_time(timeEc);
            if (!timeEc) item.modifiedTime = static_cast<int64_t>(modified.time_since_epoch().count());
            fillAudioFacts(item);
            entries.push_back(std::move(item));

            if (++sincePublish >= kPublishEvery) {
                sincePublish = 0;
                publish(false);
            }
        }
    }
    publish(true);
}

} // namespace AestraUI
