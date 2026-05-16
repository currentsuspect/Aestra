// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUIIcon.h"
#include "FileBrowser.h" // For FileItem definition
#include <vector>
#include <string>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>

namespace AestraUI {

class FilePreviewPanel : public NUIComponent {
public:
    FilePreviewPanel();
    ~FilePreviewPanel() override = default;

    void onRender(NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;
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
    bool hasFileSelection() const { return hasCurrentFile_ && !currentFile_.isDirectory; }

    // Loop and BPM sync
    void setLoopEnabled(bool loop) { m_loopEnabled = loop; setDirty(true); }
    bool isLoopEnabled() const { return m_loopEnabled; }
    void setBpmSyncEnabled(bool sync) { m_bpmSyncEnabled = sync; setDirty(true); }
    bool isBpmSyncEnabled() const { return m_bpmSyncEnabled; }
    void setProjectBpm(int bpm) { m_projectBpm = bpm; }
    void setCurrentFileBpm(int bpm) { m_currentFileBpm = bpm; }
    void onPreviewEnded();

    // Transport bar height (scrubber + 4px gap + bar + padding)
    static constexpr float kTransportBarHeight = 26.0f;
    float getRequiredHeight() const;

    // Replay callback for loop (bypasses setFile / waveform regen)
    void setOnReplay(std::function<void()> callback) { onReplay_ = callback; }

private:
    void generateWaveform(const std::string& path, size_t fileSize);
    void waveformWorker(const std::string& path, uint64_t generation);

    FileItem currentFile_;
    bool hasCurrentFile_ = false;
    std::vector<float> waveformData_;
    mutable std::mutex waveformMutex_;
    
    bool isPlaying_ = false;
    bool isLoading_ = false;
    bool isWaveformLoading_ = false;
    float loadingAnimationTime_ = 0.0f;
    double playheadPosition_ = 0.0;
    double duration_ = 0.0;
    
    // Async control
    std::atomic<uint64_t> currentGeneration_{0};
    std::string pendingWaveformPath_;
    size_t pendingWaveformFileSize_ = 0;
    double pendingWaveformDelay_ = 0.0;
    bool waveformQueued_ = false;
    std::atomic<bool> waveformJustCompleted_{false};
    
    // Layout
    bool isSeekDragging_ = false;
    NUIRect playButtonBounds_;
    NUIRect scrubberBounds_;

    // Transport controls
    NUIRect stopButtonBounds_;
    NUIRect loopButtonBounds_;
    NUIRect bpmSyncButtonBounds_;

    // Callbacks
    std::function<void(const FileItem&)> onPlay_;
    std::function<void()> onStop_;
    std::function<void(double)> onSeek_;
    std::function<void()> onReplay_;

    // Icons
    std::shared_ptr<NUIIcon> folderIcon_;
    std::shared_ptr<NUIIcon> fileIcon_;
    std::shared_ptr<NUIIcon> audioFileIcon_;
    std::shared_ptr<NUIIcon> playIcon_;
    std::shared_ptr<NUIIcon> stopIcon_;
    std::shared_ptr<NUIIcon> loopIcon_;
    std::shared_ptr<NUIIcon> bpmSyncIcon_;

    // Loop and BPM sync state
    bool m_loopEnabled = false;
    bool m_bpmSyncEnabled = false;
    int m_projectBpm = 0;
    int m_currentFileBpm = 0;
    std::string m_currentFilePath;
};

} // namespace AestraUI
