// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Core/NUIIcon.h"
#include "FileBrowser.h" // For FileItem definition
#include <vector>
#include <string>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>

namespace NomadUI {

class FilePreviewPanel : public NUIComponent {
public:
    FilePreviewPanel();
    ~FilePreviewPanel() override = default;

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    // Data input
    void setFile(const FileItem* file);
    void clear();

    // Playback state
    void setPlaying(bool playing);
    void setLoading(bool loading);

    // Events
    void setOnPlay(std::function<void(const FileItem&)> callback) { onPlay_ = callback; }
    void setOnStop(std::function<void()> callback) { onStop_ = callback; }
    void setOnSeek(std::function<void(double)> callback) { onSeek_ = callback; }
    void setPlayheadPosition(double seconds);
    void setDuration(double seconds);

private:
    void generateWaveform(const std::string& path, size_t fileSize);
    void waveformWorker(const std::string& path, uint64_t generation);

    const FileItem* currentFile_ = nullptr;
    std::vector<float> waveformData_;
    mutable std::mutex waveformMutex_;
    
    bool isPlaying_ = false;
    bool isLoading_ = false;
    float loadingAnimationTime_ = 0.0f;
    double playheadPosition_ = 0.0;
    double duration_ = 0.0;
    
    // Async control
    std::atomic<uint64_t> currentGeneration_{0};
    
    // Layout
    NUIRect playButtonBounds_;

    // Callbacks
    std::function<void(const FileItem&)> onPlay_;
    std::function<void()> onStop_;
    std::function<void(double)> onSeek_;

    // Icons
    std::shared_ptr<NUIIcon> folderIcon_;
    std::shared_ptr<NUIIcon> fileIcon_;
};

} // namespace NomadUI
