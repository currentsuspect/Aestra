// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// FileBrowser, split by concern. This file: directory scanning: the extension filter, the
// background scan worker, and folding its results back into the tree.
// Split out of FileBrowser.cpp so no one file has to hold the whole browser;
// see Tests/Guards/file_size_ratchet.cmake.
#include "FileBrowserInternal.h"

using namespace Aestra;

namespace AestraUI {

using namespace FileBrowserInternal;

bool FileFilter::isAllowed(const std::string& path) {
    if (path.empty()) return false;

    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) return true;

    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (contains(audioExtensions, ext)) return true;
    if (contains(projectExtensions, ext)) return true;
    if (ext == ".mid" || ext == ".midi") return true;

    return false;
}

// The one extension -> FileType mapping. FileBrowser::getFileTypeFromExtension()
// used to be a second, slightly different copy of this, with no callers at all:
// it mapped .aes/.Aestra to ProjectFile where this one does not, and anyone
// extending "the" mapping had even odds of editing the dead one.
FileType FileFilter::getType(const std::string& path, bool isDir) {
    if (isDir) return FileType::Folder;

    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".wav") return FileType::WavFile;
    if (ext == ".mp3") return FileType::Mp3File;
    if (ext == ".flac") return FileType::FlacFile;
    // Ogg family shares a container glyph; .ogg previously fell through to the
    // generic music note (spec 2 §7).
    if (ext == ".ogg" || ext == ".oga" || ext == ".opus") return FileType::OggFile;
    if (ext == ".aif" || ext == ".aiff" || ext == ".m4a" || ext == ".aac" || ext == ".mp4")
        return FileType::AudioFile;
    if (ext == ".mid" || ext == ".midi") return FileType::MidiFile;
    if (contains(projectExtensions, ext)) return FileType::ProjectFile;

    return FileType::Unknown;
}

void FileBrowser::scanWorkerLoop() {
    while (true) {
        ScanTask task;
        {
            std::unique_lock<std::mutex> lock(scanMutex_);
            scanCv_.wait(lock, [&]() {
                return scanStop_.load(std::memory_order_acquire) || !scanTasks_.empty();
            });

            if (scanStop_.load(std::memory_order_acquire) && scanTasks_.empty()) {
                return;
            }

            task = std::move(scanTasks_.front());
            scanTasks_.pop_front();
        }

        const uint64_t currentGen = scanGeneration_.load(std::memory_order_acquire);
        if (task.generation != currentGen) {
            continue;
        }

        ScanResult result;
        result.kind = task.kind;
        result.path = task.path;
        result.depth = task.depth;
        result.generation = task.generation;
        if (task.kind == ScanKind::Listing) {
            result.items = scanListing(task.paths, task.generation);
        } else {
            result.items = scanDirectory(task.path, task.depth, task.showHidden, task.generation, result.error);
        }

        {
            std::lock_guard<std::mutex> lock(scanMutex_);
            scanResults_.push_back(std::move(result));
        }
    }
}

std::vector<FileItem> FileBrowser::scanDirectory(const std::string& path, int depth, bool showHidden,
                                                 uint64_t generation, std::string& error) const {
    std::vector<FileItem> items;
    error.clear();
    try {
        const std::filesystem::path dir(path);
        const auto options = std::filesystem::directory_options::skip_permission_denied;
        std::error_code iterEc;
        std::filesystem::directory_iterator it(dir, options, iterEc);
        if (iterEc) {
            error = iterEc.message();
            Log::warning(std::string("[FileBrowser] Scan failed for ") + path + ": " + iterEc.message());
            return items;
        }

        for (; it != std::filesystem::directory_iterator(); it.increment(iterEc)) {
            if (scanStop_.load(std::memory_order_acquire) ||
                generation != scanGeneration_.load(std::memory_order_acquire)) {
                break;
            }

            if (iterEc) {
                error = iterEc.message();
                Log::warning(std::string("[FileBrowser] Scan iteration failed for ") + path + ": " + iterEc.message());
                break;
            }

            const auto& entry = *it;

            const std::string name = entry.path().filename().string();
            if (!showHidden && !name.empty() && name[0] == '.') {
                continue;
            }

            const std::string entryPath = entry.path().string();

            // --- SMART FILTER APPLIED HERE ---
            // If it's not a directory and not in our whitelist, skip it.
            std::error_code dirEc;
            const bool isDir = entry.is_directory(dirEc);
            if (dirEc) continue;

            if (!isDir && !FileFilter::isAllowed(entryPath)) {
                continue; // Whitelist filter
            }

            FileType type = FileFilter::getType(entryPath, isDir);
            size_t size = 0;
            std::string lastModified;

            if (!isDir) {
                std::error_code sizeEc;
                size = static_cast<size_t>(entry.file_size(sizeEc));
                if (sizeEc) size = 0;
            }

            FileItem item(name, entryPath, type, isDir, size, lastModified);
            item.depth = depth;
            {
                std::error_code timeEc;
                const auto modified = entry.last_write_time(timeEc);
                if (!timeEc) item.modifiedTime = static_cast<int64_t>(modified.time_since_epoch().count());
            }
            fillAudioFacts(item); // length/rate/channels/BPM/key; worker thread
            items.push_back(std::move(item));
        }
    } catch (const std::exception& e) {
        error = e.what();
        Log::warning(std::string("[FileBrowser] Scan failed for ") + path + ": " + e.what());
    }

    return items;
}

