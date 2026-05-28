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

std::string formatTimeShort(double seconds) {
    if (seconds <= 0.0) {
        return "0:00";
    }
    const int total = static_cast<int>(std::round(seconds));
    const int mins = total / 60;
    const int secs = total % 60;
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d:%02d", mins, secs);
    return std::string(buffer);
}

std::string fileExtensionUpper(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    return ext;
}

} // namespace

FilePreviewPanel::FilePreviewPanel() {
    setId("FilePreviewPanel");

    folderIcon_ = std::make_shared<NUIIcon>(R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M20 6h-8l-2-2H4c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2zm-2.06 11L15 10l.94-2H21v9h-3.06z" opacity="0.8"/><path d="M20,6H12L10,4H4A2,2,0,0,0,2,6V18A2,2,0,0,0,4,20H20A2,2,0,0,0,22,18V8A2,2,0,0,0,20,6Z"/></svg>)");

    fileIcon_ = std::make_shared<NUIIcon>("<svg viewBox='0 0 24 24' fill='currentColor'><path d='M14 2H6c-1.1 0-1.99.9-1.99 2L4 20c0 1.1.89 2 1.99 2H18c1.1 0 2-.9 2-2V8l-6-6zm2 16H8v-2h8v2zm0-4H8v-2h8v2zm-3-5V3.5L18.5 9H13z'/></svg>");

    audioFileIcon_ = std::make_shared<NUIIcon>("<svg viewBox='0 0 24 24' fill='currentColor'><path d='M12 3v10.55c-.59-.34-1.27-.55-2-.55-2.21 0-4 1.79-4 4s1.79 4 4 4 4-1.79 4-4V7h4V3h-6z'/></svg>");

    playIcon_ = std::make_shared<NUIIcon>("<svg viewBox='0 0 24 24' fill='currentColor'><path d='M8 5v14l11-7z'/></svg>");

    stopIcon_ = std::make_shared<NUIIcon>("<svg viewBox='0 0 24 24' fill='currentColor'><path d='M6 6h12v12H6z'/></svg>");

    loopIcon_ = std::make_shared<NUIIcon>("<svg viewBox='0 0 24 24' fill='currentColor'><path d='M12 4V1L8 5l4 4V6c3.31 0 6 2.69 6 6 0 1.01-.25 1.97-.7 2.8l1.46 1.46A7.93 7.93 0 0020 12c0-4.42-3.58-8-8-8zm0 14c-3.31 0-6-2.69-6-6 0-1.01.25-1.97.7-2.8L5.24 7.74A7.93 7.93 0 004 12c0 4.42 3.58 8 8 8v3l4-4-4-4v3z'/></svg>");

    bpmSyncIcon_ = std::make_shared<NUIIcon>("<svg viewBox='0 0 24 24' fill='currentColor'><path d='M12 3v10.55c-.59-.34-1.27-.55-2-.55-2.21 0-4 1.79-4 4s1.79 4 4 4 4-1.79 4-4V7h4V3h-6z'/></svg>");
}

