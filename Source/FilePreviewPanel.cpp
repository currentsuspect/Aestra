// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "FilePreviewPanel.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <iostream>

namespace NomadUI {

FilePreviewPanel::FilePreviewPanel() {
    setId("FilePreviewPanel");
}

void FilePreviewPanel::setFile(const FileItem* file) {
    currentFile_ = file;
    waveformData_.clear();

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

void FilePreviewPanel::generateWaveform(const std::string& path, size_t fileSize) {
    // Generate placeholder waveform (simple sine pattern for demo)
    // Same logic as was in FileBrowser::selectFile
    waveformData_.resize(256);
    for (size_t s = 0; s < waveformData_.size(); ++s) {
        // Create a pseudo-random pattern based on file size for variety
        float t = static_cast<float>(s) / waveformData_.size();
        float wave = std::sin(t * 20.0f + static_cast<float>(fileSize % 1000) * 0.01f);
        float envelope = std::sin(t * 3.14159f); // Fade in/out
        float noise = std::sin(t * 47.0f + t * 123.0f) * 0.3f; // Add variation
        waveformData_[s] = std::abs(wave * envelope + noise) * 0.8f + 0.1f;
    }
}

void FilePreviewPanel::onRender(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    NUIRect bounds = getBounds();
    
    // Panel background
    renderer.fillRoundedRect(bounds, 6.0f, theme.getColor("surfaceRaised"));
    renderer.strokeRoundedRect(bounds, 6.0f, 1.0f, theme.getColor("borderSubtle"));
    
    if (!currentFile_) return;
    
    // === VERTICAL LAYOUT ===
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
    
    std::string icon = isPlaying_ ? "■" : "▶";
    renderer.drawText(icon, NUIPoint(playButtonBounds_.x + 10, playButtonBounds_.y + 5), 14.0f, btnColor);
    
    // === BOTTOM ROW: Waveform (Full Width) ===
    NUIRect waveformBounds(bounds.x + 8, waveformY, bounds.width - 16, waveformHeight);
    
    // Waveform background
    renderer.fillRoundedRect(waveformBounds, 4.0f, theme.getColor("waveformBackground"));
    
    // Draw waveform or loading state
    if (isLoading_) {
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
        
    } else if (!waveformData_.empty() && waveformBounds.width > 0 && waveformBounds.height > 0) {
        // === WAVEFORM RENDERING ===
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
    }
}

bool FilePreviewPanel::onMouseEvent(const NUIMouseEvent& event) {
    if (!currentFile_) return false;

    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (playButtonBounds_.contains(event.position)) {
            if (isPlaying_) {
                if (onStop_) onStop_();
            } else {
                if (onPlay_) onPlay_(*currentFile_);
            }
            return true;
        }
    }
    return false;
}

} // namespace NomadUI
