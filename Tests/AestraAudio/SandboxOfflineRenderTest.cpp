// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// SandboxOfflineRenderTest — an offline render through a sandboxed plugin is exact, not crackly.
//
// Reported by the founder: Bounce in Place with a third-party plugin crackles. The exporter renders flat out; a sandboxed
// plugin hands each block to the helper process and plays the result one chunk later, or plays that block dry when the result is
// late, which is right for live playback and wrong for a render, where every late block is baked into the audio. With
// OfflineRenderMode on, the plugin must wait for the helper's answer to each block and keep the same one-chunk delay, so the
// output is the input delayed by exactly one chunk, with no dropped or dry block. The echo fake returns its input unchanged and
// every input sample is its own position + 1, so each output sample says where it came from.

#include "Plugin/OfflineRenderMode.h"
#include "Plugin/OutOfProcessPluginInstance.h"
#include "Plugin/PluginFactory.h"
#include "Plugin/PluginHostTestHooks.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
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

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "sandbox offline render passed\n";
    return 0;
}
