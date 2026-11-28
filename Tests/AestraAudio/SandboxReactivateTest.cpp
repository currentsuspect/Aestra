// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// SandboxReactivateTest — deactivating and activating a sandboxed CLAP plugin leaves it working.
//
// A double stop calls AudioEngine::panic(), which calls EffectChain::reset(): deactivate() then activate() on every plugin,
// to clear delay lines and reverb tails. For a CLAP plugin in the sandbox helper, DEACTIVATE really deactivated it, but
// ACTIVATE only set a flag and never activated the plugin again, so the next audio block failed. The app treats a failed
// block as a crash and bypasses the plugin for good. Seen on the real RF-201 delay: after the reset every block returned ERR.
// This drives the helper's fake CLAP plugin through the real ClapModule.

#include "Plugin/OutOfProcessPluginInstance.h"
#include "Plugin/PluginFactory.h"
#include "Plugin/PluginHostTestHooks.h"

#include <cstdint>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
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

PluginInfo fakeClapInfo() {
    PluginInfo info;
    info.id = "__aestra_test_clap_midi__";
    info.name = info.id;
    info.vendor = "Aestra Test";
    info.version = "1";
    info.category = "Effect";
    info.format = PluginFormat::CLAP;
    info.type = PluginType::Effect;
    info.path = "/tmp/aestra-test-plugin.clap";
    info.numAudioInputs = 2;
    info.numAudioOutputs = 2;
    return info;
}

std::string blockCommand(uint32_t frames) {
    std::vector<float> samples(static_cast<size_t>(frames) * 2, 0.25f);
    static const char* digits = "0123456789ABCDEF";
    std::string hex;
    const auto* bytes = reinterpret_cast<const unsigned char*>(samples.data());
    for (size_t i = 0; i < samples.size() * sizeof(float); ++i) {
        hex.push_back(digits[bytes[i] >> 4]);
        hex.push_back(digits[bytes[i] & 15]);
    }
    return "PROCESS 2 " + std::to_string(frames) + " " + hex;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || !std::filesystem::exists(argv[1])) {
        std::cout << "AestraPluginHost not given or not found; skipping.\n";
        return 77;
    }
    OutOfProcessPluginFactory factory(argv[1]);
    PluginInstancePtr created;
    factory.createPluginAsync(fakeClapInfo(), [&](PluginInstancePtr instance) { created = std::move(instance); });
    if (!created || !created->initialize(48000.0, 256)) {
        std::cerr << "could not start the fake CLAP plugin\n";
        return 1;
    }
    created->activate();
    auto& instance = *static_cast<OutOfProcessPluginInstance*>(created.get());
    const std::string block = blockCommand(64);

    check(instance.sendRawCommandForTest(block).rfind("OK", 0) == 0, "premise: an active plugin processes a block");

    // What EffectChain::reset() does to every plugin on a double stop.
    created->deactivate();
    created->activate();

    check(!created->isCrashed(), "after deactivate and activate the plugin is not marked crashed");
    const std::string after = instance.sendRawCommandForTest(block);
    check(after.rfind("OK", 0) == 0, "and it processes the next block (got: " + after.substr(0, 40) + ")");
    check(instance.sendRawCommandForTest(block).rfind("OK", 0) == 0, "and the one after");

    // The reset runs on the main thread while the audio thread may still hand the plugin a block that was already queued.
    // That block reaches the helper while the plugin is deactivated; it must be dropped, not taken for a crash.
    created->deactivate();
    {
        float inL[64] = {}, inR[64] = {}, outL[64] = {}, outR[64] = {};
        const float* in[2] = {inL, inR};
        float* out[2] = {outL, outR};
        for (int i = 0; i < 8; ++i) {
            instance.process(in, out, 2, 2, 64);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    check(!created->isCrashed(), "blocks that reach a deactivated plugin are dropped, not taken for a crash");
    created->activate();
    check(instance.sendRawCommandForTest(block).rfind("OK", 0) == 0, "and the plugin works once it is activated again");

    // The race itself, hammered: the audio path keeps handing blocks over while the plugin is deactivated and activated over
    // and over, as a double stop does. Checking "is it active" and then sending is not enough when deactivate() can run in
    // between; the helper answers ERR for a deactivated plugin and the failure marks the plugin crashed. Many cycles, because
    // the window is narrow.
    {
        created->activate();
        float inL[64] = {}, inR[64] = {}, outL[64] = {}, outR[64] = {};
        const float* in[2] = {inL, inR};
        float* out[2] = {outL, outR};
        std::atomic<bool> stop{false};
        std::thread audio([&] {
            while (!stop.load()) {
                instance.process(in, out, 2, 2, 64);
                std::this_thread::sleep_for(std::chrono::microseconds(300));
            }
        });
        for (int cycle = 0; cycle < 300 && !created->isCrashed(); ++cycle) {
            created->deactivate();
            created->activate();
        }
        stop = true;
        audio.join();
        check(!created->isCrashed(), "300 deactivate/activate cycles under audio traffic never mark the plugin crashed");
    }

    // Activating a plugin that is already active changes nothing.
    created->activate();
    check(instance.sendRawCommandForTest(block).rfind("OK", 0) == 0, "activating an active plugin again leaves it working");

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "sandbox reactivate passed\n";
    return 0;
}
