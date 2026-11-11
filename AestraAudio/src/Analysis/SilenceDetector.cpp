// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Analysis/SilenceDetector.h"

#include "Models/ClipSource.h"

#include <algorithm>
#include <cmath>

namespace Aestra {
namespace Audio {

namespace {

constexpr float kFloorDb = -140.0f; // what digital silence reads as

} // namespace

std::vector<float> SilenceDetector::frameLevelsDb(const AudioBufferData& buffer, uint64_t startFrame,
                                                  uint64_t frameCount) {
    std::vector<float> levels;
    if (!buffer.isValid() || startFrame >= buffer.numFrames || frameCount == 0) {
        return levels;
    }
    frameCount = std::min(frameCount, buffer.numFrames - startFrame);
    const uint64_t hop = std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(buffer.sampleRate * kFrameSec)));
    const uint32_t channels = buffer.numChannels;
    levels.reserve(static_cast<size_t>(frameCount / hop) + 1);

    std::vector<float> samples(static_cast<size_t>(hop) * channels);
    std::vector<double> sumSquares(channels);
    for (uint64_t offset = 0; offset < frameCount; offset += hop) {
        const uint64_t n = std::min(hop, frameCount - offset);
        buffer.copyFloats((startFrame + offset) * channels, n * channels, samples.data());
        std::fill(sumSquares.begin(), sumSquares.end(), 0.0);
        for (uint64_t f = 0; f < n; ++f) {
            for (uint32_t c = 0; c < channels; ++c) {
                const double s = samples[static_cast<size_t>(f) * channels + c];
                sumSquares[c] += s * s;
            }
        }
        // The loudest channel decides: a clip is only silent when every channel is.
        const double loudest = *std::max_element(sumSquares.begin(), sumSquares.end());
        const double rms = std::sqrt(loudest / static_cast<double>(n));
        levels.push_back(rms > 1e-7 ? static_cast<float>(20.0 * std::log10(rms)) : kFloorDb);
    }
    return levels;
}

std::vector<SilenceRange> SilenceDetector::findRemovableRanges(const AudioBufferData& buffer, uint64_t startFrame,
                                                               uint64_t frameCount, const SilenceParams& params,
                                                               double* usedThresholdDb) {
    std::vector<SilenceRange> ranges;
    const std::vector<float> levels = frameLevelsDb(buffer, startFrame, frameCount);
    if (levels.empty()) {
        return ranges;
    }
    frameCount = std::min(frameCount, buffer.numFrames - startFrame);
    const double sampleRate = static_cast<double>(buffer.sampleRate);
    const uint64_t hop = std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(sampleRate * kFrameSec)));

    double threshold = params.thresholdDb;
    if (params.autoThreshold) {
        // The noise floor is what the quietest tenth of the clip sits at; speech is far above it.
        std::vector<float> sorted(levels);
        std::sort(sorted.begin(), sorted.end());
        const size_t at = std::min(sorted.size() - 1, static_cast<size_t>(sorted.size() * kFloorPercentile));
        const double floorDb = sorted[at];
        // A clip with no loud part (or only noise) has nothing to separate: cut nothing rather than guess.
        if (sorted.back() - floorDb < kAutoMinRangeDb) {
            if (usedThresholdDb)
                *usedThresholdDb = floorDb + kAutoMarginDb;
            return ranges;
        }
        threshold = std::clamp(floorDb + kAutoMarginDb, kAutoMinDb, kAutoMaxDb);
    }
    if (usedThresholdDb)
        *usedThresholdDb = threshold;

    const uint64_t minRun = static_cast<uint64_t>(std::ceil(std::max(0.0, params.minSilenceSec) / kFrameSec));
    const double keepFrames = std::max(0.0, params.keepPauseSec) * sampleRate;
    // Not worth a cut (and a fade pair) below this: 20 ms.
    const double minRemoveFrames = std::max(1.0, 0.02 * sampleRate);

    const auto emit = [&](uint64_t firstFrame, uint64_t endFrame, bool touchesStart, bool touchesEnd) {
        // [first, end) in region frames is a measured-quiet run. Work out what to remove from it.
        const double runFrames = static_cast<double>(endFrame - firstFrame);
        double a = 0.0;
        double b = 0.0;
        if (touchesStart && touchesEnd) {
            // The whole window is quiet: keep one half pause at the head, drop the rest.
            a = static_cast<double>(firstFrame) + keepFrames * 0.5;
            b = static_cast<double>(endFrame);
        } else if (touchesStart) {
            a = static_cast<double>(firstFrame);
            b = static_cast<double>(endFrame) - keepFrames * 0.5;
        } else if (touchesEnd) {
            a = static_cast<double>(firstFrame) + keepFrames * 0.5;
            b = static_cast<double>(endFrame);
        } else {
            const double remove = runFrames - keepFrames;
            const double centre = (static_cast<double>(firstFrame) + static_cast<double>(endFrame)) * 0.5;
            a = centre - remove * 0.5;
            b = centre + remove * 0.5;
        }
        if (b - a < minRemoveFrames) {
            return;
        }
        SilenceRange r;
        r.startFrame = static_cast<uint64_t>(std::llround(a));
        r.endFrame = std::min<uint64_t>(static_cast<uint64_t>(std::llround(b)), frameCount);
        if (r.endFrame > r.startFrame) {
            ranges.push_back(r);
        }
    };

    size_t i = 0;
    while (i < levels.size()) {
        if (levels[i] > threshold) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < levels.size() && levels[j] <= threshold) {
            ++j;
        }
        if (j - i >= minRun) {
            const uint64_t first = static_cast<uint64_t>(i) * hop;
            const uint64_t end = std::min<uint64_t>(static_cast<uint64_t>(j) * hop, frameCount);
            emit(first, end, i == 0, j == levels.size());
        }
        i = j;
    }
    return ranges;
}

} // namespace Audio
} // namespace Aestra
