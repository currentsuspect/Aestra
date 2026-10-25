// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The plugin compatibility set (V82-H7, SPEC-019, FD-04): each plugin of the set,
// in each format, through the same sandbox the app uses. For every one that is
// installed: the scan finds it, it loads, it makes sound (an instrument plays a
// note, an effect is fed a tone), a parameter change survives save and reload
// into a fresh instance, and, with a display, its window opens and closes.
//
// A plugin that is not installed is SKIP, not FAIL, unless AESTRA_COMPAT_REQUIRE
// names it (a comma list of labels, for CI where the set is installed on purpose).
//
//   AestraPluginCompatTest [report.md]
//
// The report is a markdown table for the vault's compatibility page.

#include "OutOfProcessPluginInstance.h"
#include "PluginManager.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr uint32_t kBlock = 512;
constexpr uint32_t kChannels = 2;

struct Entry {
    const char* label; ///< what the report calls it
    const char* name;  ///< the name the scan gives it
    PluginFormat format;
    bool instrument;
};

// FD-04's set. Names are as the plugins report them.
const Entry kSet[] = {
    {"Surge XT", "Surge XT", PluginFormat::VST3, true},
    {"Surge XT", "Surge XT", PluginFormat::CLAP, true},
    {"Vital", "Vital", PluginFormat::VST3, true},
    {"Vital", "Vital", PluginFormat::CLAP, true},
    {"Dexed", "Dexed", PluginFormat::VST3, true},
    {"Dexed", "Dexed", PluginFormat::CLAP, true},
    {"TAL-NoiseMaker", "TAL-NoiseMaker", PluginFormat::VST3, true},
    {"TAL-NoiseMaker", "TAL-NoiseMaker", PluginFormat::CLAP, true},
    {"LSP Compressor", "Compressor Stereo", PluginFormat::VST3, false},
    {"LSP Compressor", "Compressor Stereo", PluginFormat::CLAP, false},
    {"Airwindows", "Airwindows Consolidated", PluginFormat::VST3, false},
    {"Airwindows", "Airwindows Consolidated", PluginFormat::CLAP, false},
};

