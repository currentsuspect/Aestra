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

    std::function<void(const FileItem&)> onPlay_;
    std::function<void()> onStop_;
    std::function<void(double)> onSeek_;

    // Icons
    std::shared_ptr<NUIIcon> folderIcon_;
    std::shared_ptr<NUIIcon> fileIcon_;
    std::shared_ptr<NUIIcon> playIcon_;
    std::shared_ptr<NUIIcon> stopIcon_;
};

} // namespace NomadUI
