// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "FilePreviewPanel.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <iostream>

#include "../NomadAudio/include/MiniAudioDecoder.h"
#include "../NomadAudio/include/AudioFileValidator.h"

namespace NomadUI {

namespace {

// Generate waveform overview from decoded audio samples
std::vector<float> generateWaveformFromAudio(const std::vector<float>& samples, 
                                              uint32_t numChannels, 
                                              size_t targetSize = 256) {
    std::vector<float> waveform(targetSize, 0.0f);
    if (samples.empty() || numChannels == 0) return waveform;
    
    size_t totalFrames = samples.size() / numChannels;
    float framesPerBin = static_cast<float>(totalFrames) / targetSize;
    
    for (size_t bin = 0; bin < targetSize; ++bin) {
        size_t startFrame = static_cast<size_t>(bin * framesPerBin);
        size_t endFrame = static_cast<size_t>((bin + 1) * framesPerBin);
        endFrame = std::min(endFrame, totalFrames);
        
        float maxAmp = 0.0f;
        for (size_t frame = startFrame; frame < endFrame; ++frame) {
            // Mix all channels for mono-sum peak
            float sum = 0.0f;
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                sum += std::abs(samples[frame * numChannels + ch]);
            }
            maxAmp = std::max(maxAmp, sum / numChannels);
        }
        waveform[bin] = std::min(1.0f, maxAmp);
    }
    
    return waveform;
}

} // namespace

FilePreviewPanel::FilePreviewPanel() {
    setId("FilePreviewPanel");

    // Initialize SVG Icons
    // Folder Icon (Material Design)
    folderIcon_ = std::make_shared<NUIIcon>("<svg viewBox='0 0 24 24'><path d='M10 4H4c-1.1 0-1.99.9-1.99 2L2 18c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z'/></svg>");
    
    // File Icon (Text Snippet style)
    fileIcon_ = std::make_shared<NUIIcon>("<svg viewBox='0 0 24 24'><path d='M14 2H6c-1.1 0-1.99.9-1.99 2L4 20c0 1.1.89 2 1.99 2H18c1.1 0 2-.9 2-2V8l-6-6zm2 16H8v-2h8v2zm0-4H8v-2h8v2zm-3-5V3.5L18.5 9H13z'/></svg>");
}