const char* formatName(PluginFormat f) {
    return f == PluginFormat::VST3 ? "VST3" : f == PluginFormat::CLAP ? "CLAP" : "Internal";
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

enum class Mark { Pass, Fail, Skip };
const char* markText(Mark m) {
    return m == Mark::Pass ? "pass" : m == Mark::Fail ? "FAIL" : "-";
}

struct Result {
    const Entry* entry = nullptr;
    bool installed = false;
    Mark load = Mark::Skip, sound = Mark::Skip, state = Mark::Skip, editor = Mark::Skip;
    double peakDb = -200.0;
    uint64_t helperBlocks = 0, dryBlocks = 0, unsentBlocks = 0;
    uint32_t latency = 0;
    std::vector<std::string> notes;
    bool failed() const {
        return load == Mark::Fail || sound == Mark::Fail || state == Mark::Fail || editor == Mark::Fail;
    }
};

/// Buffers for one block, and a running clock so blocks go at the pace the engine sends them.
struct Runner {
    std::vector<float> in[kChannels], out[kChannels];
    const float* inPtr[kChannels]{};
    float* outPtr[kChannels]{};
    double phase = 0.0;
    std::chrono::steady_clock::time_point next = std::chrono::steady_clock::now();

    Runner() {
        for (uint32_t c = 0; c < kChannels; ++c) {
            in[c].assign(kBlock, 0.0f);
            out[c].assign(kBlock, 0.0f);
            inPtr[c] = in[c].data();
            outPtr[c] = out[c].data();
        }
    }

    /// One block. [tone] feeds a 440 Hz sine at -12 dBFS; returns the block's peak (NaN if any sample is not finite).
    float block(IPluginInstance& p, bool tone, const MidiBuffer* midi) {
        for (uint32_t i = 0; i < kBlock; ++i) {
            const float v = tone ? 0.25f * static_cast<float>(std::sin(phase)) : 0.0f;
            phase += 2.0 * M_PI * 440.0 / kSampleRate;
            for (uint32_t c = 0; c < kChannels; ++c)
                in[c][i] = v;
        }
        p.process(inPtr, outPtr, kChannels, kChannels, kBlock, midi, nullptr);
        float peak = 0.0f;
        for (uint32_t c = 0; c < kChannels; ++c) {
            for (float v : out[c]) {
                if (!std::isfinite(v))
                    return NAN;
                peak = std::max(peak, std::fabs(v));
            }
        }
        next += std::chrono::microseconds(static_cast<int64_t>(1e6 * kBlock / kSampleRate));
        std::this_thread::sleep_until(next);
        return peak;
    }

    void run(IPluginInstance& p, int blocks) {
        for (int i = 0; i < blocks; ++i)
            block(p, false, nullptr);
    }
};

PluginInstancePtr open(PluginManager& manager, const PluginInfo& info, std::string& why) {
    auto p = manager.createInstance(info);
    if (!p) {
        why = "createInstance failed";
        return nullptr;
    }
    if (!p->initialize(kSampleRate, kBlock)) {
        why = "initialize failed";
        return nullptr;
    }
    p->activate();
    if (p->isCrashed()) {
        why = "crashed while loading";
        return nullptr;
    }
    return p;
}

/// The first parameter a user could move: automatable, writable, not bypass.
const PluginParameter* movable(const std::vector<PluginParameter>& params) {
    for (const auto& prm : params) {
        if (prm.isAutomatable && !prm.isReadOnly && !prm.isBypass)
            return &prm;
    }
    return nullptr;
}

void check(PluginManager& manager, const PluginInfo& info, Result& r) {
    const bool inst = r.entry->instrument;
    std::string why;
    auto p = open(manager, info, why);
    if (!p) {
        r.load = Mark::Fail;
        r.notes.push_back(why);
        return;
    }
    r.load = Mark::Pass;

    // Sound: about 1.5 s. An instrument gets C4 at velocity 100 for one second.
    Runner run;
    run.run(*p, 4); // let the sandbox's one-block pipeline fill
    MidiBuffer on, off;
    const uint8_t noteOn[3] = {0x90, 60, 100}, noteOff[3] = {0x80, 60, 0};
    on.addEvent(0, noteOn, 3, 1);
    off.addEvent(0, noteOff, 3, 1);
    float peak = 0.0f;
    bool finite = true;
    const int kBlocks = static_cast<int>(1.5 * kSampleRate / kBlock);
    for (int i = 0; i < kBlocks; ++i) {
        const MidiBuffer* midi = !inst ? nullptr : i == 0 ? &on : i == kBlocks * 2 / 3 ? &off : nullptr;
        const float b = run.block(*p, !inst, midi);
        if (std::isnan(b))
            finite = false;
        else
            peak = std::max(peak, b);
    }
    r.peakDb = peak > 0.0f ? 20.0 * std::log10(peak) : -200.0;
    r.latency = p->getLatencySamples();
    if (auto* oop = dynamic_cast<OutOfProcessPluginInstance*>(p.get())) {
        r.helperBlocks = oop->helperBlockCount();
        r.dryBlocks = oop->dryBlockCount();
        r.unsentBlocks = oop->unsentBlockCount();
        if (!oop->usesSharedAudio())
            r.notes.push_back("audio on the hex transport (no shared block)");
    }
    if (!finite) {
        r.sound = Mark::Fail;
        r.notes.push_back("output not finite");
    } else if (r.helperBlocks == 0) {
        r.sound = Mark::Fail;
        r.notes.push_back("the helper never answered a block (all dry)");
    } else if (r.peakDb < -60.0) {
        r.sound = Mark::Fail;
        r.notes.push_back(inst ? "a note played nothing" : "the tone came out silent");
    } else {
        r.sound = Mark::Pass;
    }
    if (p->isCrashed()) {
        r.sound = Mark::Fail;
        r.notes.push_back("crashed while playing");
        return;
    }

    // State: move one parameter, save, load into a fresh instance, read it back.
    const auto params = p->getParameters();
    const PluginParameter* prm = movable(params);
    if (!prm) {
        const auto blob = p->saveState();
        r.state = blob.empty() ? Mark::Fail : Mark::Pass;
        r.notes.push_back(blob.empty() ? "no parameters, empty state" : "no movable parameter: state saved only");
    } else {
        const uint32_t id = prm->id;
        const std::string pname = prm->name;
        const float before = p->getParameter(id);
        const float target = before < 0.5f ? 0.8f : 0.2f;
        p->setParameter(id, target);
        run.run(*p, 20); // CLAP values ride in with process calls
        const float set = p->getParameter(id);
        const auto blob = p->saveState();
        if (blob.empty()) {
            r.state = Mark::Fail;
            r.notes.push_back("saveState returned nothing");
        } else if (std::fabs(set - before) < 1e-4f) {
            r.state = Mark::Fail;
            r.notes.push_back("setting \"" + pname + "\" did not change it");
        } else {
            auto q = open(manager, info, why);
            if (!q || !q->loadState(blob)) {
                r.state = Mark::Fail;
                r.notes.push_back("fresh instance did not take the state" + (q ? std::string() : ": " + why));
            } else {
                Runner run2;
                run2.run(*q, 20);
                const float back = q->getParameter(id);
                if (std::fabs(back - set) > 0.01f) {
                    r.state = Mark::Fail;
                    std::ostringstream n;
                    n << "\"" << pname << "\" saved " << set << ", came back " << back;
                    r.notes.push_back(n.str());
                } else {
                    r.state = Mark::Pass;
                }
                q->deactivate();
                q->shutdown();
            }
        }
    }

    // Editor: only with a display (the helper makes the window).
    if (std::getenv("DISPLAY") && p->hasEditor()) {
        if (!p->openEditor(nullptr)) {
            r.editor = Mark::Fail;
            r.notes.push_back("editor did not open");
        } else {
            const auto [w, h] = p->getEditorSize();
            run.run(*p, 50); // half a second with the window up, audio running
            p->closeEditor();
            run.run(*p, 5);
            if (w <= 0 || h <= 0) {
                r.editor = Mark::Fail;
                r.notes.push_back("editor opened with no size");
            } else if (p->isCrashed()) {
                r.editor = Mark::Fail;
                r.notes.push_back("crashed with the editor open");
            } else {
                r.editor = Mark::Pass;
            }
        }
    }
    p->deactivate();
    p->shutdown();
}

std::vector<std::string> required() {
    std::vector<std::string> out;
    if (const char* env = std::getenv("AESTRA_COMPAT_REQUIRE")) {
        std::stringstream ss(env);
        std::string item;
        while (std::getline(ss, item, ',')) {
            if (!item.empty())
                out.push_back(lower(item));
        }
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
#ifdef HELPER_PATH
    setenv("AESTRA_PLUGIN_HOST_PATH", HELPER_PATH, 0);
#endif
    auto& manager = PluginManager::getInstance();
    if (!manager.initialize()) {
        std::cerr << "PluginManager failed to initialise\n";
        return 1;
    }
    auto& scanner = manager.getScanner();
    // Plugins under the home folder are untrusted until the user allows them; the
    // run stands in for that "Allow" (the app asks once per plugin).
    scanner.setFirstLoadWarningCallback([](const std::filesystem::path&, const std::string&) { return true; });
    const auto found = scanner.scanBlocking();
    std::cout << "Scan found " << found.size() << " plugins\n";
    for (const auto& p : found) {
        if (p.format != PluginFormat::Internal) {
            std::cout << "  " << formatName(p.format) << "  " << p.name << "  (" << p.path.string() << ")\n";
        }
    }

    const auto must = required();
    std::vector<Result> results;
    for (const auto& e : kSet) {
        Result r;
        r.entry = &e;
        const PluginInfo* info = nullptr;
        for (const auto& p : found) {
            if (p.format == e.format && lower(p.name) == lower(e.name)) {
                info = &p;
                break;
            }
        }
        r.installed = info != nullptr;
        std::cout << "\n== " << e.label << " (" << formatName(e.format) << ") ==\n";
        if (!info) {
            const bool needed = std::find(must.begin(), must.end(), lower(e.label)) != must.end();
            if (needed) {
                r.load = Mark::Fail;
                r.notes.push_back("required but not found by the scan");
            } else {
                r.notes.push_back("not installed");
            }
        } else {
            check(manager, *info, r);
        }
        for (const auto& n : r.notes)
            std::cout << "  " << n << "\n";
        results.push_back(std::move(r));
    }

    std::ostringstream table;
    table << "| Plugin | Format | Loads | Sound | State | Editor | Peak | Latency | Dry blocks | Notes |\n";
    table << "|---|---|---|---|---|---|---|---|---|---|\n";
    int failures = 0;
    for (const auto& r : results) {
        if (r.failed())
            ++failures;
        table << "| " << r.entry->label << " | " << formatName(r.entry->format) << " | ";
        if (!r.installed && r.load != Mark::Fail) {
            table << "not installed | | | | | | | |\n";
            continue;
        }
        std::ostringstream peak;
        peak.setf(std::ios::fixed);
        peak.precision(1);
        peak << r.peakDb << " dB";
        std::string notes;
        for (const auto& n : r.notes)
            notes += (notes.empty() ? "" : "; ") + n;
        table << markText(r.load) << " | " << markText(r.sound) << " | " << markText(r.state) << " | "
              << markText(r.editor) << " | " << (r.load == Mark::Pass ? peak.str() : "") << " | " << r.latency << " | "
              << r.dryBlocks << " of " << (r.dryBlocks + r.helperBlocks) << " | " << notes << " |\n";
    }
    std::cout << "\n" << table.str();
    if (argc > 1) {
        std::ofstream(argv[1]) << table.str();
    }
    manager.shutdown();
    std::cout << (failures ? "\nFAILED: " : "\nOK: ") << failures << " failing row(s)\n";
    return failures ? 1 : 0;
}
