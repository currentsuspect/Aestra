// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// SandboxLatencyProbe — does a sandboxed plugin delay audio by what it tells PDC?
//
// Every third-party plugin runs out of process and reports getLatencySamples() for plugin delay
// compensation. The engine prepares effect chains with its block CAPACITY (at least 4096 frames),
// not the device's callback size, so this initialises the plugin the same way and then drives it
// with ordinary callback-sized blocks and an impulse. The measured delay must equal the report;
// any difference is how far the track lands off the grid against every other track.
//
// Needs installed plugins and the helper, so it runs on demand:
//   build/Tests/AestraSandboxLatencyProbe [callbackFrames]

#include "HostTransport.h"
#include "OutOfProcessPluginInstance.h"
#include "PluginManager.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr uint32_t kChannels = 2;
constexpr uint32_t kEngineCapacity = 4096; // what EffectChain::prepare passes (m_maxBufferFrames)

struct Probe {
    const char* name;
    PluginFormat format;
};
// Effects whose own latency is zero by default (no lookahead), so the whole delay is transport.
const Probe kProbes[] = {
    {"Compressor Stereo", PluginFormat::CLAP},
    {"Compressor Stereo", PluginFormat::VST3},
};

// Returns the measured delay in frames, or -1 when the impulse never came back.
long measure(IPluginInstance& p, uint32_t block) {
    std::vector<float> in[kChannels], out[kChannels];
    const float* inPtr[kChannels]{};
    float* outPtr[kChannels]{};
    for (uint32_t c = 0; c < kChannels; ++c) {
        in[c].assign(block, 0.0f);
        out[c].assign(block, 0.0f);
        inPtr[c] = in[c].data();
        outPtr[c] = out[c].data();
    }
    const uint32_t warmBlocks = 32;
    const uint32_t totalBlocks = warmBlocks + 64 + kEngineCapacity / block;
    const long impulseAt = static_cast<long>(warmBlocks) * block + 17;
    auto next = std::chrono::steady_clock::now();
    int64_t position = 0;
    long firstAt = -1; // first arrival: a ringing effect peaks later than it starts
    for (uint32_t b = 0; b < totalBlocks; ++b) {
        for (uint32_t c = 0; c < kChannels; ++c) {
            std::fill(in[c].begin(), in[c].end(), 0.0f);
        }
        const long blockStart = static_cast<long>(b) * block;
        if (impulseAt >= blockStart && impulseAt < blockStart + static_cast<long>(block)) {
            for (uint32_t c = 0; c < kChannels; ++c) {
                in[c][static_cast<size_t>(impulseAt - blockStart)] = 0.05f;
            }
        }
        HostTransportClock::publish(HostTransport::at(position, kSampleRate, 120.0, 4, true, false, 0.0, 4.0));
        position += block;
        p.process(inPtr, outPtr, kChannels, kChannels, block, nullptr, nullptr);
        for (uint32_t i = 0; i < block && firstAt < 0; ++i) {
            if (b >= warmBlocks && std::fabs(out[0][i]) > 1e-3f) {
                firstAt = blockStart + static_cast<long>(i);
            }
        }
        next += std::chrono::microseconds(static_cast<int64_t>(1e6 * block / kSampleRate));
        std::this_thread::sleep_until(next); // the engine's pace: the helper gets a real callback period
    }
    return firstAt >= 0 ? firstAt - impulseAt : -1;
}

} // namespace

int main(int argc, char** argv) {
#ifdef HELPER_PATH
    setenv("AESTRA_PLUGIN_HOST_PATH", HELPER_PATH, 0);
#endif
    const uint32_t block = argc > 1 ? static_cast<uint32_t>(std::atoi(argv[1])) : 256;
    auto& manager = PluginManager::getInstance();
    if (!manager.initialize()) {
        std::fprintf(stderr, "PluginManager failed to initialise\n");
        return 2;
    }
    auto& scanner = manager.getScanner();
    scanner.setFirstLoadWarningCallback([](const std::filesystem::path&, const std::string&) { return true; });
    const auto found = scanner.scanBlocking();

    int measured = 0;
    int mismatched = 0;
    for (const auto& probe : kProbes) {
        const PluginInfo* info = nullptr;
        for (const auto& f : found) {
            if (f.format == probe.format && f.name == probe.name) {
                info = &f;
                break;
            }
        }
        if (!info) {
            continue;
        }
        auto p = manager.createInstance(*info);
        if (!p || !p->initialize(kSampleRate, kEngineCapacity)) {
            std::printf("%-26s %s: failed to load\n", probe.name, probe.format == PluginFormat::CLAP ? "CLAP" : "VST3");
            continue;
        }
        p->activate();
        const long delay = measure(*p, block);
        const long reported = static_cast<long>(p->getLatencySamples());
        ++measured;
        const bool ok = delay == reported;
        mismatched += ok ? 0 : 1;
        std::printf("%-26s %s  callback=%u  reported=%ld  measured=%ld  %s\n", probe.name,
                    probe.format == PluginFormat::CLAP ? "CLAP" : "VST3", block, reported, delay,
                    ok ? "ok" : "MISMATCH");
        p->deactivate();
    }
    if (measured == 0) {
        std::printf("no probe plugin installed\n");
        return 2;
    }
    return mismatched == 0 ? 0 : 1;
}
