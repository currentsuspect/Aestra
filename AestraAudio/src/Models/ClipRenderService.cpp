// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Models/ClipRenderService.h"

#include "Models/PatternManager.h"
#include "Models/SourceManager.h"
#include "AestraLog.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace Aestra {
namespace Audio {

namespace {

/** Interleaved sample count for a frame range, guarded against overflow. */
bool framesToSamples(uint64_t frames, uint32_t channels, size_t& outSamples) {
    if (channels == 0) {
        return false;
    }
    if (frames > std::numeric_limits<size_t>::max() / channels) {
        return false;
    }
    outSamples = static_cast<size_t>(frames) * channels;
    return true;
}

} // namespace

ClipRenderService::SourceRegion ClipRenderService::resolveClipRegion(const ClipInstance& clip) const {
    SourceRegion region;
    if (!clip.patternId.isValid()) {
        return region;
    }
    const PatternSource* pattern = m_patterns.getPattern(clip.patternId);
    if (!pattern || !pattern->isAudio()) {
        return region;
    }

    const auto& payload = std::get<AudioSlicePayload>(pattern->payload);
    const ClipSource* source = m_sources.getSource(payload.audioSourceId);
    if (!source) {
        return region;
    }
    auto buffer = source->getSharedBuffer();
    if (!buffer || !buffer->isValid()) {
        return region;
    }

    // Patterns written by import and by recording both carry one full-extent
    // slice; a missing or empty slice means "the whole source".
    uint64_t startFrame = 0;
    uint64_t frameCount = buffer->numFrames;
    if (!payload.slices.empty()) {
        const AudioSlice& slice = payload.slices.front();
        if (std::isfinite(slice.startSamples) && slice.startSamples > 0.0) {
            startFrame = static_cast<uint64_t>(slice.startSamples);
        }
        if (std::isfinite(slice.lengthSamples) && slice.lengthSamples > 0.0) {
            frameCount = static_cast<uint64_t>(slice.lengthSamples);
        }
    }
    if (startFrame >= buffer->numFrames) {
        return region;
    }
    frameCount = std::min(frameCount, buffer->numFrames - startFrame);

    region.buffer = std::move(buffer);
    region.startFrame = startFrame;
    region.frameCount = frameCount;
    region.mixerChannelId = pattern->getMixerChannelId();
    region.lengthBeats = pattern->lengthBeats;
    region.name = pattern->name.empty() ? clip.name : pattern->name;
    return region;
}

std::shared_ptr<AudioBufferData> ClipRenderService::extractRegion(const AudioBufferData& source, uint64_t startFrame,
                                                                 uint64_t frameCount) {
    if (!source.isValid() || frameCount == 0 || startFrame >= source.numFrames) {
        return nullptr;
    }

    // Clamp an overhanging range instead of reading past the buffer. A clip
    // whose length outlives its source is normal after tempo edits.
    const uint64_t available = source.numFrames - startFrame;
    const uint64_t framesToCopy = std::min(frameCount, available);

    size_t sampleCount = 0;
    size_t sampleOffset = 0;
    if (!framesToSamples(framesToCopy, source.numChannels, sampleCount) ||
        !framesToSamples(startFrame, source.numChannels, sampleOffset)) {
        return nullptr;
    }
    if (sampleOffset + sampleCount > source.interleavedData.size()) {
        return nullptr;
    }

    auto out = std::make_shared<AudioBufferData>();
    out->sampleRate = source.sampleRate;
    out->numChannels = source.numChannels;
    out->numFrames = framesToCopy;
    out->interleavedData.assign(source.interleavedData.begin() + static_cast<std::ptrdiff_t>(sampleOffset),
                                source.interleavedData.begin() +
                                    static_cast<std::ptrdiff_t>(sampleOffset + sampleCount));
    return out;
}

void ClipRenderService::reverseInPlace(AudioBufferData& buffer) {
    if (!buffer.isValid() || buffer.numFrames < 2) {
        return;
    }
    const uint32_t channels = buffer.numChannels;
    size_t usableSamples = 0;
    if (!framesToSamples(buffer.numFrames, channels, usableSamples) ||
        usableSamples > buffer.interleavedData.size()) {
        return;
    }

    // Swap whole frames so a stereo image survives the reverse; reversing the
    // flat sample array would also swap L and R.
    float* data = buffer.interleavedData.data();
    for (uint64_t front = 0, back = buffer.numFrames - 1; front < back; ++front, --back) {
        float* a = data + static_cast<size_t>(front) * channels;
        float* b = data + static_cast<size_t>(back) * channels;
        for (uint32_t ch = 0; ch < channels; ++ch) {
            std::swap(a[ch], b[ch]);
        }
    }
}

void ClipRenderService::applyGain(AudioBufferData& buffer, float gainLinear) {
    if (!std::isfinite(gainLinear) || gainLinear == 1.0f) {
        return;
    }
    for (float& sample : buffer.interleavedData) {
        sample *= gainLinear;
    }
}

void ClipRenderService::applyFades(AudioBufferData& buffer, uint64_t fadeInFrames, uint64_t fadeOutFrames) {
    if (!buffer.isValid() || buffer.numFrames == 0) {
        return;
    }
    const uint32_t channels = buffer.numChannels;
    size_t usableSamples = 0;
    if (!framesToSamples(buffer.numFrames, channels, usableSamples) ||
        usableSamples > buffer.interleavedData.size()) {
        return;
    }

    // Ramps that together exceed the clip would otherwise multiply in the
    // overlap and dig a hole in the middle. Shrink them proportionally so they
    // meet exactly once, which is what a DAW crossfade-at-the-seam looks like.
    if (fadeInFrames + fadeOutFrames > buffer.numFrames) {
        const long double total = static_cast<long double>(fadeInFrames) + static_cast<long double>(fadeOutFrames);
        if (total <= 0.0L) {
            return;
        }
        const long double scale = static_cast<long double>(buffer.numFrames) / total;
        fadeInFrames = static_cast<uint64_t>(static_cast<long double>(fadeInFrames) * scale);
        fadeOutFrames = buffer.numFrames - fadeInFrames;
    }

    float* data = buffer.interleavedData.data();

    for (uint64_t frame = 0; frame < fadeInFrames; ++frame) {
        const float g = static_cast<float>(static_cast<double>(frame + 1) / static_cast<double>(fadeInFrames + 1));
        float* f = data + static_cast<size_t>(frame) * channels;
        for (uint32_t ch = 0; ch < channels; ++ch) {
            f[ch] *= g;
        }
    }

    for (uint64_t i = 0; i < fadeOutFrames; ++i) {
        const uint64_t frame = buffer.numFrames - 1 - i;
        const float g = static_cast<float>(static_cast<double>(i + 1) / static_cast<double>(fadeOutFrames + 1));
        float* f = data + static_cast<size_t>(frame) * channels;
        for (uint32_t ch = 0; ch < channels; ++ch) {
            f[ch] *= g;
        }
    }
}

float ClipRenderService::peakMagnitude(const AudioBufferData& buffer) {
    float peak = 0.0f;
    for (const float sample : buffer.interleavedData) {
        if (std::isfinite(sample)) {
            peak = std::max(peak, std::fabs(sample));
        }
    }
    return peak;
}

std::string ClipRenderService::uniqueRenderPath(const std::string& directory, const std::string& baseName) {
    namespace fs = std::filesystem;

    // Strip anything that would be awkward in a filename; the pattern name is
    // user-supplied and may contain separators.
    std::string safe;
    safe.reserve(baseName.size());
    for (const char c : baseName) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                        c == '_' || c == ' ';
        safe.push_back(ok ? c : '_');
    }
    if (safe.empty()) {
        safe = "render";
    }

    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif

