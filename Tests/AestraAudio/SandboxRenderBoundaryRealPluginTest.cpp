// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// SandboxRenderBoundaryRealPluginTest — a real plugin sounds the same live before and after a render, and after a re-prepare.
//
// Reported by the founder: after Bounce in Place (and after undoing it) the plugin plays noise. This drives the app's sandbox
// instance with a real plugin: a held note live, then a flat-out offline render of the same note, then live again, then a
// re-prepare (what an undo's graph rebuild does). Roughness is the energy of the second difference relative to the signal's:
// a clean held tone is smooth, crackle and noise are not. Runs only when AESTRA_STRESS_PLUGIN and AESTRA_STRESS_PLUGIN_ID are set.

#include "Plugin/OfflineRenderMode.h"
#include "Plugin/OutOfProcessPluginInstance.h"
#include "Plugin/PluginFactory.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
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
    if (!condition) ++g_failures;
}

double roughness(const std::vector<float>& x) {
    double sig = 0.0, d2 = 0.0;
    for (size_t i = 2; i < x.size(); ++i) {
        sig += static_cast<double>(x[i]) * x[i];
        const double d = static_cast<double>(x[i]) - 2.0 * x[i - 1] + x[i - 2];
        d2 += d * d;
    }
    return sig > 1e-12 ? std::sqrt(d2 / sig) : -1.0;
}
double peak(const std::vector<float>& x) {
    double p = 0.0;
    for (float v : x) p = std::max(p, static_cast<double>(std::fabs(v)));
    return p;
}

