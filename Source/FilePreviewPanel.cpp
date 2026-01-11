// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "FilePreviewPanel.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadAudio/include/MiniAudioDecoder.h"
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <mutex>

namespace NomadUI {

FilePreviewPanel::FilePreviewPanel() {
    setId("FilePreviewPanel");

    // Initialize SVG Icons
    // Folder Icon (Material Design)
    folderIcon_ = std::make_shared<NUIIcon>("<svg viewBox='0 0 24 24'><path d='M10 4H4c-1.1 0-1.99.9-1.99 2L2 18c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z'/></svg>");
    
    // File Icon (Text Snippet style)
    fileIcon_ = std::make_shared<NUIIcon>("<svg viewBox='0 0 24 24'><path d='M14 2H6c-1.1 0-1.99.9-1.99 2L4 20c0 1.1.89 2 1.99 2H18c1.1 0 2-.9 2-2V8l-6-6zm2 16H8v-2h8v2zm0-4H8v-2h8v2zm-3-5V3.5L18.5 9H13z'/></svg>");
}

FilePreviewPanel::~FilePreviewPanel() {
    cancelLoading_ = true;
    if (loadFuture_.valid()) {
        loadFuture_.wait();
    }
}

void FilePreviewPanel::setFile(const FileItem* file) {
    // 1. Cancel existing load
    cancelLoading_ = true;
    if (loadFuture_.valid()) {
        // In a real app we might not want to block UI here, but for safety we wait or use a shared_ptr token.
        // For now, simpler to just start a new future that checks the atomic.
        // But std::async destructor blocks. Ideally store future in a list or detach.
        // Actually std::future destructor BLOCKS. So we must wait. 
        // This is a UI stall risk if decoder is slow. 
        // Better pattern: use a shared "load generation" ID or atomic flag per task.
        // For simplicity: we'll wait (decoder checks cancel flag frequently).
        // BUT wait: MiniAudioDecoder doesn't check our flag.
        // So we just abandon the old result by incrementing a generation counter or checking path.
        // Stalling destructor is unavoidable with std::async + launch::async.
        // Let's rely on fast checking.
    }
    
    currentFile_ = file;
    waveformData_.clear();
    setLoading(false);

    if (currentFile_ && !currentFile_->isDirectory) {
        std::string ext = std::filesystem::path(currentFile_->path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        if (ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".ogg" || 
            ext == ".aif" || ext == ".aiff" || ext == ".m4a" || ext == ".mp4") {
            
            // Start Async Loading
            setLoading(true);
            std::string path = currentFile_->path;
            loadedFilePath_ = path;
            cancelLoading_ = false;
            waveformReady_ = false;
            
            // Launch async task
            loadFuture_ = std::async(std::launch::async, [this, path]() {
                std::vector<float> rawAudio;
                uint32_t rate = 0;
                uint32_t channels = 0;
                
                if (cancelLoading_) return;
                
                // Decode
                bool success = Nomad::Audio::decodeAudioFile(path, rawAudio, rate, channels);
                
                if (!success || rawAudio.empty() || cancelLoading_) return;
                
                // Process for visualization (downsample to 256 points)
                std::vector<float> visualData;
                visualData.resize(256);
                
                size_t samplesPerPixel = rawAudio.size() / channels / 256;
                if (samplesPerPixel < 1) samplesPerPixel = 1;
                
                for (size_t i = 0; i < 256; ++i) {
                    float sum = 0.0f;
                    size_t startSample = i * samplesPerPixel * channels;
                    size_t count = 0;
                    
                    // Simple peak detection in chunk
                    float maxVal = 0.0f;
                    for (size_t j = 0; j < samplesPerPixel && (startSample + j*channels) < rawAudio.size(); ++j) {
                        float val = std::abs(rawAudio[startSample + j*channels]); // Use first channel
                        if (val > maxVal) maxVal = val;
                    }
                    visualData[i] = maxVal;
                }
                
                if (cancelLoading_) return;
                
                // Store result
                {
                    std::lock_guard<std::mutex> lock(waveformMutex_);
                    pendingWaveform_ = std::move(visualData);
                    loadedSampleRate_ = rate;
                    loadedChannels_ = channels;
                    loadedDurationSeconds_ = (rate > 0 && channels > 0) ? (static_cast<double>(rawAudio.size()) / channels / rate) : 0.0;
                }
                
                waveformReady_ = true;
            });
        }
    }
    setDirty(true);
}
    
void FilePreviewPanel::onUpdate(double deltaTime) {
    NUIComponent::onUpdate(deltaTime);
    
    if (isLoading_) {
        loadingAnimationTime_ += static_cast<float>(deltaTime);
        setDirty(true); // Animate spinner
    }
    
    if (waveformReady_) {
        std::lock_guard<std::mutex> lock(waveformMutex_);
        if (currentFile_ && currentFile_->path == loadedFilePath_) {
             waveformData_ = pendingWaveform_;
             setLoading(false);
             setDirty(true);
        }
        waveformReady_ = false; 
    }
}

void FilePreviewPanel::clear() {
    currentFile_ = nullptr;
    waveformData_.clear();
    setDirty(true);
}

void FilePreviewPanel::setPlaying(bool playing) {
    if (isPlaying_ != playing) {
        isPlaying_ = playing;
        setDirty(true);
    }
}

void FilePreviewPanel::setLoading(bool loading) {
    isLoading_ = loading;
    if (loading) loadingAnimationTime_ = 0.0f;
    setDirty(true);
}

// void FilePreviewPanel::generateWaveform... Removed

void FilePreviewPanel::onRender(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    NUIRect bounds = getBounds();
    
    // Panel background
    renderer.fillRoundedRect(bounds, 6.0f, theme.getColor("surfaceRaised"));
    renderer.strokeRoundedRect(bounds, 6.0f, 1.0f, theme.getColor("borderSubtle"));
    
    // === EMPTY STATE ===
    if (!currentFile_) {
        float centerX = bounds.x + bounds.width * 0.5f;
        float centerY = bounds.y + bounds.height * 0.5f;
        
        // Draw File Icon (faded)
        float iconSize = 48.0f;
        if (fileIcon_) {
            fileIcon_->setBounds(NUIRect(centerX - iconSize * 0.5f, centerY - iconSize * 0.5f - 15, iconSize, iconSize));
            fileIcon_->setColor(theme.getColor("textSecondary").withAlpha(0.2f));
            fileIcon_->onRender(renderer);
        }

        // Draw centered empty state text
        std::string emptyText = "Select a file to preview";
        float fontSize = 14.0f;
        auto size = renderer.measureText(emptyText, fontSize);
        renderer.drawText(emptyText, 
            NUIPoint(centerX - size.width * 0.5f, centerY + 25), 
            fontSize, theme.getColor("textSecondary").withAlpha(0.6f));
        return;
    }

    // === FOLDER STATE ===
    if (currentFile_->isDirectory) {
        float centerX = bounds.x + bounds.width * 0.5f;
        float centerY = bounds.y + bounds.height * 0.5f;
        
        float iconSize = 32.0f;
        float padding = 12.0f;
        float startX = 20.0f; 
        
        if (folderIcon_) {
            NUIRect iconRect(bounds.x + startX, centerY - iconSize * 0.5f, iconSize, iconSize);
            folderIcon_->setBounds(iconRect);
            NUIColor purpleAccent(0.6f, 0.3f, 0.9f, 1.0f);
            folderIcon_->setColor(purpleAccent);
            folderIcon_->onRender(renderer);
        }
        
        float textX = bounds.x + startX + iconSize + padding;
        float textMaxWidth = bounds.width - (startX + iconSize + padding + 10.0f);
        float totalTextHeight = 14.0f + 4.0f + 11.0f;
        float textStartY = centerY - (totalTextHeight * 0.5f);

        std::string name = currentFile_->name;
        if (renderer.measureText(name, 14.0f).width > textMaxWidth && name.length() > 25) {
             name = name.substr(0, 22) + "...";
        }
        
        renderer.drawText(name, NUIPoint(textX, textStartY), 14.0f, theme.getColor("textPrimary"));
        renderer.drawText("Folder", NUIPoint(textX, textStartY + 18.0f), 11.0f, theme.getColor("textSecondary"));
        return;
    }
    
    // === AUDIO STATE ===
    
    // Layout
    float topRowY = bounds.y + 6;
    float topRowHeight = 32;
    float metaHeight = 20.0f;
    float waveformY = topRowY + topRowHeight + 4;
    float waveformHeight = std::max(10.0f, bounds.height - topRowHeight - metaHeight - 10);
    
    // 1. Top Row: Info (Left) + Play (Right)
    float infoX = bounds.x + 10;
    float playBtnWidth = 32.0f;
    float playX = bounds.x + bounds.width - playBtnWidth - 10;
    
    // Filename
    std::string displayName = currentFile_->name;
    if (displayName.length() > 25) displayName = displayName.substr(0, 22) + "...";
    renderer.drawText(displayName, NUIPoint(infoX, topRowY + 2), 11.0f, theme.getColor("textPrimary"));
    
    // Play Button
    playButtonBounds_ = NUIRect(playX, topRowY + 2, playBtnWidth, 26);
    NUIColor btnColor = isPlaying_ ? theme.getColor("accentLime") : theme.getColor("primary");
    renderer.fillRoundedRect(playButtonBounds_, 4.0f, btnColor.withAlpha(0.3f));
    std::string iconStr = isPlaying_ ? "■" : "▶";
    renderer.drawText(iconStr, NUIPoint(playButtonBounds_.x + 10, playButtonBounds_.y + 5), 14.0f, btnColor);
    
    // 2. Waveform
    NUIRect waveBounds(bounds.x + 8, waveformY, bounds.width - 16, waveformHeight);
    
    // Condition fix: Show waveform IF it's ready, regardless of isLoading_ 
    // (isLoading_ might still be true if onUpdate hasn't flipped it yet)
    if (isLoading_ && waveformData_.empty()) {
         renderer.drawTextCentered("Generating Waveform...", waveBounds, theme.getFontSize("s"), theme.getColor("textSecondary"));
    } else {
         std::lock_guard<std::mutex> lock(waveformMutex_);
         
         if (!waveformData_.empty()) {
             float midY = waveBounds.y + waveBounds.height * 0.5f;
             float stepX = waveBounds.width / static_cast<float>(waveformData_.size());
             
             // Center line
             renderer.drawLine(NUIPoint(waveBounds.x, midY), NUIPoint(waveBounds.right(), midY), 1.0f, theme.getColor("border").withAlpha(0.3f));
             
             // Purple Waveform
             NUIColor waveColor(0.6f, 0.3f, 0.9f, 1.0f); 
             
             for (size_t i = 0; i < waveformData_.size(); ++i) {
                 float val = waveformData_[i]; 
                 float h = val * (waveBounds.height * 0.45f); 
                 float x = waveBounds.x + i * stepX;
                 renderer.drawLine(NUIPoint(x, midY - h), NUIPoint(x, midY + h), 2.0f, waveColor);
             }

             // --- PLAYHEAD ---
             if (isPlaying_ && loadedDurationSeconds_ > 0.0) {
                 float playheadPct = static_cast<float>(playheadPosition_ / loadedDurationSeconds_);
                 float playheadX = waveBounds.x + std::clamp(playheadPct, 0.0f, 1.0f) * waveBounds.width;
                 
                 // Draw playhead line (bright accent color)
                 renderer.drawLine(
                     NUIPoint(playheadX, waveBounds.y),
                     NUIPoint(playheadX, waveBounds.bottom()),
                     2.0f,
                     theme.getColor("accentLime")
                 );
             }
         }
    }
    
    // 3. Metadata (Bottom)
    std::string metaText;
    if (loadedSampleRate_ > 0) {
        int totalSec = static_cast<int>(loadedDurationSeconds_);
        int min = totalSec / 60;
        int sec = totalSec % 60;
        char durBuf[16];
        snprintf(durBuf, sizeof(durBuf), "%d:%02d", min, sec);
        
        std::string chans = (loadedChannels_ == 1) ? "Mono" : (loadedChannels_ == 2) ? "Stereo" : (std::to_string(loadedChannels_) + " Ch");
        metaText = std::string(durBuf) + " • " + std::to_string(loadedSampleRate_) + " Hz • " + chans;
    }

    if (!metaText.empty()) {
        float metaY = bounds.y + bounds.height - 18.0f;
        renderer.drawText(metaText, NUIPoint(bounds.x + 12, metaY), theme.getFontSize("xs"), theme.getColor("textSecondary"));
    }
}

bool FilePreviewPanel::onMouseEvent(const NUIMouseEvent& event) {
    if (!currentFile_ || currentFile_->isDirectory) return false;
    
    // Hit test
    if (!getBounds().contains(event.position)) return false;

    // Handle Left Mouse Interaction (Pressed = Click or Drag)
    if (event.pressed && event.button == NUIMouseButton::Left) {
        
        // 1. Play Button Logic (Top Right)
        if (playButtonBounds_.contains(event.position)) {
             // Avoid repeated triggers if dragging over button by assuming simple press logic covers it.
             // (Ideally check !oldPressed && newPressed, but event model suggests this is fine)
             if (isPlaying_) {
                 if (onStop_) onStop_();
             } else {
                 if (onPlay_) onPlay_(*currentFile_);
             }
             return true;
        }

        // 2. Waveform Scrubbing
        // Only if we have valid duration
        if (loadedDurationSeconds_ > 0.0) {
            // Recompute layout rects to be sure
            float topRowHeight = 32;
            float metaHeight = 20.0f;
            float waveformY = getBounds().y + 6 + topRowHeight + 4;
            float waveformHeight = getBounds().height - 6 - topRowHeight - metaHeight - 10;
            NUIRect waveRect(getBounds().x + 8, waveformY, getBounds().width - 16, waveformHeight);
            
            if (waveRect.contains(event.position)) {
                float relativeX = event.position.x - waveRect.x;
                double pct = std::clamp(relativeX / waveRect.width, 0.0f, 1.0f);
                double seekTime = pct * loadedDurationSeconds_;
                
                if (onSeek_) {
                     onSeek_(seekTime);
                }
                return true; 
            }
        }
    }
    
    return false;
}
    


} // namespace NomadUI