void FilePreviewPanel::setFile(const FileItem* file) {
    currentFile_ = file;
    {
        std::lock_guard<std::mutex> lock(waveformMutex_);
        waveformData_.clear();
    }
    
    // Increment generation to invalidate pending tasks
    currentGeneration_++;

    if (currentFile_ && !currentFile_->isDirectory) {
        std::string ext = std::filesystem::path(currentFile_->path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        if (ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".ogg" || 
            ext == ".aif" || ext == ".aiff" || ext == ".m4a" || ext == ".mp4") {
            generateWaveform(currentFile_->path, currentFile_->size);
        }
    }
    setDirty(true);
}

void FilePreviewPanel::clear() {
    currentFile_ = nullptr;
    {
        std::lock_guard<std::mutex> lock(waveformMutex_);
        waveformData_.clear();
    }
    currentGeneration_++;
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

void FilePreviewPanel::setPlayheadPosition(double seconds) {
    if (std::abs(playheadPosition_ - seconds) > 0.01) {
        playheadPosition_ = seconds;
        setDirty(true);
    }
}

void FilePreviewPanel::setDuration(double seconds) {
    if (std::abs(duration_ - seconds) > 0.01) {
        duration_ = seconds;
        setDirty(true);
    }
}

void FilePreviewPanel::generateWaveform(const std::string& path, size_t fileSize) {
    isLoading_ = true;
    loadingAnimationTime_ = 0.0f;
    uint64_t gen = currentGeneration_.load();

    std::thread([this, path, gen]() {
        waveformWorker(path, gen);
    }).detach();
}

void FilePreviewPanel::waveformWorker(const std::string& path, uint64_t generation) {
    // Check cancellation early
    if (generation != currentGeneration_.load(std::memory_order_acquire)) return;

    std::vector<float> audioData;
    uint32_t sampleRate = 0;
    uint32_t numChannels = 0;

    // Decode audio file (blocking)
    bool success = Nomad::Audio::decodeAudioFile(path, audioData, sampleRate, numChannels);

    // Check cancellation after decode
    if (generation != currentGeneration_.load(std::memory_order_acquire)) return;

    if (success && !audioData.empty()) {
        // Generate visualization data
        std::vector<float> waveform = generateWaveformFromAudio(audioData, numChannels, 1024);
        
        std::lock_guard<std::mutex> lock(waveformMutex_);
        if (generation == currentGeneration_.load(std::memory_order_acquire)) {
            waveformData_ = std::move(waveform);
            isLoading_ = false;
        }
    } else {
        std::lock_guard<std::mutex> lock(waveformMutex_);
        if (generation == currentGeneration_.load(std::memory_order_acquire)) {
            isLoading_ = false;
        }
    }
}

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
        
        // Define layout
        float iconSize = 32.0f;
        float padding = 12.0f;
        float startX = 20.0f; // Left margin
        
        // Folder Icon (SVG)
        if (folderIcon_) {
            // Icon on the left
            NUIRect iconRect(bounds.x + startX, centerY - iconSize * 0.5f, iconSize, iconSize);
            folderIcon_->setBounds(iconRect);
            
            // Purple Color (Matching sidebar selection)
            // Using a vibrant purple accent
            NUIColor purpleAccent(0.6f, 0.3f, 0.9f, 1.0f);
            folderIcon_->setColor(purpleAccent);
            
            folderIcon_->onRender(renderer);
        }
        
        // Text Position (Right of icon)
        float textX = bounds.x + startX + iconSize + padding;
        float textMaxWidth = bounds.width - (startX + iconSize + padding + 10.0f);
        
        // Vertical Alignment Calculation
        // Name (14px) + Gap (4px) + Hint (11px) = 29px Total Height
        float totalTextHeight = 14.0f + 4.0f + 11.0f;
        float textStartY = centerY - (totalTextHeight * 0.5f);

        // Folder Name
        std::string name = currentFile_->name;
        // Truncate if needed
        float nameWidth = renderer.measureText(name, 14.0f).width;
        if (nameWidth > textMaxWidth) {
             if (name.length() > 25) {
                 name = name.substr(0, 22) + "...";
             }
        }
        
        renderer.drawText(name, NUIPoint(textX, textStartY), 14.0f, theme.getColor("textPrimary"));
        
        // Hint / Info
        std::string hint = "Folder";
        renderer.drawText(hint, NUIPoint(textX, textStartY + 14.0f + 4.0f), 11.0f, theme.getColor("textSecondary"));
        
        return;
    }
    
    // === VERTICAL LAYOUT (Audio File) ===
    // Top row: [Info] ............. [Play Button]
    // Bottom row: [======== Waveform ========]
    
    float topRowY = bounds.y + 6;
    float topRowHeight = 32;
    float waveformY = topRowY + topRowHeight + 4;
    float waveformHeight = bounds.height - topRowHeight - 14; // spacing
    
    // === TOP ROW: Info (Left) + Play (Right) ===
    float infoX = bounds.x + 10;
    float playBtnWidth = 32.0f;
    float playX = bounds.x + bounds.width - playBtnWidth - 10;
    
    // File name (truncated)
    std::string displayName = currentFile_->name;
    if (displayName.length() > 25) {
        displayName = displayName.substr(0, 22) + "...";
    }
    renderer.drawText(displayName, NUIPoint(infoX, topRowY + 2), 11.0f, theme.getColor("textPrimary"));
    
    // File size + extension on same line
    std::string sizeStr;
    if (currentFile_->size < 1024) {
        sizeStr = std::to_string(currentFile_->size) + " B";
    } else if (currentFile_->size < 1024 * 1024) {
        sizeStr = std::to_string(currentFile_->size / 1024) + " KB";
    } else {
        sizeStr = std::to_string(currentFile_->size / (1024 * 1024)) + " MB";
    }
    
    std::string ext = std::filesystem::path(currentFile_->path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    
    std::string meta = sizeStr + " • " + ext;
    renderer.drawText(meta, NUIPoint(infoX, topRowY + 16), 9.0f, theme.getColor("textSecondary"));
    
    // Play Button (Right side of top row)
    playButtonBounds_ = NUIRect(playX, topRowY + 2, playBtnWidth, 26);
    
    NUIColor btnColor = isPlaying_ ? theme.getColor("accentLime") : theme.getColor("primary");
    renderer.fillRoundedRect(playButtonBounds_, 4.0f, btnColor.withAlpha(0.3f));
    
    std::string iconStr = isPlaying_ ? "■" : "▶";
    renderer.drawText(iconStr, NUIPoint(playButtonBounds_.x + 10, playButtonBounds_.y + 5), 14.0f, btnColor);
    
    // === BOTTOM ROW: Waveform (Full Width) ===
    NUIRect waveformBounds(bounds.x + 8, waveformY, bounds.width - 16, waveformHeight);
    
    // Waveform background
    renderer.fillRoundedRect(waveformBounds, 4.0f, theme.getColor("waveformBackground"));
    
    // Draw waveform or loading state
    // Draw waveform or loading state
    bool loading = false;
    bool hasData = false;
    {
        std::lock_guard<std::mutex> lock(waveformMutex_);
        loading = isLoading_;
        hasData = !waveformData_.empty();
    }

    if (loading) {
        // === LOADING SPINNER ===
        float centerX = waveformBounds.x + waveformBounds.width * 0.5f;
        float centerY = waveformBounds.y + waveformBounds.height * 0.5f;
        float spinnerRadius = std::min(waveformBounds.width, waveformBounds.height) * 0.3f;
        
        // Animated arc (spinning)
        float angle = loadingAnimationTime_ * 4.0f; // Rotation speed
        int segments = 8;
        for (int i = 0; i < segments; ++i) {
            float segmentAngle = angle + (i * 2.0f * 3.14159f / segments);
            float alpha = (1.0f - static_cast<float>(i) / segments) * 0.8f;
            
            float x1 = centerX + std::cos(segmentAngle) * (spinnerRadius - 3);
            float y1 = centerY + std::sin(segmentAngle) * (spinnerRadius - 3);
            float x2 = centerX + std::cos(segmentAngle) * (spinnerRadius + 3);
            float y2 = centerY + std::sin(segmentAngle) * (spinnerRadius + 3);
            
            renderer.drawLine(
                NUIPoint(x1, y1), NUIPoint(x2, y2),
                2.0f,
                theme.getColor("primary").withAlpha(alpha)
            );
        }
        
    } else if (hasData && waveformBounds.width > 0 && waveformBounds.height > 0) {
        // === WAVEFORM RENDERING ===
        std::lock_guard<std::mutex> lock(waveformMutex_);
        // Double check data is still there
        if (waveformData_.empty()) return;

        NUIColor waveformFill = theme.getColor("waveformFill");
        
        float centerY = waveformBounds.y + waveformBounds.height * 0.5f;
        float maxAmplitude = waveformBounds.height * 0.45f;
        float samplesPerPixel = static_cast<float>(waveformData_.size()) / waveformBounds.width;
        
        if (samplesPerPixel > 0.0f) {
            for (float x = 0; x < waveformBounds.width; x += 1.0f) {
                int startSample = static_cast<int>(x * samplesPerPixel);
                int endSample = static_cast<int>((x + 1.0f) * samplesPerPixel);
                startSample = std::clamp(startSample, 0, (int)waveformData_.size() - 1);
                endSample = std::clamp(endSample, startSample + 1, (int)waveformData_.size());
                
                float amplitude = 0.0f;
                // Find max amplitude in this pixel's range
                for (int i = startSample; i < endSample; ++i) {
                    amplitude = std::max(amplitude, waveformData_[i]);
                }
                
                float barHeight = std::max(1.0f, amplitude * maxAmplitude * 2.0f);
                float yStart = centerY - barHeight * 0.5f;
                
                renderer.drawLine(
                    NUIPoint(waveformBounds.x + x, yStart), 
                    NUIPoint(waveformBounds.x + x, yStart + barHeight), 
                    1.0f, waveformFill
                );
            }
        }

        // === PLAYHEAD RENDERING ===
        if (duration_ > 0.0) {
            float progress = static_cast<float>(playheadPosition_ / duration_);
            progress = std::clamp(progress, 0.0f, 1.0f);
            float playheadX = waveformBounds.x + (progress * waveformBounds.width);
            
            renderer.drawLine(
                NUIPoint(playheadX, waveformBounds.y),
                NUIPoint(playheadX, waveformBounds.y + waveformBounds.height),
                2.0f, theme.getColor("accentLime")
            );
        }
    }
}

bool FilePreviewPanel::onMouseEvent(const NUIMouseEvent& event) {
    if (!currentFile_ || currentFile_->isDirectory) return false;

    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (playButtonBounds_.contains(event.position)) {
            if (isPlaying_) {
                if (onStop_) onStop_();
            } else {
                if (onPlay_) onPlay_(*currentFile_);
            }
            return true;
        }

        // Handle seeking on waveform click
        NUIRect waveformBounds(getBounds().x + 8, getBounds().y + 32 + 4 + 6, getBounds().width - 16, getBounds().height - 32 - 14);
        if (waveformBounds.contains(event.position) && duration_ > 0.0) {
            float relativeX = event.position.x - waveformBounds.x;
            float progress = std::clamp(relativeX / waveformBounds.width, 0.0f, 1.0f);
            double seekTime = progress * duration_;
            
            if (onSeek_) onSeek_(seekTime);
            return true;
        }
    }
    return false;
}

} // namespace NomadUI
