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
    
    const float cornerRadius = 6.0f;
    NUIColor bgColor = theme.getColor("surfaceRaised");
    NUIColor borderColor = theme.getColor("borderSubtle");
    
    // === SOLID BASE FILL ===
    // First, fill the entire bounds with background color to ensure no gaps
    renderer.fillRect(bounds, bgColor);
    
    // === CLIPPED BORDER RENDERING ===
    
    // 1. Draw the TOP portion (square corners) - only the top 6px
    {
        NUIRect topClip = bounds;
        topClip.height = cornerRadius;
        renderer.setClipRect(topClip);
        
        // Draw simple filled rect for the top (no rounded corners)
        renderer.fillRect(topClip, bgColor);
        
        // Draw side borders and top separator line
        renderer.drawLine(NUIPoint(topClip.x, topClip.y), NUIPoint(topClip.x, topClip.bottom()), 1.0f, borderColor);
        renderer.drawLine(NUIPoint(topClip.right(), topClip.y), NUIPoint(topClip.right(), topClip.bottom()), 1.0f, borderColor);
        renderer.drawLine(NUIPoint(bounds.x, bounds.y), NUIPoint(bounds.right(), bounds.y), 1.0f, borderColor);
        
        renderer.clearClipRect();
    }
    
    // 2. Draw the BOTTOM portion (with rounded corners) - clip out top 6px
    {
        NUIRect bottomClip = bounds;
        bottomClip.y += cornerRadius;
        bottomClip.height -= cornerRadius;
        renderer.setClipRect(bottomClip);
        renderer.fillRoundedRect(bounds, cornerRadius, bgColor);
        renderer.strokeRoundedRect(bounds, cornerRadius, 1.0f, borderColor);
        renderer.clearClipRect();
    }
    
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
            
            // Purple Color (Matching sidebar selection)
            // Using a vibrant purple accent
            folderIcon_->setColor(theme.getColor("accentPrimary"));
            
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

    NUIColor btnColor = isPlaying_ ? theme.getColor("accentPrimary") : theme.getColor("primary");
    // Button background
    renderer.fillRoundedRect(playButtonBounds_, 4.0f, btnColor.withAlpha(0.3f));
    
    // Icon
    auto& icon = isPlaying_ ? stopIcon_ : playIcon_;
    if (icon) {
        float iconSize = 14.0f;
        // Pixel snap positions
        float iconX = std::floor(playButtonBounds_.x + (playButtonBounds_.width - iconSize) * 0.5f + (isPlaying_ ? 0.0f : 1.0f)); 
        // Nudge Stop icon down 1px
        float iconY = std::floor(playButtonBounds_.y + (playButtonBounds_.height - iconSize) * 0.5f + (isPlaying_ ? 1.0f : 0.0f)); 
        
        icon->setBounds(NUIRect(iconX, iconY, iconSize, iconSize));
        icon->setColor(theme.getColor("textPrimary")); // White/Bright
        icon->onRender(renderer);
    }
    
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

        // Use primary accent (Purple) for waveform to match theme
        NUIColor waveformFill = theme.getColor("accentPrimary").withAlpha(0.7f);
        
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
                2.0f, theme.getColor("accentPrimary")
            );
        }
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