    static std::atomic<uint64_t> s_counter{0};
    const uint64_t uniq = s_counter.fetch_add(1, std::memory_order_relaxed);

    std::ostringstream oss;
    oss << safe << "_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << "_" << uniq << ".wav";
    return (fs::path(directory) / oss.str()).string();
}

bool ClipRenderService::writeFloatWav(const std::string& path, const AudioBufferData& buffer) {
    if (buffer.numChannels == 0 || buffer.numChannels > std::numeric_limits<uint16_t>::max() ||
        buffer.sampleRate == 0 || buffer.numFrames > std::numeric_limits<uint32_t>::max() ||
        buffer.interleavedData.size() > (std::numeric_limits<uint32_t>::max() / sizeof(float))) {
        return false;
    }

    const uint16_t audioFormat = 3; // IEEE float
    const uint16_t numChannels = static_cast<uint16_t>(buffer.numChannels);
    const uint32_t sampleRate = buffer.sampleRate;
    const uint16_t bitsPerSample = 32;
    const uint16_t blockAlign = static_cast<uint16_t>(numChannels * (bitsPerSample / 8));
    if (blockAlign == 0 || sampleRate > std::numeric_limits<uint32_t>::max() / blockAlign) {
        return false;
    }
    const uint32_t byteRate = sampleRate * blockAlign;
    const uint32_t dataSize = static_cast<uint32_t>(buffer.interleavedData.size() * sizeof(float));
    const uint32_t sampleCount = static_cast<uint32_t>(buffer.numFrames);
    const uint32_t factChunkSize = 4;
    if (dataSize > std::numeric_limits<uint32_t>::max() - 48u) {
        return false;
    }
    const uint32_t riffChunkSize = 48u + dataSize;
    const uint32_t fmtChunkSize = 16;

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char*>(&riffChunkSize), sizeof(riffChunkSize));
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    file.write(reinterpret_cast<const char*>(&fmtChunkSize), sizeof(fmtChunkSize));
    file.write(reinterpret_cast<const char*>(&audioFormat), sizeof(audioFormat));
    file.write(reinterpret_cast<const char*>(&numChannels), sizeof(numChannels));
    file.write(reinterpret_cast<const char*>(&sampleRate), sizeof(sampleRate));
    file.write(reinterpret_cast<const char*>(&byteRate), sizeof(byteRate));
    file.write(reinterpret_cast<const char*>(&blockAlign), sizeof(blockAlign));
    file.write(reinterpret_cast<const char*>(&bitsPerSample), sizeof(bitsPerSample));
    file.write("fact", 4);
    file.write(reinterpret_cast<const char*>(&factChunkSize), sizeof(factChunkSize));
    file.write(reinterpret_cast<const char*>(&sampleCount), sizeof(sampleCount));
    file.write("data", 4);
    file.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
    file.write(reinterpret_cast<const char*>(buffer.interleavedData.data()), dataSize);

    return file.good();
}