void FilePreviewPanel::setFile(const FileItem* file) {
    hasCurrentFile_ = file != nullptr;
    m_currentFilePath.clear();
    m_currentFileBpm = 0;
    if (file) {
        currentFile_ = *file;
        m_currentFilePath = file->path;
        m_currentFileBpm = file->detectedBpm;
    } else {
        currentFile_ = FileItem();
    }
    {
        std::lock_guard<std::mutex> lock(waveformMutex_);
        waveformData_.clear();
    }

    currentGeneration_++;

    pendingWaveformPath_.clear();
    pendingWaveformFileSize_ = 0;
    pendingWaveformDelay_ = 0.0;
    waveformQueued_ = false;

    isLoading_ = false;
    isWaveformLoading_ = false;

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

void FilePreviewPanel::onPreviewEnded() {
    if (m_loopEnabled && !m_currentFilePath.empty()) {
        if (onReplay_) onReplay_();
    } else {
        isPlaying_ = false;
        playheadPosition_ = 0.0;
        setDirty(true);
    }
}

float FilePreviewPanel::getRequiredHeight() const {
    return getBounds().height + kTransportBarHeight;
}

void FilePreviewPanel::onUpdate(double deltaTime) {
    NUIComponent::onUpdate(deltaTime);

    if (waveformJustCompleted_.exchange(false)) {
        setDirty(true);
    }

    if (isLoading_ || isWaveformLoading_) {
        loadingAnimationTime_ += static_cast<float>(deltaTime);
        setDirty(true);
    }

    if (!waveformQueued_ || !isVisible() || isWaveformLoading_ || pendingWaveformPath_.empty()) {
        return;
    }

    pendingWaveformDelay_ -= deltaTime;
    if (pendingWaveformDelay_ <= 0.0) {
        waveformQueued_ = false;
        generateWaveform(pendingWaveformPath_, pendingWaveformFileSize_);
    }
}

void FilePreviewPanel::generateWaveform(const std::string& path, size_t fileSize) {
    isWaveformLoading_ = true;
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
    if (generation != currentGeneration_.load(std::memory_order_acquire)) return;

    std::vector<float> audioData;
    uint32_t sampleRate = 0;
    uint32_t numChannels = 0;

    constexpr uint64_t kPreviewMaxFrames = 48000 * 24;
    constexpr double kPreviewMaxSeconds = static_cast<double>(kPreviewMaxFrames) / 48000.0;
    bool success = Aestra::Audio::decodeAudioPreview(path, audioData, sampleRate, numChannels, kPreviewMaxSeconds);

    if (generation != currentGeneration_.load(std::memory_order_acquire)) return;

    if (success && !audioData.empty()) {
        std::vector<float> waveform = generateWaveformFromAudio(audioData, numChannels, 1024);

        std::lock_guard<std::mutex> lock(waveformMutex_);
        if (generation == currentGeneration_.load(std::memory_order_acquire)) {
            waveformData_ = std::move(waveform);
            isWaveformLoading_ = false;
            waveformJustCompleted_.store(true);
        }
    } else {
        std::lock_guard<std::mutex> lock(waveformMutex_);
        if (generation == currentGeneration_.load(std::memory_order_acquire)) {
            isWaveformLoading_ = false;
            waveformJustCompleted_.store(true);
        }
    }
}

void FilePreviewPanel::onRender(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    NUIRect bounds = getBounds();
    if (bounds.isEmpty()) return;

    const bool hasSelection = hasCurrentFile_;
    NUIColor bgColor = theme.getColor("backgroundSecondary").darkened(0.03f);
    NUIColor borderColor = theme.getColor("border").withAlpha(0.38f);
    NUIColor accent = theme.getColor("accentPrimary");

    // Background fill
    renderer.fillRect(bounds, bgColor);
    // Top separator line
    renderer.drawLine(NUIPoint(bounds.x, bounds.y), NUIPoint(bounds.right(), bounds.y), 1.0f, borderColor);

    // === EMPTY STATE ===
    if (!hasSelection) {
        float centerX = bounds.x + bounds.width * 0.5f;
        float centerY = bounds.y + bounds.height * 0.5f;

        float iconSize = 32.0f;
        if (fileIcon_) {
            fileIcon_->setBounds(NUIRect(centerX - iconSize * 0.5f, centerY - iconSize * 0.5f - 10.0f, iconSize, iconSize));
            fileIcon_->setColor(theme.getColor("textSecondary").withAlpha(0.18f));
            fileIcon_->onRender(renderer);
        }

        std::string emptyText = "Select a file to preview";
        float fontSize = 12.0f;
        auto size = renderer.measureText(emptyText, fontSize);
        renderer.drawText(emptyText,
            NUIPoint(centerX - size.width * 0.5f, centerY + 18.0f),
            fontSize, theme.getColor("textSecondary").withAlpha(0.42f));
        return;
    }

    // === FOLDER STATE ===
    if (currentFile_.isDirectory) {
        float iconSize = 26.0f;
        float padding = 14.0f;
        float startX = bounds.x + 16.0f;
        float centerY = bounds.y + bounds.height * 0.5f;

        if (folderIcon_) {
            NUIRect iconRect(startX, centerY - iconSize * 0.5f, iconSize, iconSize);
            folderIcon_->setBounds(iconRect);
            folderIcon_->setColor(accent.withAlpha(0.75f));
            folderIcon_->onRender(renderer);
        }

        float textX = startX + iconSize + padding;
        float textMaxWidth = bounds.width - (startX + iconSize + padding + 20.0f);
        float totalTextHeight = 14.0f + 3.0f + 10.0f;
        float textStartY = centerY - (totalTextHeight * 0.5f);

        std::string name = currentFile_.name;
        if (name.length() > 35) {
            name = name.substr(0, 32) + "...";
        }

        renderer.drawText(name, NUIPoint(textX, textStartY + 2.0f), 13.0f, theme.getColor("textPrimary").withAlpha(0.88f));
        renderer.drawText("Folder", NUIPoint(textX, textStartY + 14.0f + 5.0f), 10.0f, theme.getColor("textSecondary").withAlpha(0.55f));
        return;
    }

    // === AUDIO FILE STATE ===
    const float padL = 14.0f;
    const float padR = 14.0f;
    const float transportH = 18.0f;
    const float scrubberH = 3.0f;
    const float padB = 8.0f; // bottom padding below scrubber
    const float playBtnSize = 30.0f;
    const float playBtnX = bounds.x + padL;
    const float centerY = bounds.y + bounds.height * 0.5f;

    // Play button bounds
    playButtonBounds_ = NUIRect(playBtnX, centerY - playBtnSize * 0.5f, playBtnSize, playBtnSize);

    // Transport bar positioned above scrubber
    const float transportY = bounds.bottom() - padB - scrubberH - transportH - 2.0f;

    // Scrubber bounds (full width minus padding, at bottom)
    scrubberBounds_ = NUIRect(bounds.x + padL, bounds.bottom() - padB - scrubberH,
                              std::max(0.0f, bounds.width - padL - padR), scrubberH);

    // Layout columns
    const float textX = playButtonBounds_.right() + 12.0f;
    const float timeW = 72.0f; // enough for "0:00 / 0:00"
    const float timeX = bounds.right() - padR - timeW;
    const float textMaxWidth = std::max(0.0f, timeX - textX - 10.0f);

    // -- Background waveform (full-width, subtle) --
    bool hasData = false;
    {
        std::lock_guard<std::mutex> lock(waveformMutex_);
        hasData = !waveformData_.empty();
    }

    if (hasData) {
        std::lock_guard<std::mutex> lock(waveformMutex_);
        if (!waveformData_.empty()) {
            NUIColor waveformFill = accent.withAlpha(0.07f);
            float wfY = bounds.y + bounds.height * 0.5f;
            float maxAmp = bounds.height * 0.32f;
            float samplesPerPixel = static_cast<float>(waveformData_.size()) / bounds.width;

            if (samplesPerPixel > 0.0f) {
                for (float x = 0; x < bounds.width; x += 2.0f) {
                    int startSample = static_cast<int>(x * samplesPerPixel);
                    int endSample = static_cast<int>((x + 2.0f) * samplesPerPixel);
                    startSample = std::clamp(startSample, 0, (int)waveformData_.size() - 1);
                    endSample = std::clamp(endSample, startSample + 1, (int)waveformData_.size());

                    float amplitude = 0.0f;
                    for (int i = startSample; i < endSample; ++i) {
                        amplitude = std::max(amplitude, waveformData_[i]);
                    }

                    float barHeight = std::max(1.0f, amplitude * maxAmp * 2.0f);
                    float yStart = wfY - barHeight * 0.5f;
                    renderer.drawLine(
                        NUIPoint(bounds.x + x, yStart),
                        NUIPoint(bounds.x + x, yStart + barHeight),
                        1.0f, waveformFill
                    );
                }
            }
        }
    }

    // -- Play button --
    NUIColor btnBg = isPlaying_
        ? accent.withAlpha(0.85f)
        : bgColor;
    NUIColor btnBorder = isPlaying_
        ? accent.withAlpha(0.20f)
        : theme.getColor("border").withAlpha(0.35f);
    if (!isPlaying_) {
        // Subtle hover ring could be added here if hover state tracked
        btnBorder = theme.getColor("border").withAlpha(0.45f);
    }

    renderer.fillRoundedRect(playButtonBounds_, playBtnSize * 0.5f, btnBg);
    renderer.strokeRoundedRect(playButtonBounds_, playBtnSize * 0.5f, 1.0f, btnBorder);

    auto& icon = isPlaying_ ? stopIcon_ : playIcon_;
    if (icon) {
        float iconSize = 13.0f;
        float iconX = std::floor(playButtonBounds_.x + (playButtonBounds_.width - iconSize) * 0.5f);
        float iconY = std::floor(playButtonBounds_.y + (playButtonBounds_.height - iconSize) * 0.5f);

        icon->setBounds(NUIRect(iconX, iconY, iconSize, iconSize));
        icon->setColor(isPlaying_ ? NUIColor::white() : theme.getColor("textPrimary").withAlpha(0.85f));
        icon->onRender(renderer);
    }

    // -- File info --
    // Audio icon (small, left of name)
    float iconSizeSmall = 14.0f;
    float infoTopY = bounds.y + 16.0f;
    if (audioFileIcon_) {
        audioFileIcon_->setBounds(NUIRect(textX, infoTopY - 1.0f, iconSizeSmall, iconSizeSmall));
        audioFileIcon_->setColor(accent.withAlpha(0.65f));
        audioFileIcon_->onRender(renderer);
    }

    float nameX = textX + iconSizeSmall + 6.0f;
    float nameMaxW = std::max(0.0f, textMaxWidth - iconSizeSmall - 6.0f);
    std::string displayName = truncateToWidth(renderer, currentFile_.name, 12.5f, nameMaxW);
    renderer.drawText(displayName, NUIPoint(nameX, infoTopY), 12.5f, theme.getColor("textPrimary").withAlpha(0.92f));

    // BPM and key info line
    std::string infoLine;
    if (m_currentFileBpm > 0) {
        infoLine = std::to_string(m_currentFileBpm) + " BPM";
    }
    if (!m_currentFileKey.empty()) {
        if (!infoLine.empty()) infoLine += "  ";
        infoLine += m_currentFileKey;
    }
    if (infoLine.empty()) {
        infoLine = fileExtensionUpper(currentFile_.path);
    }
    renderer.drawText(infoLine, NUIPoint(nameX, infoTopY + 15.0f), 10.0f, theme.getColor("textSecondary").withAlpha(0.55f));

    // -- Time / Duration --
    if (timeX > nameX + 40.0f) {
        std::string timeStr;
        if (duration_ > 0.0 && isPlaying_) {
            timeStr = formatTimeShort(playheadPosition_) + " / " + formatTimeShort(duration_);
        } else if (duration_ > 0.0) {
            timeStr = formatTimeShort(duration_);
        } else {
            timeStr = "--:--";
        }
        auto timeSize = renderer.measureText(timeStr, 10.0f);
        renderer.drawText(timeStr,
            NUIPoint(timeX + timeW - timeSize.width, infoTopY + 7.0f),
            10.0f, theme.getColor("textSecondary").withAlpha(0.55f));
    }

    // -- Scrubber / Progress bar --
    float progress = 0.0f;
    if (duration_ > 0.0) {
        progress = static_cast<float>(playheadPosition_ / duration_);
        progress = std::clamp(progress, 0.0f, 1.0f);
    }

    float trackY = scrubberBounds_.y + scrubberBounds_.height * 0.5f;

    // Track background
    renderer.fillRoundedRect(scrubberBounds_, scrubberH * 0.5f, theme.getColor("border").withAlpha(0.22f));

    // Progress fill
    float fillW = scrubberBounds_.width * progress;
    if (fillW > 0.5f) {
        NUIRect fillRect(scrubberBounds_.x, scrubberBounds_.y, fillW, scrubberBounds_.height);
        renderer.fillRoundedRect(fillRect, scrubberH * 0.5f, accent.withAlpha(0.78f));
    }

    // Playhead indicator
    if (duration_ > 0.0) {
        float playheadX = scrubberBounds_.x + progress * scrubberBounds_.width;
        float indicatorH = 7.0f;
        renderer.drawLine(
            NUIPoint(playheadX, trackY - indicatorH * 0.5f),
            NUIPoint(playheadX, trackY + indicatorH * 0.5f),
            2.0f, accent.withAlpha(0.92f)
        );
    }

    // -- Transport bar (above scrubber) --
    const float btnSize = 16.0f;
    float tx = bounds.x + padL;

    // Loop button
    loopButtonBounds_ = NUIRect(tx, transportY, btnSize, btnSize);
    if (m_loopEnabled) {
        renderer.fillRoundedRect(loopButtonBounds_, 3.0f, accent.withAlpha(0.25f));
        renderer.strokeRoundedRect(loopButtonBounds_, 3.0f, 1.0f, accent.withAlpha(0.5f));
    } else {
        renderer.strokeRoundedRect(loopButtonBounds_, 3.0f, 1.0f, theme.getColor("border").withAlpha(0.35f));
    }
    if (loopIcon_) {
        loopIcon_->setBounds(NUIRect(tx + 1.5f, transportY + 1.5f, 13.0f, 13.0f));
        loopIcon_->setColor(m_loopEnabled ? accent : theme.getColor("textSecondary").withAlpha(0.5f));
        loopIcon_->onRender(renderer);
    }
    tx += btnSize + 8.0f;

    // BPM sync button
    bpmSyncButtonBounds_ = NUIRect(tx, transportY, btnSize, btnSize);
    if (m_bpmSyncEnabled) {
        renderer.fillRoundedRect(bpmSyncButtonBounds_, 3.0f, NUIColor(0.204f, 0.835f, 0.600f, 0.25f));
        renderer.strokeRoundedRect(bpmSyncButtonBounds_, 3.0f, 1.0f, NUIColor(0.204f, 0.835f, 0.600f, 0.5f));
    } else {
        renderer.strokeRoundedRect(bpmSyncButtonBounds_, 3.0f, 1.0f, theme.getColor("border").withAlpha(0.35f));
    }
    if (bpmSyncIcon_) {
        bpmSyncIcon_->setBounds(NUIRect(tx + 1.5f, transportY + 1.5f, 13.0f, 13.0f));
        bpmSyncIcon_->setColor(m_bpmSyncEnabled ? NUIColor(0.204f, 0.835f, 0.600f, 1.0f) : theme.getColor("textSecondary").withAlpha(0.5f));
        bpmSyncIcon_->onRender(renderer);
    }
    tx += btnSize + 12.0f;

    // Time display
    std::string timeStr = formatTimeShort(playheadPosition_) + " / " + formatTimeShort(duration_);
    renderer.drawText(timeStr, {tx, transportY + 2.0f}, 10.0f, theme.getColor("textSecondary").withAlpha(0.45f));

    // BPM display (right-aligned)
    if (m_currentFileBpm > 0) {
        float rate = 1.0f;
        if (m_bpmSyncEnabled && m_projectBpm > 0) {
            rate = std::clamp(static_cast<float>(m_projectBpm) / static_cast<float>(m_currentFileBpm), 0.5f, 2.0f);
        }
        char bpmBuf[32];
        std::snprintf(bpmBuf, sizeof(bpmBuf), "%d BPM", m_currentFileBpm);
        std::string bpmStr = bpmBuf;
        if (m_bpmSyncEnabled && m_projectBpm > 0) {
            char rateBuf[16];
            std::snprintf(rateBuf, sizeof(rateBuf), "  x%.2f", rate);
            bpmStr += rateBuf;
        }
        auto bpmSize = renderer.measureText(bpmStr, 10.0f);
        renderer.drawText(bpmStr, {bounds.right() - padR - bpmSize.width, transportY + 2.0f}, 10.0f,
                          m_bpmSyncEnabled ? NUIColor(0.204f, 0.835f, 0.600f, 0.9f) : theme.getColor("textSecondary").withAlpha(0.35f));
    }

    // -- Loading spinner overlay (small, near play button) --
    bool loading = isLoading_ || isWaveformLoading_;

    if (loading) {
        float spinnerCX = bounds.right() - 28.0f;
        float spinnerCY = infoTopY + 6.0f;
        float spinnerR = 6.0f;
        int segments = 8;
        float angleOffset = loadingAnimationTime_ * 5.0f;
        for (int i = 0; i < segments; ++i) {
            float a = angleOffset + (i * 2.0f * 3.14159265f / segments);
            float alpha = (1.0f - static_cast<float>(i) / segments) * 0.8f;
            float x1 = spinnerCX + std::cos(a) * (spinnerR - 2.0f);
            float y1 = spinnerCY + std::sin(a) * (spinnerR - 2.0f);
            float x2 = spinnerCX + std::cos(a) * (spinnerR + 2.0f);
            float y2 = spinnerCY + std::sin(a) * (spinnerR + 2.0f);
            renderer.drawLine(NUIPoint(x1, y1), NUIPoint(x2, y2), 1.8f, accent.withAlpha(alpha));
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

    if (event.type == NUIMouseEventType::Move) {
        if (stopButtonBounds_.contains(event.position)) {
            showRemoteTooltip("Stop preview", event.position, this);
        } else if (loopButtonBounds_.contains(event.position)) {
            showRemoteTooltip(m_loopEnabled ? "Loop: On" : "Loop: Off", event.position, this);
        } else if (bpmSyncButtonBounds_.contains(event.position)) {
            showRemoteTooltip(m_bpmSyncEnabled ? "BPM Sync: On" : "BPM Sync: Off", event.position, this);
        }
    }

    auto seekFromPosition = [&](const NUIPoint& pos) {
        if (duration_ <= 0.0 || scrubberBounds_.width <= 0.0f) return;
        float relativeX = pos.x - scrubberBounds_.x;
        float progress = std::clamp(relativeX / scrubberBounds_.width, 0.0f, 1.0f);
        double seekTime = progress * duration_;
        if (onSeek_) onSeek_(seekTime);
    };

    if (isSeekDragging_) {
        if (event.released) {
            seekFromPosition(event.position);
            isSeekDragging_ = false;
            setDirty(true);
            return true;
        }
        if (event.type == NUIMouseEventType::Drag) {
            seekFromPosition(event.position);
            setDirty(true);
            return true;
        }
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

        if (scrubberBounds_.contains(event.position) && duration_ > 0.0) {
            isSeekDragging_ = true;
            seekFromPosition(event.position);
            setDirty(true);
            return true;
        }

        // Stop button
        if (stopButtonBounds_.contains(event.position)) {
            if (onStop_) onStop_();
            playheadPosition_ = 0.0;
            setDirty(true);
            return true;
        }

        // Loop button
        if (loopButtonBounds_.contains(event.position)) {
            m_loopEnabled = !m_loopEnabled;
            setDirty(true);
            return true;
        }

        // BPM sync button
        if (bpmSyncButtonBounds_.contains(event.position)) {
            m_bpmSyncEnabled = !m_bpmSyncEnabled;
            setDirty(true);
            return true;
        }
    }

    if (event.pressed && event.button == NUIMouseButton::Right) {
        return true;
    }

    return false;
}

} // namespace AestraUI