// Plays the note live (paced like a device callback) and returns what came out after the plugin settled.
std::vector<float> playLive(OutOfProcessPluginInstance& plugin, uint32_t frames, int blocks, bool noteOn) {
    std::vector<float> all, l(frames), r(frames);
    float* out[2] = {l.data(), r.data()};
    for (int k = 0; k < blocks; ++k) {
        MidiBuffer midi;
        const uint8_t on[3] = {0x90, 57, 110};
        if (noteOn && k == 0) midi.addEvent(0, on, 3);
        plugin.process(nullptr, out, 0, 2, frames, &midi, nullptr);
        if (k >= blocks / 2) all.insert(all.end(), l.begin(), l.end());
        std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int>(frames * 1e6 / 44100.0)));
    }
    return all;
}
void allOff(OutOfProcessPluginInstance& plugin, uint32_t frames) {
    std::vector<float> l(frames), r(frames);
    float* out[2] = {l.data(), r.data()};
    for (uint8_t ch = 0; ch < 1; ++ch) {
        MidiBuffer midi;
        const uint8_t off[3] = {0xB0, 123, 0};
        midi.addEvent(0, off, 3);
        plugin.process(nullptr, out, 0, 2, frames, &midi, nullptr);
    }
    for (int k = 0; k < 200; ++k) {
        MidiBuffer none;
        plugin.process(nullptr, out, 0, 2, frames, &none, nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
}
} // namespace

int main(int argc, char** argv) {
    const char* pluginPath = std::getenv("AESTRA_STRESS_PLUGIN");
    const char* pluginId = std::getenv("AESTRA_STRESS_PLUGIN_ID");
    if (argc < 2 || !pluginPath || !pluginId || !std::filesystem::exists(argv[1]) || !std::filesystem::exists(pluginPath)) {
        std::cout << "AESTRA_STRESS_PLUGIN / AESTRA_STRESS_PLUGIN_ID not set (or helper missing); skipping.\n";
        return 77;
    }
    PluginInfo info;
    info.id = pluginId;
    info.name = pluginId;
    info.vendor = "probe";
    info.version = "1";
    info.category = "Instrument";
    info.format = PluginFormat::CLAP;
    info.type = PluginType::Instrument;
    info.path = pluginPath;
    info.numAudioInputs = 0;
    info.numAudioOutputs = 2;

    OutOfProcessPluginFactory factory(argv[1]);
    PluginInstancePtr created;
    factory.createPluginAsync(info, [&](PluginInstancePtr instance) { created = std::move(instance); });
    constexpr uint32_t kDevice = 256;
    if (!created || !created->initialize(44100.0, 512)) {
        std::cerr << "could not start " << pluginId << "\n";
        return 1;
    }
    created->activate();
    auto& plugin = *static_cast<OutOfProcessPluginInstance*>(created.get());

    const auto before = playLive(plugin, kDevice, 160, true);
    allOff(plugin, kDevice);
    const double rBefore = roughness(before);
    std::cout << "live before: roughness " << rBefore << " peak " << peak(before) << std::endl;

    // A flat-out render of the same note, in the exporter's 4096-frame blocks.
    OfflineRenderMode::setActive(true);
    {
        std::vector<float> l(4096), r(4096);
        float* out[2] = {l.data(), r.data()};
        for (int k = 0; k < 40; ++k) {
            MidiBuffer midi;
            const uint8_t on[3] = {0x90, 57, 110};
            if (k == 0) midi.addEvent(0, on, 3);
            plugin.process(nullptr, out, 0, 2, 4096, &midi, nullptr);
        }
    }
    OfflineRenderMode::setActive(false);

    allOff(plugin, kDevice);
    const auto after = playLive(plugin, kDevice, 160, true);
    allOff(plugin, kDevice);
    const double rAfter = roughness(after);
    std::cout << "live after render: roughness " << rAfter << " peak " << peak(after) << std::endl;
    check(rBefore > 0 && rAfter > 0 && rAfter < rBefore * 1.5 + 0.02,
          "the plugin is as smooth live after a render as before it");

    // An undo rebuilds the graph: every plugin is prepared again.
    plugin.initialize(44100.0, 512);
    plugin.activate();
    const auto undone = playLive(plugin, kDevice, 160, true);
    const double rUndo = roughness(undone);
    std::cout << "live after re-prepare: roughness " << rUndo << " peak " << peak(undone) << std::endl;
    check(rUndo > 0 && rUndo < rBefore * 1.5 + 0.02, "and as smooth after being prepared again");
    check(!plugin.isCrashed(), "and not crash-marked");

    // The order a producer often has: the plugin has never played live when the bounce runs, so its hand-off size is learned from
    // the render, not from the audio device.
    {
        PluginInstancePtr fresh;
        factory.createPluginAsync(info, [&](PluginInstancePtr instance) { fresh = std::move(instance); });
        if (!fresh || !fresh->initialize(44100.0, 512)) {
            std::cerr << "could not start the second instance\n";
            return 1;
        }
        fresh->activate();
        auto& second = *static_cast<OutOfProcessPluginInstance*>(fresh.get());
        OfflineRenderMode::setActive(true);
        {
            std::vector<float> l(4096), r(4096);
            float* out[2] = {l.data(), r.data()};
            for (int k = 0; k < 40; ++k) {
                MidiBuffer midi;
                const uint8_t on[3] = {0x90, 57, 110};
                if (k == 0) midi.addEvent(0, on, 3);
                second.process(nullptr, out, 0, 2, 4096, &midi, nullptr);
            }
        }
        OfflineRenderMode::setActive(false);
        allOff(second, kDevice);
        const auto live = playLive(second, kDevice, 160, true);
        const double rFresh = roughness(live);
        std::cout << "live after a render that came first: roughness " << rFresh << " peak " << peak(live)
                  << " latency " << second.getLatencySamples() << std::endl;
        check(rFresh > 0 && rFresh < rBefore * 1.5 + 0.02, "a plugin whose first audio was a render is as smooth live afterwards");
        check(second.dryBlockCount() < 8, "with next to no dry blocks (" + std::to_string(second.dryBlockCount()) + ")");
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "render boundary (real plugin) passed\n";
    return 0;
}
