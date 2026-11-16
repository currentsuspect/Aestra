// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// A reference longer than the mix is cut where the mix ends (owner, 16 Nov 2026). The position was held at the
// mix's end while the engine kept playing, so every block read the same instant of the longer reference again: a
// buzz at the block rate. Now both stop together at the mix's end, and a mix with no reference also stops at its
// end instead of holding there.

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

constexpr uint32_t kRate = 48000;

// A source whose left channel counts up from 0 to 1 over its length, so any repeated instant shows as a repeat.
std::shared_ptr<ClipSource> rampSource(double seconds) {
    auto buffer = std::make_shared<AudioBufferData>();
    buffer->sampleRate = kRate;
    buffer->numChannels = 2;
    buffer->numFrames = static_cast<uint64_t>(seconds * kRate);
    buffer->interleavedData.resize(buffer->numFrames * 2);
    for (uint64_t i = 0; i < buffer->numFrames; ++i)
        buffer->interleavedData[i * 2] = buffer->interleavedData[i * 2 + 1] = static_cast<float>(i) / static_cast<float>(buffer->numFrames);
    auto source = std::make_shared<ClipSource>(ClipSourceID{1}, "ramp");
    source->setBuffer(buffer);
    return source;
}

std::vector<float> render(AuditionEngine& engine, int blocks, uint32_t frames) {
    std::vector<float> left, block(frames * 2);
    for (int b = 0; b < blocks; ++b) {
        engine.processBlock(block.data(), frames, 2);
        engine.handleDeferredTrackTransition(); // the app calls this from its UI timer
        for (uint32_t i = 0; i < frames; ++i) left.push_back(block[i * 2]);
    }
    return left;
}

} // namespace

int main() {
    constexpr uint32_t kBlock = 256;
    const int blocksPerSecond = kRate / kBlock;

    // A longer reference is cut where the mix ends: both stop together, and nothing repeats or buzzes.
    {
        AuditionEngine engine;
        engine.setSampleRate(kRate);
        engine.setMixSource(rampSource(1.0));
        engine.setReferenceSource(rampSource(3.0));
        engine.setListenSide(AuditionEngine::ListenSide::Reference);
        check(std::fabs(engine.getDurationSeconds() - 1.0) < 0.01, "the length is the mix's, not the longer reference's");
        engine.play();
        const auto out = render(engine, blocksPerSecond * 3, kBlock);

        // The reference is heard up to the mix's end (1 s), rising, then silence: no held instant repeating.
        size_t lastAudible = 0;
        for (size_t i = 0; i < out.size(); ++i) if (std::fabs(out[i]) > 1e-6f) lastAudible = i;
        check(lastAudible > static_cast<size_t>(0.9 * kRate) && lastAudible <= static_cast<size_t>(1.02 * kRate),
              "the reference is heard to the end of the mix and not a moment past it");
        check(!engine.isPlaying(), "playback stops where the mix ends");
        bool silentAfter = true;
        for (size_t i = static_cast<size_t>(1.05 * kRate); i < out.size(); ++i) if (std::fabs(out[i]) > 1e-6f) { silentAfter = false; break; }
        check(silentAfter, "and what follows is silent (no buzz)");
    }

    // A mix with no reference and no queue also stops at its end instead of holding there.
    {
        AuditionEngine engine;
        engine.setSampleRate(kRate);
        engine.setMixSource(rampSource(0.5));
        engine.play();
        render(engine, blocksPerSecond, kBlock);
        check(!engine.isPlaying(), "a lone mix pauses at its end");
        engine.play();
        check(engine.getPositionSeconds() < 0.01, "and Play at the end starts again from the top");
        engine.pause();
    }

    // Listen-on presets are heard: choosing one changes the signal, and Studio is the untouched bypass. Nothing used
    // to turn the chain on, so every preset but Studio was a placebo (owner, 16 Nov 2026).
    {
        const auto firstSecond = [&](const AuditionDSPPreset& preset) {
            AuditionEngine engine;
            engine.setSampleRate(kRate);
            engine.setMixSource(rampSource(2.0));
            engine.setDSPPreset(preset);
            engine.play();
            return render(engine, blocksPerSecond, kBlock);
        };
        const auto studio = firstSecond(AuditionDSPPreset::Bypass());
        const auto car = firstSecond(AuditionDSPPreset::Car());
        const auto lossless = firstSecond(AuditionDSPPreset::Lossless());
        double carDiff = 0.0, losslessDiff = 0.0;
        for (size_t i = 0; i < studio.size(); ++i) {
            carDiff = std::max(carDiff, static_cast<double>(std::fabs(car[i] - studio[i])));
            losslessDiff = std::max(losslessDiff, static_cast<double>(std::fabs(lossless[i] - studio[i])));
        }
        check(carDiff > 0.01, "the Car preset changes what is heard");
        check(losslessDiff > 0.001, "the Lossless preset changes what is heard");

        AuditionEngine back;
        back.setSampleRate(kRate);
        back.setMixSource(rampSource(2.0));
        back.setDSPPreset(AuditionDSPPreset::Car());
        back.setDSPPreset(AuditionDSPPreset::Bypass());
        back.play();
        const auto again = render(back, blocksPerSecond, kBlock);
        double drift = 0.0;
        for (size_t i = 0; i < studio.size(); ++i) drift = std::max(drift, static_cast<double>(std::fabs(again[i] - studio[i])));
        check(drift < 1e-6, "Studio after another preset is the untouched signal again");
    }

    if (g_failures) { std::printf("%d check(s) failed\n", g_failures); return 1; }
    std::printf("AuditionReferenceEndTest: all checks passed\n");
    return 0;
}
