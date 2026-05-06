// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "FilePreviewPanel.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>

#include "MiniAudioDecoder.h"
#include "AudioFileValidator.h"

namespace AestraUI {

namespace {

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

std::string truncateToWidth(NUIRenderer& renderer, const std::string& text, float fontSize, float maxWidth) {
    if (maxWidth <= 0.0f) return "";
    if (renderer.measureText(text, fontSize).width <= maxWidth) {
        return text;
    }

    static const std::string ellipsis = "...";
    if (renderer.measureText(ellipsis, fontSize).width > maxWidth) {
        return "";
    }

    auto trimLastUtf8Codepoint = [](std::string& value) {
        if (value.empty()) {
            return;
        }
        size_t index = value.size();
        do {
            --index;
        } while (index > 0 && (static_cast<unsigned char>(value[index]) & 0xC0u) == 0x80u);
        value.erase(index);
    };

    std::string candidate = text;
    while (!candidate.empty()) {
        trimLastUtf8Codepoint(candidate);
        const std::string attempt = candidate + ellipsis;
        if (renderer.measureText(attempt, fontSize).width <= maxWidth) {
            return attempt;
        }
    }
    return "";
}

} // namespace

FilePreviewPanel::FilePreviewPanel() {
    setId("FilePreviewPanel");

    // Folder Icon (Material Design / Mac Style)
    // Use the same high-quality path as FileBrowser for consistency
    folderIcon_ = std::make_shared<NUIIcon>(R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M20 6h-8l-2-2H4c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2zm-2.06 11L15 10l.94-2H21v9h-3.06z" opacity="0.8"/><path d="M20,6H12L10,4H4A2,2,0,0,0,2,6V18A2,2,0,0,0,4,20H20A2,2,0,0,0,22,18V8A2,2,0,0,0,20,6Z"/></svg>)");

    // File Icon (Text Snippet style)
    fileIcon_ = std::make_shared<NUIIcon>("<svg viewBox='0 0 24 24' fill='currentColor'><path d='M14 2H6c-1.1 0-1.99.9-1.99 2L4 20c0 1.1.89 2 1.99 2H18c1.1 0 2-.9 2-2V8l-6-6zm2 16H8v-2h8v2zm0-4H8v-2h8v2zm-3-5V3.5L18.5 9H13z'/></svg>");
    
    // Play Icon (Solid Triangle)
    playIcon_ = std::make_shared<NUIIcon>("<svg viewBox='0 0 24 24' fill='currentColor'><path d='M8 5v14l11-7z'/></svg>");
    
    // Stop Icon (Solid Square)
    stopIcon_ = std::make_shared<NUIIcon>("<svg viewBox='0 0 24 24' fill='currentColor'><path d='M6 6h12v12H6z'/></svg>");
}

void FilePreviewPanel::setFile(const FileItem* file) {
    hasCurrentFile_ = file != nullptr;
    if (file) {
        currentFile_ = *file;
    } else {
        currentFile_ = FileItem();
    }
    {
        std::lock_guard<std::mutex> lock(waveformMutex_);
        waveformData_.clear();
    }
    
    // Increment generation to invalidate pending tasks
    currentGeneration_++;

    pendingWaveformPath_.clear();
    pendingWaveformFileSize_ = 0;
    pendingWaveformDelay_ = 0.0;
    waveformQueued_ = false;

    // Keep selection cheap: defer waveform generation briefly so quick browse /
    // import flows do not immediately trigger another heavy decode.
    isLoading_ = false;

    if (hasCurrentFile_ && !currentFile_.isDirectory) {
        std::string ext = std::filesystem::path(currentFile_.path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".ogg" ||
            ext == ".aif" || ext == ".aiff" || ext == ".m4a" || ext == ".mp4") {
            pendingWaveformPath_ = currentFile_.path;
            pendingWaveformFileSize_ = currentFile_.size;
            pendingWaveformDelay_ = 0.18;
            waveformQueued_ = true;
        }
    }
    setDirty(true);
}

void FilePreviewPanel::clear() {
    hasCurrentFile_ = false;
    currentFile_ = FileItem();
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

void FilePreviewPanel::onUpdate(double deltaTime) {
    NUIComponent::onUpdate(deltaTime);

    if (!waveformQueued_ || !isVisible() || isLoading_ || pendingWaveformPath_.empty()) {
        return;
    }

    pendingWaveformDelay_ -= deltaTime;
    if (pendingWaveformDelay_ <= 0.0) {
        waveformQueued_ = false;
        generateWaveform(pendingWaveformPath_, pendingWaveformFileSize_);
    }
}

void FilePreviewPanel::generateWaveform(const std::string& path, size_t fileSize) {
    isLoading_ = true;
    loadingAnimationTime_ = 0.0f;
    uint64_t gen = currentGeneration_.load();
    std::weak_ptr<NUIComponent> weakSelf = weak_from_this();

    std::thread([weakSelf, path, gen]() {
        auto self = std::dynamic_pointer_cast<FilePreviewPanel>(weakSelf.lock());
        if (!self) return;

        self->waveformWorker(path, gen);
    }).detach();
}

void FilePreviewPanel::waveformWorker(const std::string& path, uint64_t generation) {
    // Check cancellation early
    if (generation != currentGeneration_.load(std::memory_order_acquire)) return;

    std::vector<float> audioData;
    uint32_t sampleRate = 0;
    uint32_t numChannels = 0;

    // Preview waveform should not pay full import cost. Decode only a bounded
    // sequential chunk for a fast approximate overview.
    constexpr uint64_t kPreviewMaxFrames = 48000 * 24;
    bool success = Aestra::Audio::decodeAudioPreview(path, audioData, sampleRate, numChannels, kPreviewMaxFrames);

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
    
    const float cornerRadius = 6.0f;
    const bool hasSelection = hasCurrentFile_;
    NUIColor bgColor = theme.getColor("surfaceRaised");
    NUIColor borderColor = hasSelection
        ? theme.getColor("borderActive").withAlpha(0.30f)
        : theme.getColor("borderSubtle").withAlpha(0.72f);
    NUIColor accent = theme.getColor("accentPrimary").withAlpha(hasSelection ? 0.34f : 0.24f);

    renderer.fillRect(bounds, bgColor);

    {
        NUIRect topClip = bounds;
        topClip.height = cornerRadius;
        renderer.setClipRect(topClip);

        renderer.fillRect(topClip, bgColor);

        renderer.drawLine(NUIPoint(topClip.x, topClip.y), NUIPoint(topClip.x, topClip.bottom()), 1.0f, borderColor);
        renderer.drawLine(NUIPoint(topClip.right(), topClip.y), NUIPoint(topClip.right(), topClip.bottom()), 1.0f, borderColor);
        renderer.drawLine(NUIPoint(bounds.x, bounds.y), NUIPoint(bounds.right(), bounds.y), 1.0f, borderColor);

        renderer.clearClipRect();
    }

    {
        NUIRect bottomClip = bounds;
        bottomClip.y += cornerRadius;
        bottomClip.height -= cornerRadius;
        renderer.setClipRect(bottomClip);
        renderer.fillRoundedRect(bounds, cornerRadius, bgColor);
        renderer.strokeRoundedRect(bounds, cornerRadius, 1.0f, borderColor);
        renderer.clearClipRect();
    }

    renderer.fillRoundedRect({bounds.x + 10.0f, bounds.y, std::max(0.0f, bounds.width - 20.0f), 1.5f},
                             0.75f,
                             accent);
    
    // === EMPTY STATE ===
    if (!hasCurrentFile_) {
        float centerX = bounds.x + bounds.width * 0.5f;
        float centerY = bounds.y + bounds.height * 0.5f;
        
        float iconSize = 48.0f;
        if (fileIcon_) {
            fileIcon_->setBounds(NUIRect(centerX - iconSize * 0.5f, centerY - iconSize * 0.5f - 15, iconSize, iconSize));
            fileIcon_->setColor(theme.getColor("textSecondary").withAlpha(0.2f));
            fileIcon_->onRender(renderer);
        }

        std::string emptyText = "Select a file to preview";
        float fontSize = 14.0f;
        auto size = renderer.measureText(emptyText, fontSize);
        renderer.drawText(emptyText,
            NUIPoint(centerX - size.width * 0.5f, centerY + 25),
            fontSize, theme.getColor("textSecondary").withAlpha(0.6f));
        return;
    }

    // === FOLDER STATE ===
    if (currentFile_.isDirectory) {
        float centerX = bounds.x + bounds.width * 0.5f;
        float centerY = bounds.y + bounds.height * 0.5f;
        
        float iconSize = 32.0f;
        float padding = 12.0f;
        float startX = 20.0f;

        if (folderIcon_) {
            NUIRect iconRect(bounds.x + startX, centerY - iconSize * 0.5f, iconSize, iconSize);
            folderIcon_->setBounds(iconRect);
            folderIcon_->setColor(theme.getColor("primary"));
            folderIcon_->onRender(renderer);
        }

        float textX = bounds.x + startX + iconSize + padding;
        float textMaxWidth = bounds.width - (startX + iconSize + padding + 10.0f);
        float totalTextHeight = 14.0f + 4.0f + 11.0f;
        float textStartY = centerY - (totalTextHeight * 0.5f);

        std::string name = currentFile_.name;
        float nameWidth = renderer.measureText(name, 14.0f).width;
        if (nameWidth > textMaxWidth) {
             if (name.length() > 25) {
                 name = name.substr(0, 22) + "...";
             }
        }
        
        renderer.drawText(name, NUIPoint(textX, textStartY), 14.0f, theme.getColor("textPrimary"));
        std::string hint = "Folder";
        renderer.drawText(hint, NUIPoint(textX, textStartY + 14.0f + 4.0f), 11.0f, theme.getColor("textSecondary"));
        
        return;
    }
    
    auto formatTime = [](double seconds) {
        if (seconds <= 0.0) {
            return std::string("--:--");
        }
        const int total = static_cast<int>(std::round(seconds));
        const int mins = total / 60;
        const int secs = total % 60;
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%d:%02d", mins, secs);
        return std::string(buffer);
    };

    const float centerY = bounds.y + bounds.height * 0.5f;
    const float playSize = 24.0f;
    playButtonBounds_ = NUIRect(bounds.x + 12.0f, centerY - playSize * 0.5f, playSize, playSize);

    const float textX = playButtonBounds_.right() + 10.0f;
    const float rightPad = 10.0f;
    const float waveformW = std::clamp(bounds.width * 0.20f, 38.0f, 58.0f);
    const float waveformX = bounds.right() - waveformW - rightPad;
    const float durationW = 36.0f;
    const float durationX = waveformX - durationW - 6.0f;
    const bool showDuration = durationX > textX + 40.0f;
    NUIRect waveformBounds(waveformX, bounds.y + 12.0f, waveformW, bounds.height - 24.0f);

    const float textLimitX = showDuration ? durationX : waveformX;
    const float textMaxWidth = std::max(0.0f, textLimitX - textX - 8.0f);
    std::string displayName = truncateToWidth(renderer, currentFile_.name, 11.0f, textMaxWidth);
    renderer.drawText(displayName, NUIPoint(textX, bounds.y + 10.0f), 11.0f, theme.getColor("textPrimary").withAlpha(0.92f));
    
    std::string sizeStr;
    if (currentFile_.size < 1024) {
        sizeStr = std::to_string(currentFile_.size) + " B";
    } else if (currentFile_.size < 1024 * 1024) {
        sizeStr = std::to_string(currentFile_.size / 1024) + " KB";
    } else {
        sizeStr = std::to_string(currentFile_.size / (1024 * 1024)) + " MB";
    }
    
    std::string ext = std::filesystem::path(currentFile_.path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    
    std::string meta = sizeStr + "  " + ext;
    renderer.drawText(meta, NUIPoint(textX, bounds.y + 27.0f), 9.5f, theme.getColor("textSecondary").withAlpha(0.68f));

    NUIColor btnBg = isPlaying_
        ? theme.getColor("accentPrimary").withAlpha(0.82f)
        : theme.getColor("surfaceRaised").withAlpha(0.76f);
    NUIColor btnBorder = isPlaying_
        ? theme.getColor("accentPrimary").withAlpha(0.76f)
        : theme.getColor("border").withAlpha(0.28f);
    renderer.fillRoundedRect(playButtonBounds_, playSize * 0.5f, btnBg);
    renderer.strokeRoundedRect(playButtonBounds_, playSize * 0.5f, 1.0f, btnBorder);
    
    // Icon
    auto& icon = isPlaying_ ? stopIcon_ : playIcon_;
    if (icon) {
        float iconSize = 11.0f;
        // Pixel snap positions
        float iconX = std::floor(playButtonBounds_.x + (playButtonBounds_.width - iconSize) * 0.5f + (isPlaying_ ? 0.0f : 1.0f)); 
        // Nudge Stop icon down 1px
        float iconY = std::floor(playButtonBounds_.y + (playButtonBounds_.height - iconSize) * 0.5f + (isPlaying_ ? 1.0f : 0.0f)); 
        
        icon->setBounds(NUIRect(iconX, iconY, iconSize, iconSize));
        icon->setColor(theme.getColor("textPrimary"));
        icon->onRender(renderer);
    }

    if (showDuration) {
        renderer.drawText(formatTime(duration_), NUIPoint(durationX, bounds.y + 20.0f), 9.5f,
                          theme.getColor("textSecondary").withAlpha(0.62f));
    }

    renderer.fillRoundedRect(waveformBounds, 4.0f, theme.getColor("backgroundPrimary").withAlpha(0.14f));
    
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
        std::lock_guard<std::mutex> lock(waveformMutex_);
        if (waveformData_.empty()) return;
        NUIColor waveformFill = theme.getColor("accentPrimary").withAlpha(0.38f);
        
        float centerY = waveformBounds.y + waveformBounds.height * 0.5f;
        float maxAmplitude = waveformBounds.height * 0.45f;
        float samplesPerPixel = static_cast<float>(waveformData_.size()) / waveformBounds.width;
        
        if (samplesPerPixel > 0.0f) {
            for (float x = 0; x < waveformBounds.width; x += 3.0f) {
                int startSample = static_cast<int>(x * samplesPerPixel);
                int endSample = static_cast<int>((x + 3.0f) * samplesPerPixel);
                startSample = std::clamp(startSample, 0, (int)waveformData_.size() - 1);
                endSample = std::clamp(endSample, startSample + 1, (int)waveformData_.size());
                
                float amplitude = 0.0f;
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

        if (duration_ > 0.0) {
            float progress = static_cast<float>(playheadPosition_ / duration_);
            progress = std::clamp(progress, 0.0f, 1.0f);
            float playheadX = waveformBounds.x + (progress * waveformBounds.width);
            
            renderer.drawLine(
                NUIPoint(playheadX, waveformBounds.y),
                NUIPoint(playheadX, waveformBounds.y + waveformBounds.height),
                1.5f, theme.getColor("accentPrimary").withAlpha(0.82f)
            );
        }
    }
}

bool FilePreviewPanel::onMouseEvent(const NUIMouseEvent& event) {
    const auto bounds = getBounds();
    if (!bounds.contains(event.position)) return false;

    if (!hasCurrentFile_ || currentFile_.isDirectory) {
        if (event.pressed && event.button == NUIMouseButton::Right) {
            return true;
        }
        return false;
    }

    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (playButtonBounds_.contains(event.position)) {
            if (isPlaying_) {
                if (onStop_) onStop_();
            } else {
                if (onPlay_) onPlay_(currentFile_);
            }
            return true;
        }

        const float waveformW = std::clamp(bounds.width * 0.20f, 38.0f, 58.0f);
        const float waveformX = bounds.right() - waveformW - 10.0f;
        NUIRect waveformBounds(waveformX, bounds.y + 12.0f, waveformW, bounds.height - 24.0f);
        if (waveformBounds.contains(event.position) && duration_ > 0.0) {
            float relativeX = event.position.x - waveformBounds.x;
            float progress = std::clamp(relativeX / waveformBounds.width, 0.0f, 1.0f);
            double seekTime = progress * duration_;
            
            if (onSeek_) onSeek_(seekTime);
            return true;
        }
    }

    if (event.pressed && event.button == NUIMouseButton::Right) {
        return true;
    }

    return false;
}

} // namespace AestraUI