std::vector<FileItem> FileBrowser::scanListing(const std::vector<std::string>& paths, uint64_t generation) const {
    namespace fs = std::filesystem;
    std::vector<FileItem> items;
    items.reserve(paths.size());
    for (const auto& rawPath : paths) {
        if (scanStop_.load(std::memory_order_acquire) ||
            generation != scanGeneration_.load(std::memory_order_acquire)) {
            break;
        }
        // Favorites and tags are stored as generic ('/') keys; hand the OS its
        // native form so the row's path matches what a directory scan produces.
        const fs::path path = fs::path(rawPath).make_preferred();
        std::error_code ec;
        const fs::file_status status = fs::status(path, ec);
        if (ec || !fs::exists(status)) {
            continue; // Moved or deleted since it was added: leave it out, keep the entry.
        }
        const bool isDir = fs::is_directory(status);

        size_t size = 0;
        if (!isDir) {
            std::error_code sizeEc;
            size = static_cast<size_t>(fs::file_size(path, sizeEc));
            if (sizeEc) size = 0;
        }

        std::string name = path.filename().string();
        if (name.empty()) name = path.string();
        const FileType type = FileFilter::getType(path.string(), isDir);
        FileItem item(name, path.string(), type, isDir, size, "");
        std::error_code timeEc;
        const auto modified = fs::last_write_time(path, timeEc);
        if (!timeEc) item.modifiedTime = static_cast<int64_t>(modified.time_since_epoch().count());
        fillAudioFacts(item);
        items.push_back(std::move(item));
    }
    return items;
}

void FileBrowser::processScanResults() {
    std::deque<ScanResult> results;
    {
        std::lock_guard<std::mutex> lock(scanMutex_);
        if (scanResults_.empty()) return;
        results.swap(scanResults_);
    }

    const uint64_t currentGen = scanGeneration_.load(std::memory_order_acquire);
    bool didUpdate = false;

    for (auto& result : results) {
        if (result.generation != currentGen) continue;

        if (result.kind == ScanKind::Root || result.kind == ScanKind::Listing) {
            scanningRoot_ = false;
            scanError_ = std::move(result.error);

            rootItems_ = std::move(result.items);
            sortFiles();
            updateDisplayList();

            if (isFilterActive()) {
                applyFilter();
            } else {
                filteredFiles_.clear();
                viewDirty_ = true;
                if (!pendingSelectionPath_.empty()) {
                    const std::string restoredPath = pendingSelectionPath_;
                    pendingSelectionPath_.clear();
                    selectFile(restoredPath);
                } else if (!displayItems_.empty()) {
                    selectedIndex_ = 0;
                    selectedFile_ = displayItems_[0];
                    selectedIndices_.clear();
                    selectedIndices_.push_back(0);
                    lastShiftSelectIndex_ = 0;
                } else {
                    clearSelection();
                }
                updateScrollbarVisibility();
                invalidateCache();
            }

            didUpdate = true;
            continue;
        }

        if (result.kind == ScanKind::Folder) {
            if (FileItem* folder = findItemByPath(result.path)) {
                folder->children = std::move(result.items);
                folder->hasLoadedChildren = result.error.empty();
                folder->isLoadingChildren = false;

                if (!result.error.empty()) {
                    FileItem placeholder("Folder unavailable — collapse and reopen to retry", "",
                                         FileType::Unknown, false, 0, "");
                    placeholder.depth = folder->depth + 1;
                    placeholder.isPlaceholder = true;
                    folder->children.push_back(std::move(placeholder));
                }

                std::stable_sort(folder->children.begin(), folder->children.end(),
                                 [this](const FileItem& a, const FileItem& b) { return compareFileItems(a, b); });

                updateDisplayList();
                if (isFilterActive()) {
                    applyFilter();
                } else {
                    updateScrollbarVisibility();
                    invalidateCache();
                }
                didUpdate = true;
            }
        }
    }

    if (didUpdate) {
        updateScrollbarVisibility();
    }
}

} // namespace AestraUI
