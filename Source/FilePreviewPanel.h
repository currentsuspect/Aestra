// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Core/NUIIcon.h"
#include "FileBrowser.h" // For FileItem definition
#include <vector>
#include <string>
#include <functional>
#include <future>
#include <atomic>
#include <vector>
#include <mutex>

namespace NomadUI {

class FilePreviewPanel : public NUIComponent {
public:
    FilePreviewPanel();
    ~FilePreviewPanel() override;

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    // Data input
    void setFile(const FileItem* file);
    void clear();

    // Playback state
    void setPlaying(bool playing);
    void setLoading(bool loading);
    void setPlayheadPosition(double seconds) { playheadPosition_ = seconds; setDirty(true); }

    // Events
    void setOnPlay(std::function<void(const FileItem&)> callback) { onPlay_ = callback; }
    void setOnStop(std::function<void()> callback) { onStop_ = callback; }
    void setOnSeek(std::function<void(double)> callback) { onSeek_ = callback; }
    
private:
    // void generateWaveform(const std::string& path, size_t fileSize); // Removed in favor of async logic

    const FileItem* currentFile_ = nullptr;
    std::vector<float> waveformData_;
    
    bool isPlaying_ = false;
    bool isLoading_ = false;
    float loadingAnimationTime_ = 0.0f;

    // Layout
    NUIRect playButtonBounds_;

    std::function<void(const FileItem&)> onPlay_;
    std::function<void()> onStop_;

    // Icons
    std::shared_ptr<NUIIcon> folderIcon_;
    std::shared_ptr<NUIIcon> fileIcon_;

    void onUpdate(double deltaTime) override;

    // Mutex for async data transfer
    std::mutex waveformMutex_;
    std::vector<float> pendingWaveform_;
    uint32_t loadedSampleRate_ = 0;
    uint32_t loadedChannels_ = 0;
    size_t loadedDurationMs_ = 0;
    double loadedDurationSeconds_ = 0.0;
    double playheadPosition_ = 0.0; // Current playback time in seconds

    // Callbacks
    std::function<void(double)> onSeek_;

    // Async Loading
    std::future<void> loadFuture_;
    std::atomic<bool> cancelLoading_{false};
    std::atomic<bool> waveformReady_{false};
    std::string loadedFilePath_; // To verify completion matches current request
};

} // namespace NomadUI
