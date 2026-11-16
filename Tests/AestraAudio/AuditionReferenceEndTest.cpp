// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// A reference longer than the mix must not buzz when the mix ends (owner, 16 Nov 2026). The position was held at the
// mix's end while the engine kept playing, so every block read the same instant of the longer reference again: a
// buzz at the block rate. Now the reference plays on to its own end and playback then stops, and a mix with no
// reference still stops at its end instead of holding there.

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

    // A longer reference plays on past the end of the mix, never repeating an instant, then playback stops.
    {
        AuditionEngine engine;
        engine.setSampleRate(kRate);
        engine.setMixSource(rampSource(1.0));
        engine.setReferenceSource(rampSource(3.0));
        engine.setListenSide(AuditionEngine::ListenSide::Reference);
        check(engine.getDurationSeconds() > 2.9, "the playable length is the longer of the two");
        engine.play();
        render(engine, blocksPerSecond / 2, kBlock); // let the switch ramp finish
        const auto out = render(engine, blocksPerSecond * 3, kBlock);

        // Past the mix's end (1 s) the reference is still moving forward: strictly increasing, no repeated block.
        const size_t from = static_cast<size_t>(0.6 * kRate); // 1.1 s into the song: out starts at 0.5 s
        bool rising = true;
        for (size_t i = from + 1; i < static_cast<size_t>(1.9 * kRate) && i < out.size(); ++i)
            if (out[i] < out[i - 1]) { rising = false; break; }
        check(rising, "the reference keeps moving forward after the mix ends (no repeated instant, no buzz)");
        check(!engine.isPlaying(), "playback stops at the end of the longer of the two");
        check(std::fabs(out.back()) < 1e-6f, "and the tail is silent");
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
