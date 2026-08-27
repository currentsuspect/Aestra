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

ClipRenderService::SourceRegion ClipRenderService::resolveClipRegion(const ClipInstance& clip,
                                                                     double projectSampleRate) const {
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
    // Same offsets the runtime snapshot applies (PlaylistModel::buildSnapshot):
    // the canonical per-clip offset, then the instance slip. sourceStart is in
    // project-rate samples, so convert it into this source's frames.
    const double sourceRate = static_cast<double>(buffer->sampleRate);
    double offsetFrames = 0.0;
    if (clip.durationSeconds > 0.0 && std::isfinite(clip.sourceOffsetSeconds) && clip.sourceOffsetSeconds > 0.0) {
        offsetFrames += clip.sourceOffsetSeconds * sourceRate;
    }
    if (std::isfinite(clip.edits.sourceStart) && clip.edits.sourceStart > 0.0) {
        const double rateScale = (projectSampleRate > 0.0) ? sourceRate / projectSampleRate : 1.0;
        offsetFrames += clip.edits.sourceStart * rateScale;
    }
    if (offsetFrames > 0.0) {
        const double maxOffset = static_cast<double>(buffer->numFrames);
        startFrame += static_cast<uint64_t>(std::min(offsetFrames, maxOffset));
    }

    if (startFrame >= buffer->numFrames) {
        return region;
    }
    frameCount = std::min(frameCount, buffer->numFrames - startFrame);

    // A trimmed clip plays only its own duration, so that is what gets
    // rendered; otherwise reversing a trim would drag in audio the user
    // cannot hear. The audible window is what the KERNEL consumes — timeline
    // span x effective varispeed. durationSeconds is the model's bookkeeping
    // canonical (span / varispeed, #746), so the window is canonical x v^2;
    // using canonical x v underreports it by another factor of v and
    // prematurely truncates varispeed clips (commit/extract regressions).
    if (clip.durationSeconds > 0.0 && std::isfinite(clip.durationSeconds)) {
        const double varispeed = static_cast<double>(clip.edits.effectiveVarispeed());
        const double audibleFrames = clip.durationSeconds * varispeed * varispeed * sourceRate;
        if (audibleFrames >= 1.0) {
            frameCount = std::min(frameCount, static_cast<uint64_t>(audibleFrames));
        }
    }

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

void ClipRenderService::applyPlaybackRate(AudioBufferData& buffer, float playbackRate) {
    if (!buffer.isValid()) {
        return;
    }

    const double rate = std::isfinite(playbackRate) ? std::clamp(static_cast<double>(playbackRate), 0.25, 4.0) : 1.0;
    if (std::abs(rate - 1.0) < 1.0e-9) {
        return;
    }

    const uint64_t sourceFrames = buffer.numFrames;
    const uint32_t channels = buffer.numChannels;
    const uint64_t outputFrames =
        std::max<uint64_t>(1, static_cast<uint64_t>(static_cast<double>(sourceFrames) / rate));
    size_t outputSamples = 0;
    if (!framesToSamples(outputFrames, channels, outputSamples)) {
        return;
    }

    std::vector<float> output(outputSamples, 0.0f);
    const auto sampleAt = [&buffer, sourceFrames, channels](int64_t frame, uint32_t channel) {
        const auto clamped =
            static_cast<uint64_t>(std::clamp<int64_t>(frame, 0, static_cast<int64_t>(sourceFrames) - 1));
        const float sample = buffer.interleavedData[static_cast<size_t>(clamped) * channels + channel];
        return std::isfinite(sample) ? sample : 0.0f;
    };

    for (uint64_t outputFrame = 0; outputFrame < outputFrames; ++outputFrame) {
        const double phase = std::min(static_cast<double>(sourceFrames - 1), static_cast<double>(outputFrame) * rate);
        const int64_t frame = static_cast<int64_t>(phase);
        const double fraction = phase - static_cast<double>(frame);
        for (uint32_t channel = 0; channel < channels; ++channel) {
            const double s0 = sampleAt(frame - 1, channel);
            const double s1 = sampleAt(frame, channel);
            const double s2 = sampleAt(frame + 1, channel);
            const double s3 = sampleAt(frame + 2, channel);
            const double sample = 0.5 * ((2.0 * s1) + (-s0 + s2) * fraction +
                                         (2.0 * s0 - 5.0 * s1 + 4.0 * s2 - s3) * fraction * fraction +
                                         (-s0 + 3.0 * s1 - 3.0 * s2 + s3) * fraction * fraction * fraction);
            output[static_cast<size_t>(outputFrame) * channels + channel] = static_cast<float>(sample);
        }
    }

    buffer.numFrames = outputFrames;
    buffer.interleavedData = std::move(output);
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

    // Clamp BEFORE any arithmetic on the pair: a fade longer than the clip is
    // legal input (a corrupt project, or a tempo change shrinking the clip),
    // and summing two unclamped uint64 lengths can wrap past the guard and
    // send the ramp loop off the end of the buffer.
    const uint64_t fadeIn = std::min(fadeInFrames, buffer.numFrames);
    const uint64_t fadeOut = std::min(fadeOutFrames, buffer.numFrames);
    if (fadeIn == 0 && fadeOut == 0) {
        return;
    }

    // Mirrors AudioEngine's clipFadeAt so a committed clip sounds like the one
    // that was playing: the fade-in starts at zero, the fade-out's seam sample
    // stays at unity (strict >), and overlapping ramps take the MINIMUM rather
    // than multiplying into a notch.
    //
    // The engine's CLIP_EDGE_FADE_SAMPLES micro-fade is deliberately not baked
    // in: it is a playback-time anti-click that still applies to the committed
    // clip, so baking it too would attenuate those 128 samples twice.
    float* data = buffer.interleavedData.data();
    for (uint64_t frame = 0; frame < buffer.numFrames; ++frame) {
        double gain = 1.0;
        if (fadeIn > 0 && frame < fadeIn) {
            gain = std::min(gain, static_cast<double>(frame) / static_cast<double>(fadeIn));
        }
        if (fadeOut > 0 && frame + fadeOut > buffer.numFrames) {
            gain = std::min(gain, static_cast<double>(buffer.numFrames - frame) / static_cast<double>(fadeOut));
        }
        if (gain >= 1.0) {
            continue;
        }
        float* f = data + static_cast<size_t>(frame) * channels;
        for (uint32_t ch = 0; ch < channels; ++ch) {
            f[ch] = static_cast<float>(static_cast<double>(f[ch]) * gain);
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