ClipRenderService::CommitResult ClipRenderService::commit(const AudioBufferData& buffer,
                                                          const std::string& renderDirectory,
                                                          const std::string& baseName, double lengthBeats,
                                                          uint32_t mixerChannelId) {
    CommitResult result;
    if (!buffer.isValid() || buffer.numFrames == 0) {
        return result;
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(renderDirectory, ec);
    if (ec) {
        Log::error("[ClipRenderService] Could not create render directory: " + renderDirectory);
        return result;
    }

    const std::string path = uniqueRenderPath(renderDirectory, baseName);
    if (!writeFloatWav(path, buffer)) {
        Log::error("[ClipRenderService] Failed to write rendered audio: " + path);
        // A partial file would look like a valid source on the next load.
        fs::remove(path, ec);
        return result;
    }

    // Hand the in-memory copy straight to the source so playback does not have
    // to wait for a decode of the file we just wrote.
    auto owned = std::make_shared<AudioBufferData>(buffer);
    const ClipSourceID sourceId = m_sources.createRecordedSource(path, baseName, std::move(owned));
    if (!sourceId.isValid()) {
        Log::error("[ClipRenderService] Failed to register rendered source: " + path);
        fs::remove(path, ec);
        return result;
    }

    AudioSlicePayload payload;
    payload.audioSourceId = sourceId;
    payload.durationSeconds = buffer.durationSeconds();
    AudioSlice fullSlice;
    fullSlice.startSamples = 0.0;
    fullSlice.lengthSamples = static_cast<double>(buffer.numFrames);
    payload.slices.push_back(fullSlice);

    const double beats = (std::isfinite(lengthBeats) && lengthBeats > 0.0) ? lengthBeats : 4.0;
    const PatternID patternId = m_patterns.createAudioPattern(baseName, beats, payload);
    if (!patternId.isValid()) {
        Log::error("[ClipRenderService] Failed to create pattern for rendered audio: " + path);
        return result;
    }
    m_patterns.setPatternMixerChannel(patternId, mixerChannelId);

    result.patternId = patternId;
    result.sourceId = sourceId;
    result.filePath = path;
    return result;
}

} // namespace Audio
} // namespace Aestra
