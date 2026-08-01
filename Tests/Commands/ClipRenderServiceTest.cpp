// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Proves the pure transforms behind every destructive clip operation. These run
// on bare buffers so a failure points at the DSP, not at project plumbing.

#include "Models/ClipRenderService.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

using Aestra::Audio::AudioBufferData;
using Aestra::Audio::ClipRenderService;

namespace {

int g_failures = 0;

void require(bool condition, const std::string& what) {
    if (!condition) {
        std::printf("  FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

void requireClose(float actual, float expected, const std::string& what) {
    if (std::fabs(actual - expected) > 1.0e-5f) {
        std::printf("  FAIL: %s (expected %f, got %f)\n", what.c_str(), expected, actual);
        ++g_failures;
    }
}

/** Stereo ramp where L is the frame index and R is its negation. */
AudioBufferData makeRamp(uint64_t frames) {
    AudioBufferData buffer;
    buffer.sampleRate = 48000;
    buffer.numChannels = 2;
    buffer.numFrames = frames;
    buffer.interleavedData.resize(static_cast<size_t>(frames) * 2);
    for (uint64_t i = 0; i < frames; ++i) {
        buffer.interleavedData[i * 2] = static_cast<float>(i);
        buffer.interleavedData[i * 2 + 1] = -static_cast<float>(i);
    }
    return buffer;
}

void testExtractRegion() {
    std::printf("extractRegion...\n");
    const AudioBufferData source = makeRamp(100);

    auto mid = ClipRenderService::extractRegion(source, 10, 20);
    require(mid != nullptr, "mid-buffer region is extracted");
    if (mid) {
        require(mid->numFrames == 20, "region has the requested frame count");
        require(mid->numChannels == source.numChannels, "region keeps the channel count");
        require(mid->sampleRate == source.sampleRate, "region keeps the sample rate");
        requireClose(mid->interleavedData[0], 10.0f, "region starts at the requested frame");
        requireClose(mid->interleavedData[1], -10.0f, "region keeps channel pairing");
    }

    // A clip may outlive its source after a tempo edit; clamp, do not overrun.
    auto overhang = ClipRenderService::extractRegion(source, 90, 50);
    require(overhang != nullptr, "overhanging region still extracts");
    if (overhang) {
        require(overhang->numFrames == 10, "overhanging region clamps to the source end");
    }

    require(ClipRenderService::extractRegion(source, 100, 10) == nullptr,
            "a region starting past the end yields nothing, not silence");
    require(ClipRenderService::extractRegion(source, 0, 0) == nullptr, "an empty region yields nothing");
}

void testReverse() {
    std::printf("reverseInPlace...\n");
    AudioBufferData buffer = makeRamp(8);
    ClipRenderService::reverseInPlace(buffer);

    require(buffer.numFrames == 8, "reverse preserves length");
    requireClose(buffer.interleavedData[0], 7.0f, "first frame becomes the last");
    requireClose(buffer.interleavedData[14], 0.0f, "last frame becomes the first");

    // The regression that matters: reversing the flat sample array would put
    // the right channel where the left belongs.
    for (uint64_t i = 0; i < buffer.numFrames; ++i) {
        const float l = buffer.interleavedData[i * 2];
        const float r = buffer.interleavedData[i * 2 + 1];
        requireClose(r, -l, "reverse keeps L/R paired within each frame");
    }

    AudioBufferData single = makeRamp(1);
    ClipRenderService::reverseInPlace(single);
    requireClose(single.interleavedData[0], 0.0f, "single-frame reverse is a no-op");

    // Reversing twice must return the original exactly.
    AudioBufferData twice = makeRamp(9);
    ClipRenderService::reverseInPlace(twice);
    ClipRenderService::reverseInPlace(twice);
    const AudioBufferData original = makeRamp(9);
    for (size_t i = 0; i < original.interleavedData.size(); ++i) {
        requireClose(twice.interleavedData[i], original.interleavedData[i], "double reverse restores the original");
    }
}

void testGain() {
    std::printf("applyGain...\n");
    AudioBufferData buffer = makeRamp(4);
    ClipRenderService::applyGain(buffer, 0.5f);
    requireClose(buffer.interleavedData[2], 0.5f, "gain scales samples");

    // A non-finite gain must not poison the buffer with NaN.
    AudioBufferData guarded = makeRamp(4);
    ClipRenderService::applyGain(guarded, std::nanf(""));
    requireClose(guarded.interleavedData[2], 1.0f, "non-finite gain is ignored");
}

void testFades() {
    std::printf("applyFades...\n");
    AudioBufferData buffer;
    buffer.sampleRate = 48000;
    buffer.numChannels = 1;
    buffer.numFrames = 100;
    buffer.interleavedData.assign(100, 1.0f);

    ClipRenderService::applyFades(buffer, 10, 10);
    // The engine's clipFadeAt starts a fade-in at silence, so a baked fade
    // must too; see testFadeMatchesEngineShape for the exact contract.
    requireClose(buffer.interleavedData[0], 0.0f, "fade-in starts at silence");
    requireClose(buffer.interleavedData[50], 1.0f, "the middle is left at unity");
    require(buffer.interleavedData[99] < 1.0f, "fade-out attenuates the last frame");

    // Ramps are monotonic across their span.
    for (uint64_t i = 1; i < 10; ++i) {
        require(buffer.interleavedData[i] > buffer.interleavedData[i - 1], "fade-in rises monotonically");
    }

    // Overlapping ramps must not multiply into a notch in the middle.
    AudioBufferData overlapped;
    overlapped.sampleRate = 48000;
    overlapped.numChannels = 1;
    overlapped.numFrames = 20;
    overlapped.interleavedData.assign(20, 1.0f);
    ClipRenderService::applyFades(overlapped, 100, 100);
    for (const float sample : overlapped.interleavedData) {
        require(sample >= 0.0f && sample <= 1.0f, "over-long fades stay within unity");
    }
    require(overlapped.interleavedData[10] > 0.0f, "over-long fades do not silence the middle");
}

void testFadeLengthsCannotOverrun() {
    std::printf("applyFades: absurd fade lengths are clamped...\n");
    // A fade longer than the clip is legal input (corrupt project, or a tempo
    // change shrinking the clip). Summing two unclamped uint64 lengths wraps
    // past the guard, so the ramp loop used to run off the end of the buffer.
    const uint64_t huge = std::numeric_limits<uint64_t>::max() - 4;

    AudioBufferData buffer = makeRamp(64);
    const size_t originalSamples = buffer.interleavedData.size();
    ClipRenderService::applyFades(buffer, huge, 8);
    require(buffer.numFrames == 64, "an absurd fade-in leaves the length alone");
    require(buffer.interleavedData.size() == originalSamples, "an absurd fade-in does not resize the buffer");

    AudioBufferData both = makeRamp(64);
    ClipRenderService::applyFades(both, huge, huge);
    require(both.numFrames == 64, "two absurd fades leave the length alone");
    require(both.interleavedData.size() == originalSamples, "two absurd fades do not resize the buffer");
    for (size_t i = 0; i < both.interleavedData.size(); ++i) {
        const float original = static_cast<float>(i / 2);
        const float actual = std::fabs(both.interleavedData[i]);
        require(std::isfinite(actual) && actual <= original + 1.0e-4f, "clamped fades never amplify");
    }
}

void testFadeMatchesEngineShape() {
    std::printf("applyFades: ramps match the engine's clip fade...\n");
    AudioBufferData buffer;
    buffer.sampleRate = 48000;
    buffer.numChannels = 1;
    buffer.numFrames = 100;
    buffer.interleavedData.assign(100, 1.0f);

    ClipRenderService::applyFades(buffer, 10, 10);
    // AudioEngine::clipFadeAt starts the fade-in at zero...
    requireClose(buffer.interleavedData[0], 0.0f, "fade-in starts at silence like the engine");
    requireClose(buffer.interleavedData[5], 0.5f, "fade-in is linear in frame/fadeLen");
    requireClose(buffer.interleavedData[10], 1.0f, "fade-in reaches unity at its length");
    // ...and leaves the fade-out's seam sample at unity (strict >).
    requireClose(buffer.interleavedData[90], 1.0f, "the fade-out seam sample stays at unity");
    requireClose(buffer.interleavedData[95], 0.5f, "fade-out is linear in remaining/fadeLen");
    requireClose(buffer.interleavedData[99], 0.1f, "the last frame is one step above silence");

    // Overlapping ramps take the minimum, as the engine does, not the product.
    AudioBufferData overlap;
    overlap.sampleRate = 48000;
    overlap.numChannels = 1;
    overlap.numFrames = 10;
    overlap.interleavedData.assign(10, 1.0f);
    ClipRenderService::applyFades(overlap, 8, 8);
    for (uint64_t i = 0; i < overlap.numFrames; ++i) {
        const double fadeIn = (i < 8) ? static_cast<double>(i) / 8.0 : 1.0;
        const double fadeOut = (i + 8 > 10) ? static_cast<double>(10 - i) / 8.0 : 1.0;
        requireClose(overlap.interleavedData[i], static_cast<float>(std::min({1.0, fadeIn, fadeOut})),
                     "overlapping fades take the minimum, not the product");
    }
}

void testPeak() {
    std::printf("peakMagnitude...\n");
    AudioBufferData buffer;
    buffer.sampleRate = 48000;
    buffer.numChannels = 1;
    buffer.numFrames = 4;
    buffer.interleavedData = {0.1f, -0.8f, 0.3f, 0.2f};
    requireClose(ClipRenderService::peakMagnitude(buffer), 0.8f, "peak uses absolute value");

    buffer.interleavedData = {0.1f, std::nanf(""), 0.3f, 0.2f};
    requireClose(ClipRenderService::peakMagnitude(buffer), 0.3f, "peak skips non-finite samples");
}

} // namespace

int main() {
    std::printf("=== ClipRenderService transforms ===\n");
    testExtractRegion();
    testReverse();
    testGain();
    testFades();
    testFadeLengthsCannotOverrun();
    testFadeMatchesEngineShape();
    testPeak();

    if (g_failures != 0) {
        std::printf("FAILED: %d check(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("All ClipRenderService transform checks passed.\n");
    return EXIT_SUCCESS;
}
