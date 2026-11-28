// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// SandboxEditorStressTest — an instrument that is playing notes keeps its editor and survives a save, however often both happen.
//
// Reported by the founder with Dexed and TAL-NoiseMaker: the plugin "disappears" when the project is saved or the editor is
// reopened, and the window falls back to the generic editor ("has an editor but the host would not open it"). This drives the
// app's real sandbox instance (worker thread, IPC lock, shared memory, MIDI) the way a session does: an audio-like thread
// plays notes continuously while the main thread opens the editor, saves state and closes it, over and over.
//
// It needs a real plugin and an X server, so it runs only when AESTRA_STRESS_PLUGIN (a .clap path) and
// AESTRA_STRESS_PLUGIN_ID are set, and skips otherwise (CI).

#include "Plugin/OutOfProcessPluginInstance.h"
#include "Plugin/PluginFactory.h"

#include <X11/Xlib.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace Aestra::Audio;

int main(int argc, char** argv) {
    const char* pluginPath = std::getenv("AESTRA_STRESS_PLUGIN");
    const char* pluginId = std::getenv("AESTRA_STRESS_PLUGIN_ID");
    if (argc < 2 || !pluginPath || !pluginId || !std::filesystem::exists(argv[1]) || !std::filesystem::exists(pluginPath)) {
        std::cout << "AESTRA_STRESS_PLUGIN / AESTRA_STRESS_PLUGIN_ID not set (or helper missing); skipping.\n";
        return 77;
    }
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        std::cout << "no X display; skipping.\n";
        return 77;
    }
    const ::Window parent = XCreateSimpleWindow(display, DefaultRootWindow(display), 40, 40, 1200, 800, 0, 0, 0);
    XMapWindow(display, parent);
    XFlush(display);

    PluginInfo info;
    info.id = pluginId;
    info.name = pluginId;
    info.vendor = "stress";
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
    if (!created || !created->initialize(44100.0, 256)) {
        std::cerr << "could not start " << pluginId << "\n";
        return 1;
    }
    created->activate();
    auto& plugin = *static_cast<OutOfProcessPluginInstance*>(created.get());

    // The engine's side: a block every 5.8 ms, with a note on and off, forever.
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> blocks{0};
    std::thread audio([&] {
        std::vector<float> outL(256), outR(256);
        float* out[2] = {outL.data(), outR.data()};
        uint64_t block = 0;
        while (!stop.load()) {
            MidiBuffer midi;
            const uint8_t noteOn[3] = {0x90, 60, 100};
            const uint8_t noteOff[3] = {0x80, 60, 0};
            if (block % 70 == 0) midi.addEvent(0, noteOn, 3);
            if (block % 70 == 35) midi.addEvent(0, noteOff, 3);
            plugin.process(nullptr, out, 0, 2, 256, &midi, nullptr);
            ++block;
            blocks = block;
            std::this_thread::sleep_for(std::chrono::microseconds(5800));
        }
    });

    // The UI's side: editors and the preset bar read parameters from the UI thread all the time. A reply that arrives after
    // its timeout is read as the answer to the NEXT command, so this is where a desynchronised conversation shows up.
    const size_t expectedParams = plugin.getParameters().size();
    std::atomic<int> badParamLists{0};
    std::atomic<uint64_t> uiCalls{0};
    std::thread ui([&] {
        while (!stop.load()) {
            if (plugin.getParameters().size() != expectedParams) ++badParamLists;
            plugin.getParameter(0);
            ++uiCalls;
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    });

    // A deliberate refusal, to see the reason in the log: a parent window that does not exist.
    if (plugin.openEditorEmbedded(0x7fffffffu) != 0) std::cout << "note: the helper embedded into a nonexistent window\n";
    plugin.closeEditor();

    int refused = 0, emptyState = 0, crashed = 0;
    const int cycles = 30;
    for (int i = 0; i < cycles; ++i) {
        const auto child = plugin.openEditorEmbedded(static_cast<std::uintptr_t>(parent));
        if (child == 0) {
            ++refused;
            std::cout << "cycle " << i << ": editor REFUSED (crashed=" << plugin.isCrashed() << ", hasEditor=" << plugin.hasEditor()
                      << ")\n";
        } else {
            XMapWindow(display, static_cast<::Window>(child));
            XFlush(display);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        if (i % 2 == 0 && plugin.saveState().empty()) ++emptyState; // a project save in the middle
        if (plugin.isCrashed()) {
            ++crashed;
            std::cout << "cycle " << i << ": plugin marked CRASHED\n";
            break;
        }
        plugin.closeEditor();
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
    stop = true;
    audio.join();
    ui.join();

    if (std::getenv("AESTRA_STRESS_KILL_HELPER")) {
        // What a dead helper looks like from the app: kill it, then ask for the editor.
        std::system("pkill -x AestraPluginHos");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::cout << "after killing the helper, openEditorEmbedded returned " << plugin.openEditorEmbedded(static_cast<std::uintptr_t>(parent)) << "\n";
        std::cout << "and openEditor returned " << plugin.openEditor(nullptr) << "\n";
    }
    std::cout << cycles << " cycles, " << blocks.load() << " audio blocks: refused opens " << refused << ", empty saves "
              << emptyState << ", crashed " << crashed << ", crashed at end " << plugin.isCrashed() << "; " << uiCalls.load()
              << " UI parameter reads, " << badParamLists.load() << " wrong parameter lists\n";
    const bool ok = refused == 0 && crashed == 0 && !plugin.isCrashed() && badParamLists.load() == 0;
    std::cout << (ok ? "PASS" : "FAIL") << ": the plugin keeps its editor and survives saves while playing notes\n";
    XCloseDisplay(display);
    return ok ? 0 : 1;
}
