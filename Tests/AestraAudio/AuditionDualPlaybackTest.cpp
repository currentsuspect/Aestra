// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Synchronised dual playback (V81-E2, FD-26): with a reference loaded, A and B run in lockstep, a switch is a
// ramp between two live signals (never a cut or a seek), level match scales a side, and with no reference
// playback is exactly what it was.

#include "ClipSource.h"
#include "Playback/AuditionEngine.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;
void check(bool c, const char* m) { if (!c) { std::printf("[FAIL] %s\n", m); ++g_failures; } }
void checkNear(double a, double e, double tol, const char* m) {
    if (std::fabs(a - e) > tol) { std::printf("[FAIL] %s: expected %.4f ±%.4f, got %.4f\n", m, e, tol, a); ++g_failures; }
}

constexpr uint32_t kRate = 48000;

std::shared_ptr<ClipSource> constantSource(float value, double seconds) {
    auto buffer = std::make_shared<AudioBufferData>();
    buffer->sampleRate = kRate;
    buffer->numChannels = 2;
    buffer->numFrames = static_cast<uint64_t>(seconds * kRate);
    buffer->interleavedData.assign(buffer->numFrames * 2, value);
    auto source = std::make_shared<ClipSource>(ClipSourceID{1}, "test");
    source->setBuffer(buffer);
    return source;
}

// Renders `blocks` blocks of `frames` frames and returns the left channel.
std::vector<float> render(AuditionEngine& engine, int blocks, uint32_t frames) {
    std::vector<float> left, block(frames * 2);
    for (int b = 0; b < blocks; ++b) {
        engine.processBlock(block.data(), frames, 2);
        for (uint32_t i = 0; i < frames; ++i) left.push_back(block[i * 2]);
    }
    return left;
}

} // namespace

int main() {
    // No reference: the mix plays untouched.
    {
        AuditionEngine engine;
        engine.setSampleRate(kRate);
        engine.setMixSource(constantSource(0.5f, 2.0));
        engine.play();
        const auto out = render(engine, 4, 256);
        checkNear(out.front(), 0.5, 1e-4, "the mix plays alone before a reference is set");
        checkNear(out.back(), 0.5, 1e-4, "and keeps playing");
        check(!engine.hasReference(), "no reference yet");
    }

    // A reference runs alongside: the mix is still heard until the side changes.
    {
        AuditionEngine engine;
        engine.setSampleRate(kRate);
        engine.setMixSource(constantSource(0.5f, 2.0));
        engine.setReferenceSource(constantSource(0.25f, 2.0));
        engine.play();
        check(engine.hasReference(), "a reference is set");
        const auto before = render(engine, 4, 256);
        checkNear(before.back(), 0.5, 1e-4, "the mix is heard while the reference runs silently");

        // Switch to B: a ramp, never a gap. Every sample stays between the two levels.
        engine.setListenSide(AuditionEngine::ListenSide::Reference);
        const auto ramp = render(engine, 8, 256); // 2048 frames, ramp is 480
        float lowest = 10.0f, highest = -10.0f;
        double largestStep = 0.0;
        for (size_t i = 0; i < ramp.size(); ++i) {
            lowest = std::min(lowest, ramp[i]);
            highest = std::max(highest, ramp[i]);
            if (i) largestStep = std::max(largestStep, static_cast<double>(std::fabs(ramp[i] - ramp[i - 1])));
        }
        check(lowest >= 0.25f - 1e-4f && highest <= 0.5f + 1e-4f, "a switch never dips below or overshoots the two levels");
        check(largestStep < 0.01, "a switch moves by small steps, not a cut");
        checkNear(ramp.back(), 0.25, 1e-4, "after the ramp only the reference is heard");

        // And back, with the reference level-matched +6 dB (0.25 -> 0.5) on the way.
        engine.setReferenceGainDb(6.0206f);
        const auto matched = render(engine, 4, 256);
        checkNear(matched.back(), 0.5, 1e-3, "level match scales the reference to the mix");
        engine.setListenSide(AuditionEngine::ListenSide::Mix);
        const auto back = render(engine, 8, 256);
        checkNear(back.back(), 0.5, 1e-4, "switching back returns the mix");
    }

    // The reference reads at the same position plus its offset: a section switch lands in the matching section.
    {
        auto reference = std::make_shared<AudioBufferData>();
        reference->sampleRate = kRate;
        reference->numChannels = 2;
        reference->numFrames = kRate * 2;
        reference->interleavedData.assign(reference->numFrames * 2, 0.1f);
        for (uint64_t f = kRate; f < reference->numFrames; ++f) { // second half is louder
            reference->interleavedData[f * 2] = reference->interleavedData[f * 2 + 1] = 0.8f;
        }
        auto referenceSource = std::make_shared<ClipSource>(ClipSourceID{2}, "ref");
        referenceSource->setBuffer(reference);

        AuditionEngine engine;
        engine.setSampleRate(kRate);
        engine.setMixSource(constantSource(0.5f, 1.0));
        engine.setReferenceSource(referenceSource, 1.0); // the mix's start lines up with the reference's second half
        engine.setListenSide(AuditionEngine::ListenSide::Reference);
        engine.play();
        render(engine, 4, 256); // let the ramp finish
        checkNear(render(engine, 1, 256).back(), 0.8, 1e-3, "with a +1 s offset the reference plays its second half");

        engine.setReferenceOffsetSeconds(0.0);
        render(engine, 1, 256);
        checkNear(render(engine, 1, 256).back(), 0.1, 1e-3, "with no offset it plays its first half");
    }

    // Clearing the reference leaves the mix alone again.
    {
        AuditionEngine engine;
        engine.setSampleRate(kRate);
        engine.setMixSource(constantSource(0.5f, 2.0));
        engine.setReferenceSource(constantSource(0.25f, 2.0));
        engine.setListenSide(AuditionEngine::ListenSide::Reference);
        engine.play();
        render(engine, 8, 256);
        engine.clearReferenceSource();
        const auto out = render(engine, 2, 256);
        checkNear(out.back(), 0.5, 1e-4, "clearing the reference returns the plain mix");
    }

    if (g_failures) { std::printf("%d failed\n", g_failures); return 1; }
    std::printf("AuditionDualPlaybackTest passed\n");
    return 0;
}
