// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// SandboxAudioThreadTest — an audio block is answered while the helper's main thread is busy with its window.
//
// Measured on the real RF-16VU: with its editor open and the machine busy, 19% of blocks missed their 5.8 ms deadline
// (2.7% on an idle machine), against 0.7% / 0.4% with the editor closed. The helper ran the editor's drawing and the audio
// on one thread, so a repaint delayed the next block, and a late block plays dry, which for a compressor is a click.
// TESTPAINT <ms> keeps the helper's main thread busy after its reply, as a repaint would; the block sent right after
// must come back long before the paint ends.

#include "Plugin/OutOfProcessPluginInstance.h"
#include "Plugin/PluginFactory.h"
#include "Plugin/PluginHostTestHooks.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
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

double timedMs(OutOfProcessPluginInstance& instance, const std::string& command, std::string& reply) {
    const auto start = std::chrono::steady_clock::now();
    reply = instance.sendRawCommandForTest(command);
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
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
    if (!created || !created->initialize(48000.0, 256)) {
        std::cerr << "could not start the echo plugin\n";
        return 1;
    }
    created->activate();
    auto& instance = *static_cast<OutOfProcessPluginInstance*>(created.get());
    const std::string block = blockCommand(64);
    std::string reply;

    // Premise: with nothing else going on, a block comes straight back.
    const double idleMs = timedMs(instance, block, reply);
    check(reply.rfind("OK", 0) == 0, "premise: an idle helper answers a block");
    check(idleMs < 200.0, "premise: and does it quickly");

    // The case: the helper's main thread is busy for 600 ms right after it replies.
    check(instance.sendRawCommandForTest("TESTPAINT 600") == "OK", "the helper is asked to stay busy for 600 ms");
    const double busyMs = timedMs(instance, block, reply);
    check(reply.rfind("OK", 0) == 0, "a block sent while the main thread is busy is still answered");
    check(busyMs < 200.0, "and long before the busy period ends (took " + std::to_string(busyMs) + " ms)");

    // Afterwards everything is back to normal, in order.
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    check(instance.sendRawCommandForTest("PING").rfind("OK", 0) == 0, "an ordinary command works after the busy period");
    check(timedMs(instance, block, reply) < 200.0 && reply.rfind("OK", 0) == 0, "and so does the next block");

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "sandbox audio thread passed\n";
    return 0;
}
