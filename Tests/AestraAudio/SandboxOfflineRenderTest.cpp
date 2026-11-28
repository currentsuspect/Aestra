// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// SandboxOfflineRenderTest — an offline render through a sandboxed plugin is exact, not crackly.
//
// Reported by the founder: Bounce in Place with a third-party plugin crackles. The exporter renders flat out; a sandboxed
// plugin hands each block to the helper process and plays the result one chunk later, or plays that block dry when the result is
// late, which is right for live playback and wrong for a render, where every late block is baked into the audio. With
// OfflineRenderMode on, the plugin must wait for the helper's answer to each block and keep the same one-chunk delay, so the
// output is the input delayed by exactly one chunk, with no dropped or dry block. The echo fake returns its input unchanged and
// every input sample is its own position + 1, so each output sample says where it came from.

#include "Plugin/MidiSlice.h"
#include "Plugin/OfflineRenderMode.h"
#include "Plugin/OutOfProcessPluginInstance.h"
#include "Plugin/PluginFactory.h"
#include "Plugin/PluginHostTestHooks.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace Aestra::Audio;

namespace {
int g_failures = 0;

void check(bool condition, const std::string& label) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << label << std::endl;
    if (!condition) {
        ++g_failures;
    }
}

PluginInfo echoInfo() {
    PluginInfo info;
    info.id = "__aestra_test_echo__";
    info.name = info.id;
    info.vendor = "Aestra Test";
    info.version = "1";
    info.category = "Effect";
    info.format = PluginFormat::VST3;
    info.type = PluginType::Effect;
    info.path = "/tmp/aestra-test-plugin.vst3";
    info.numAudioInputs = 2;
    info.numAudioOutputs = 2;
    return info;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || !std::filesystem::exists(argv[1])) {
        std::cout << "AestraPluginHost not given or not found; skipping.\n";
        return 77;
    }
    OutOfProcessPluginFactory factory(argv[1]);
    PluginInstancePtr created;
    factory.createPluginAsync(echoInfo(), [&](PluginInstancePtr instance) { created = std::move(instance); });
    constexpr uint32_t kFrames = 64;
    if (!created || !created->initialize(48000.0, kFrames)) {
        std::cerr << "could not start the echo plugin\n";
        return 1;
    }
    created->activate();
    auto& plugin = *static_cast<OutOfProcessPluginInstance*>(created.get());

    check(!OfflineRenderMode::active(), "premise: a render is not active until the exporter says so");
    OfflineRenderMode::setActive(true);

    // Flat out, no pacing: what an offline render does. Every input sample is its own position + 1.
    constexpr int kBlocks = 400;
    int wrongSamples = 0, wrongSilence = 0;
    for (int k = 0; k < kBlocks; ++k) {
        std::vector<float> inL(kFrames), inR(kFrames), outL(kFrames, -1.0f), outR(kFrames, -1.0f);
        for (uint32_t i = 0; i < kFrames; ++i) {
            inL[i] = inR[i] = static_cast<float>(static_cast<uint64_t>(k) * kFrames + i + 1);
        }
        const float* in[2] = {inL.data(), inR.data()};
        float* out[2] = {outL.data(), outR.data()};
        plugin.process(in, out, 2, 2, kFrames, nullptr, nullptr);
        for (uint32_t i = 0; i < kFrames; ++i) {
            // The plugin runs one chunk behind: block k carries block k-1's samples, and block 0 is silence.
            const float expected = k == 0 ? 0.0f : static_cast<float>(static_cast<uint64_t>(k - 1) * kFrames + i + 1);
            if (outL[i] != expected || outR[i] != expected) {
                if (k == 0) ++wrongSilence;
                else ++wrongSamples;
            }
        }
    }
    OfflineRenderMode::setActive(false);

    check(wrongSilence == 0, "the first block, before the first chunk has come back, is silence");
    check(wrongSamples == 0, "every later block is the previous block's input, exactly (" + std::to_string(wrongSamples) +
                                 " wrong samples)");
    check(plugin.dryBlockCount() == 0, "no block was played dry (" + std::to_string(plugin.dryBlockCount()) + ")");
    check(plugin.unsentBlockCount() == 0, "no block was left unsent (" + std::to_string(plugin.unsentBlockCount()) + ")");
    check(!created->isCrashed(), "and the plugin was not marked crashed");

    // --- The block an export really renders: 4096 frames, to a plugin that was initialised for at most 64 ---------------------
    // process() treats a block larger than the plugin's maximum as "cannot handle" and passes the audio straight through: silence
    // for an instrument, dry for an effect. That is what the founder's bounces did: 8 s rendered in under a second, peak -80 dB.
    // Offline, a large block is cut into slices of the plugin's own size, so the render is the same as playback with that block size.
    {
        PluginInstancePtr big;
        factory.createPluginAsync(echoInfo(), [&](PluginInstancePtr instance) { big = std::move(instance); });
        if (!big || !big->initialize(48000.0, kFrames)) {
            std::cerr << "could not start the second echo plugin\n";
            return 1;
        }
        big->activate();
        auto& bigPlugin = *static_cast<OutOfProcessPluginInstance*>(big.get());
        constexpr uint32_t kRenderBlock = 4096;
        constexpr int kRenderBlocks = 12;
        OfflineRenderMode::setActive(true);
        int wrong = 0;
        for (int k = 0; k < kRenderBlocks; ++k) {
            std::vector<float> inL(kRenderBlock), inR(kRenderBlock), outL(kRenderBlock, -1.0f), outR(kRenderBlock, -1.0f);
            for (uint32_t i = 0; i < kRenderBlock; ++i) {
                inL[i] = inR[i] = static_cast<float>(static_cast<uint64_t>(k) * kRenderBlock + i + 1);
            }
            const float* in[2] = {inL.data(), inR.data()};
            float* out[2] = {outL.data(), outR.data()};
            bigPlugin.process(in, out, 2, 2, kRenderBlock, nullptr, nullptr);
            for (uint32_t i = 0; i < kRenderBlock; ++i) {
                const uint64_t n = static_cast<uint64_t>(k) * kRenderBlock + i;
                const float expected = n < kFrames ? 0.0f : static_cast<float>(n - kFrames + 1); // one 64-frame chunk behind
                if (outL[i] != expected || outR[i] != expected) ++wrong;
            }
        }
        OfflineRenderMode::setActive(false);
        check(wrong == 0, "a 4096-frame render block through a plugin initialised for 64 is the input delayed by one chunk (" +
                              std::to_string(wrong) + " wrong samples)");
        check(bigPlugin.dryBlockCount() == 0 && bigPlugin.unsentBlockCount() == 0, "with no dry or unsent block");
    }

    // --- Starting a render after live playback: nothing of the live signal may leak into the start of the render --------------------
    // Reported by the founder: a bounce renders, but with a click in its first millisecond, and not the same click each time. The
    // plugin's FIFOs and timeline carry on from live playback, so whatever live audio was still queued came out at the start of the
    // render. A render of silence has to be silent from its first sample.
    {
        PluginInstancePtr second;
        factory.createPluginAsync(echoInfo(), [&](PluginInstancePtr instance) { second = std::move(instance); });
        if (!second || !second->initialize(48000.0, kFrames)) {
            std::cerr << "could not start the third echo plugin\n";
            return 1;
        }
        second->activate();
        auto& live = *static_cast<OutOfProcessPluginInstance*>(second.get());
        OfflineRenderMode::setActive(false);
        std::vector<float> loud(kFrames, 0.5f), sinkL(kFrames), sinkR(kFrames);
        for (int k = 0; k < 40; ++k) { // paced like a device callback
            const float* in[2] = {loud.data(), loud.data()};
            float* out[2] = {sinkL.data(), sinkR.data()};
            live.process(in, out, 2, 2, kFrames, nullptr, nullptr);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        OfflineRenderMode::setActive(true); // the exporter starts a render
        std::vector<float> silence(kFrames, 0.0f), outL(kFrames), outR(kFrames);
        int leaked = 0;
        for (int k = 0; k < 10; ++k) {
            const float* in[2] = {silence.data(), silence.data()};
            float* out[2] = {outL.data(), outR.data()};
            live.process(in, out, 2, 2, kFrames, nullptr, nullptr);
            for (uint32_t i = 0; i < kFrames; ++i) {
                if (outL[i] != 0.0f || outR[i] != 0.0f) ++leaked;
            }
        }
        OfflineRenderMode::setActive(false);
        check(leaked == 0, "a render of silence started after live playback is silent from its first sample (" +
                               std::to_string(leaked) + " samples of the live signal leaked in)");
    }

    // --- After a render the plugin must play cleanly: the exporter changes the sample rate, which re-initialises every plugin --------
    // Reported by the founder: after a bounce the plugin is so crackly it cannot be used. Re-initialising a running instance resets
    // its buffers and shared memory under the worker thread that is using them. Live audio after that must still be the input
    // delayed by exactly one chunk, with no dry block.
    {
        PluginInstancePtr third;
        factory.createPluginAsync(echoInfo(), [&](PluginInstancePtr instance) { third = std::move(instance); });
        if (!third || !third->initialize(48000.0, kFrames)) {
            std::cerr << "could not start the fourth echo plugin\n";
            return 1;
        }
        third->activate();
        auto& after = *static_cast<OutOfProcessPluginInstance*>(third.get());
        std::vector<float> a(kFrames, 0.25f), b(kFrames), c(kFrames);
        OfflineRenderMode::setActive(false);
        for (int k = 0; k < 30; ++k) {
            const float* in[2] = {a.data(), a.data()};
            float* out[2] = {b.data(), c.data()};
            after.process(in, out, 2, 2, kFrames, nullptr, nullptr);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        OfflineRenderMode::setActive(true);
        third->initialize(44100.0, kFrames); // what the exporter's sample-rate change does
        third->activate();
        for (int k = 0; k < 20; ++k) {
            const float* in[2] = {a.data(), a.data()};
            float* out[2] = {b.data(), c.data()};
            after.process(in, out, 2, 2, kFrames, nullptr, nullptr);
        }
        OfflineRenderMode::setActive(false);
        third->initialize(48000.0, kFrames); // and back at the end
        third->activate();
        const uint64_t dryBefore = after.dryBlockCount();
        int wrong = 0, blocks = 0;
        for (int k = 0; k < 300; ++k) {
            std::vector<float> ramp(kFrames), outA(kFrames, -1.0f), outB(kFrames, -1.0f);
            for (uint32_t i = 0; i < kFrames; ++i) ramp[i] = static_cast<float>(k * kFrames + i + 1);
            const float* in[2] = {ramp.data(), ramp.data()};
            float* out[2] = {outA.data(), outB.data()};
            after.process(in, out, 2, 2, kFrames, nullptr, nullptr);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (k < 8) continue; // let the hand-off settle
            ++blocks;
            // every output sample is some earlier input sample (a ramp value) or silence: never garbage, never the same value twice
            for (uint32_t i = 1; i < kFrames; ++i) {
                if (outA[i] != 0.0f && outA[i - 1] != 0.0f && outA[i] != outA[i - 1] + 1.0f) ++wrong;
            }
        }
        check(wrong == 0, "after a render's re-initialisations, live audio is a clean ramp, no garbage or repeats (" +
                              std::to_string(wrong) + " broken samples in " + std::to_string(blocks) + " blocks)");
        check(after.dryBlockCount() - dryBefore <= 2, "and almost no dry blocks (" + std::to_string(after.dryBlockCount() - dryBefore) + ")");
        check(!after.isCrashed(), "and the plugin is not crash-marked");
    }

    // --- An instrument renders on the grid: no hand-off delay in a render ---------------------------------------------------------
    // Reported by the founder: bounces start with a blank (412 frames) and pop when looped. Delay compensation covers insert
    // effects only; an instrument's one-chunk hand-off delay was in every render and nothing took it out. In a render each block
    // waits for its own answer, so an instrument's output is its input at the same position. The exporter's first block is short
    // (about 3 ms) and the rest are 4096 frames.
    {
        PluginInfo instrumentInfo = echoInfo();
        instrumentInfo.type = PluginType::Instrument;
        instrumentInfo.category = "Instrument";
        PluginInstancePtr inst;
        factory.createPluginAsync(instrumentInfo, [&](PluginInstancePtr instance) { inst = std::move(instance); });
        if (!inst || !inst->initialize(44100.0, 256)) {
            std::cerr << "could not start the instrument echo\n";
            return 1;
        }
        inst->activate();
        auto& synth = *static_cast<OutOfProcessPluginInstance*>(inst.get());
        // It played live first, at the device's 256 frames.
        OfflineRenderMode::setActive(false);
        {
            std::vector<float> z(256, 0.0f), l(256), r(256);
            const float* in[2] = {z.data(), z.data()};
            float* out[2] = {l.data(), r.data()};
            for (int k = 0; k < 20; ++k) {
                synth.process(in, out, 2, 2, 256, nullptr, nullptr);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        const uint32_t liveLatency = synth.getLatencySamples();
        OfflineRenderMode::setActive(true);
        const uint32_t blocks[] = {132, 4096, 4096, 4096, 1000};
        uint64_t pos = 0;
        int wrong = 0, firstWrong = -1;
        for (const uint32_t n : blocks) {
            std::vector<float> inL(n), outL(n, -1.0f), outR(n, -1.0f);
            for (uint32_t i = 0; i < n; ++i) inL[i] = static_cast<float>(pos + i + 1);
            const float* in[2] = {inL.data(), inL.data()};
            float* out[2] = {outL.data(), outR.data()};
            synth.process(in, out, 2, 2, n, nullptr, nullptr);
            for (uint32_t i = 0; i < n; ++i) {
                if (outL[i] != inL[i]) {
                    if (firstWrong < 0) firstWrong = static_cast<int>(pos + i);
                    ++wrong;
                }
            }
            pos += n;
        }
        OfflineRenderMode::setActive(false);
        check(wrong == 0, "a sandboxed instrument renders with no hand-off delay: output sample n is input sample n (" +
                              std::to_string(wrong) + " wrong, first at " + std::to_string(firstWrong) + ")");
        check(synth.getLatencySamples() == liveLatency, "and the render leaves its live latency as it was (" +
                                                            std::to_string(liveLatency) + " -> " +
                                                            std::to_string(synth.getLatencySamples()) + ")");
    }

    // --- Cutting MIDI into slices: nothing lost, nothing doubled, offsets rebased, note identities kept -------------------------
    {
        MidiBuffer in;
        const uint8_t on[3] = {0x90, 60, 100}, off[3] = {0x80, 60, 0};
        in.addEvent(0, on, 3, 11);
        in.addEvent(63, off, 3, 11);
        in.addEvent(64, on, 3, 12);
        in.addEvent(130, off, 3, 12);
        in.addEvent(4095, on, 3, 13);
        int total = 0;
        bool ordered = true, idsKept = true;
        const uint32_t expectedSlice[5] = {0, 0, 1, 2, 63};
        const uint32_t expectedOffset[5] = {0, 63, 0, 2, 63};
        int seen = 0;
        for (uint32_t start = 0; start < 4096; start += 64) {
            MidiBuffer slice;
            splitMidiForSlice(in, start, 64, slice);
            total += static_cast<int>(slice.getEventCount());
            for (size_t i = 0; i < slice.getEventCount(); ++i) {
                const auto& e = slice.getEvent(i);
                if (e.sampleOffset >= 64) ordered = false;
                if (seen < 5 && (start / 64 != expectedSlice[seen] || e.sampleOffset != expectedOffset[seen])) idsKept = false;
                if (e.noteId < 11 || e.noteId > 13) idsKept = false;
                ++seen;
            }
        }
        check(total == 5, "every MIDI event lands in exactly one slice (" + std::to_string(total) + " of 5)");
        check(ordered, "with its offset rebased to inside the slice");
        check(idsKept, "in the right slice, keeping its note identity");
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "sandbox offline render passed\n";
    return 0;
}
