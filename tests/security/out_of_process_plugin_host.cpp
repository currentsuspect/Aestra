// © 2026 Aestra Studios — All Rights Reserved.
// Out-of-process plugin host containment proof.

#include "Plugin/PluginFactory.h"
#include "Plugin/OutOfProcessPluginInstance.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace Aestra::Audio;

namespace {

PluginInfo makeInfo(const std::string& id) {
    PluginInfo info;
    info.id = id;
    info.name = id;
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

PluginInfo makeRealClapProbeInfo(const std::string& path) {
    PluginInfo info;
    info.id = "__aestra_probe_first__";
    info.name = "CLAP probe";
    info.vendor = "Aestra Test";
    info.version = "1";
    info.category = "Effect";
    info.format = PluginFormat::CLAP;
    info.type = PluginType::Effect;
    info.path = path;
    info.numAudioInputs = 2;
    info.numAudioOutputs = 2;
    return info;
}

PluginInstancePtr create(OutOfProcessPluginFactory& factory, const PluginInfo& info) {
    PluginInstancePtr created;
    factory.createPluginAsync(info, [&](PluginInstancePtr instance) { created = std::move(instance); });
    return created;
}

// Pump process() until the echo output steady-states at input*gain, tolerating
// the async worker/double-buffer latency (#238 param delivery is not synchronous).
// Returns true once every frame matches; false if it never converges.
bool pumpUntilGain(const PluginInstancePtr& instance, const float* in, float gain, int tries = 100) {
    float outL[4] = {};
    float outR[4] = {};
    float* outputs[2] = {outL, outR};
    const float* inputs[2] = {in, in};
    for (int t = 0; t < tries; ++t) {
        std::memset(outL, 0, sizeof(outL));
        std::memset(outR, 0, sizeof(outR));
        instance->process(inputs, outputs, 2, 2, 4);
        bool converged = true;
        for (int i = 0; i < 4; ++i) {
            if (std::fabs(outL[i] - in[i] * gain) > 1e-5f || std::fabs(outR[i] - in[i] * gain) > 1e-5f) {
                converged = false;
            }
        }
        if (converged) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

// --- #244 CLAP note-dialect e2e helpers -----------------------------------
PluginInfo makeClapInfo(const std::string& id) {
    PluginInfo info;
    info.id = id;
    info.name = id;
    info.vendor = "Aestra Test";
    info.version = "1";
    info.category = "Instrument";
    info.format = PluginFormat::CLAP;
    info.type = PluginType::Instrument;
    info.path = "/tmp/aestra-fake.clap";
    info.numAudioInputs = 2;
    info.numAudioOutputs = 2;
    return info;
}

struct DeliveredEvent {
    int type = -1; // 0 note-on, 1 note-off, 10 raw MIDI (child CLAP event type ids)
    uint32_t time = 0;
    int port = 0;
    int channel = 0;
    int key = 0;
    double velocity = 0.0;
    int midi[3] = {0, 0, 0};
};

// Parse the child's TESTNOTES reply: "OK <n> type:time:port:ch:key:vel:m0:m1:m2 ...".
std::vector<DeliveredEvent> parseTestNotes(const std::string& reply) {
    std::vector<DeliveredEvent> out;
    std::istringstream in(reply);
    std::string ok;
    size_t n = 0;
    in >> ok >> n;
    if (ok != "OK") {
        return out;
    }
    std::string tok;
    while (in >> tok) {
        DeliveredEvent e;
        std::replace(tok.begin(), tok.end(), ':', ' ');
        std::istringstream ts(tok);
        ts >> e.type >> e.time >> e.port >> e.channel >> e.key >> e.velocity >> e.midi[0] >> e.midi[1] >>
            e.midi[2];
        out.push_back(e);
    }
    return out;
}

// Load a fake CLAP endpoint, deliver one MIDI block (note-on ch4 key60 vel100 @0,
// CC @2, note-off ch4 key60 @4), and return the events the plugin actually
// received through process.in_events.
std::vector<DeliveredEvent> deliverNotesToFakeClap(OutOfProcessPluginFactory& factory,
                                                   const std::string& id, bool& ok) {
    ok = false;
    auto instance = create(factory, makeClapInfo(id));
    if (!instance || !instance->initialize(48000.0, 64)) {
        return {};
    }
    instance->activate();
    if (!instance->isActive() || instance->isCrashed()) {
        return {};
    }

    MidiBuffer midi;
    midi.addNoteOn(4, 60, 100, 0);                                    // ch4(=MIDI ch3), key60, vel100 @0
    const uint8_t cc[3] = {static_cast<uint8_t>(0xB0 | 3), 7, 64};    // CC7 on ch3 @2
    midi.addEvent(2, cc, 3);
    midi.addNoteOff(4, 60, 0, 4);                                     // note-off @4

    float inL[64] = {};
    float inR[64] = {};
    const float* inputs[2] = {inL, inR};
    float outL[64] = {};
    float outR[64] = {};
    float* outputs[2] = {outL, outR};
    instance->process(inputs, outputs, 2, 2, 64, &midi, nullptr);

    // Poll until the worker has forwarded the block (PROCESSMIDI) and the fake
    // recorded it — tolerating async worker latency rather than a fixed sleep.
    auto* oop = static_cast<OutOfProcessPluginInstance*>(instance.get());
    std::vector<DeliveredEvent> events;
    for (int t = 0; t < 100; ++t) {
        events = parseTestNotes(oop->sendRawCommandForTest("TESTNOTES"));
        if (!events.empty()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    instance->shutdown();
    ok = true;
    return events;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: SecOutOfProcessPluginHost <AestraPluginHost path> [real CLAP path]\n";
        return 2;
    }

    OutOfProcessPluginFactory factory(argv[1]);

    auto instance = create(factory, makeInfo("__aestra_test_echo__"));
    if (!instance) {
        std::cerr << "failed to create isolated echo plugin\n";
        return 1;
    }

    if (!instance->initialize(48000.0, 64)) {
        std::cerr << "failed to initialize isolated echo plugin\n";
        return 1;
    }
    instance->activate();
    if (!instance->isActive() || instance->isCrashed()) {
        std::cerr << "isolated echo plugin not active\n";
        return 1;
    }

    float inL[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    float inR[4] = {-0.1f, -0.2f, -0.3f, -0.4f};
    const float* inputs[2] = {inL, inR};
    float outL[4] = {};
    float outR[4] = {};
    float* outputs[2] = {outL, outR};
    instance->process(inputs, outputs, 2, 2, 4);

    if (std::memcmp(inL, outL, sizeof(inL)) != 0 || std::memcmp(inR, outR, sizeof(inR)) != 0) {
        std::cerr << "isolated plugin proxy did not pass through audio safely\n";
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    float nextInL[4] = {0.9f, 0.8f, 0.7f, 0.6f};
    float nextInR[4] = {-0.9f, -0.8f, -0.7f, -0.6f};
    const float* nextInputs[2] = {nextInL, nextInR};
    std::memset(outL, 0, sizeof(outL));
    std::memset(outR, 0, sizeof(outR));
    instance->process(nextInputs, outputs, 2, 2, 4);

    if (std::memcmp(inL, outL, sizeof(inL)) != 0 || std::memcmp(inR, outR, sizeof(inR)) != 0) {
        std::cerr << "isolated plugin proxy did not return helper-processed output on the next callback\n";
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    MidiBuffer midi;
    midi.addNoteOn(1, 60, 100, 1);
    float midiInL[4] = {0.11f, 0.22f, 0.33f, 0.44f};
    float midiInR[4] = {-0.11f, -0.22f, -0.33f, -0.44f};
    const float* midiInputs[2] = {midiInL, midiInR};
    std::memset(outL, 0, sizeof(outL));
    std::memset(outR, 0, sizeof(outR));
    instance->process(midiInputs, outputs, 2, 2, 4, &midi, nullptr);
    if (std::memcmp(nextInL, outL, sizeof(nextInL)) != 0 || std::memcmp(nextInR, outR, sizeof(nextInR)) != 0) {
        std::cerr << "isolated plugin proxy did not survive MIDI-bearing process command\n";
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::memset(outL, 0, sizeof(outL));
    std::memset(outR, 0, sizeof(outR));
    instance->process(nextInputs, outputs, 2, 2, 4);
    if (std::memcmp(midiInL, outL, sizeof(midiInL)) != 0 || std::memcmp(midiInR, outR, sizeof(midiInR)) != 0) {
        std::cerr << "isolated plugin proxy did not return helper-processed MIDI block output\n";
        return 1;
    }

    if (!instance->saveState().empty() || !instance->loadState({}) ||
        instance->loadState(std::vector<uint8_t>{1, 2, 3})) {
        std::cerr << "isolated plugin proxy state protocol handling failed for echo helper\n";
        return 1;
    }

    // --- Host->child parameter delivery (#238) --------------------------------
    // setParameter must reach the child and be applied to the hosted plugin. The
    // echo helper treats parameter 0 as an output gain, so a delivered change is
    // observable in the returned audio, not just a recorded log. Parameter changes
    // flow through a lock-free queue drained by the proxy's worker thread, so a
    // settle interval is needed (matching the process double-buffer above).
    const float paramIn[4] = {0.4f, 0.5f, 0.6f, 0.7f};

    instance->setParameter(0, 0.5f); // half gain
    if (!pumpUntilGain(instance, paramIn, 0.5f)) {
        std::cerr << "parameter change did not reach and gain the child plugin\n";
        return 1;
    }

    // Successive changes arrive in order — the last write to a parameter wins.
    instance->setParameter(0, 0.25f);
    instance->setParameter(0, 0.75f);
    if (!pumpUntilGain(instance, paramIn, 0.75f)) {
        std::cerr << "ordered parameter changes did not settle on the last value\n";
        return 1;
    }

    // An out-of-range parameter id is rejected by the child but must not crash the
    // proxy, and the instance keeps working (unity gain restores cleanly after).
    instance->setParameter(99999u, 0.5f);
    instance->setParameter(0, 1.0f);
    if (!pumpUntilGain(instance, paramIn, 1.0f) || instance->isCrashed()) {
        std::cerr << "proxy did not survive / recover after an out-of-range parameter id\n";
        return 1;
    }

    // --- #244 CLAP note-dialect delivery, end to end through the proxy --------
    // Drives real MIDI through OutOfProcessPluginInstance -> forked child ->
    // ClapModule::process, and reads back exactly what the fake CLAP plugin
    // received via process.in_events for each advertised dialect.
    {
        bool ran = false;

        // Native CLAP-only port: notes arrive as native NOTE_ON/OFF; the CC (a raw
        // MIDI dialect the port did not advertise) is deliberately dropped.
        auto clap = deliverNotesToFakeClap(factory, "__aestra_test_clap_note__", ran);
        if (!ran) {
            std::cerr << "fake CLAP (native) endpoint did not run\n";
            return 1;
        }
        if (clap.size() != 2) {
            std::cerr << "native CLAP port: expected 2 note events (CC dropped), got " << clap.size()
                      << "\n";
            return 1;
        }
        if (clap[0].type != 0 || clap[0].channel != 3 || clap[0].key != 60 || clap[0].time != 0 ||
            std::fabs(clap[0].velocity - 100.0 / 127.0) > 1e-4) {
            std::cerr << "native CLAP note-on payload mismatch\n";
            return 1;
        }
        if (clap[1].type != 1 || clap[1].channel != 3 || clap[1].key != 60 || clap[1].time != 4) {
            std::cerr << "native CLAP note-off payload mismatch\n";
            return 1;
        }

        // MIDI-dialect port: everything arrives as raw CLAP_EVENT_MIDI, in order.
        auto rawmidi = deliverNotesToFakeClap(factory, "__aestra_test_clap_midi__", ran);
        if (rawmidi.size() != 3 || rawmidi[0].type != 10 || rawmidi[1].type != 10 ||
            rawmidi[2].type != 10) {
            std::cerr << "MIDI-dialect port: expected 3 raw MIDI events\n";
            return 1;
        }
        if (rawmidi[0].midi[0] != (0x90 | 3) || rawmidi[0].midi[1] != 60 || rawmidi[0].midi[2] != 100 ||
            rawmidi[1].midi[0] != (0xB0 | 3) || rawmidi[2].midi[0] != (0x80 | 3)) {
            std::cerr << "MIDI-dialect payload/order mismatch\n";
            return 1;
        }

        // Dual-dialect port preferring CLAP: notes go native, the CC rides raw MIDI
        // (the port also advertises MIDI), and chronological order is preserved.
        auto dual = deliverNotesToFakeClap(factory, "__aestra_test_clap_dual_pref_clap__", ran);
        if (dual.size() != 3 || dual[0].type != 0 || dual[1].type != 10 || dual[2].type != 1) {
            std::cerr << "dual-dialect port: expected native note-on, raw CC, native note-off in order\n";
            return 1;
        }
        if (dual[0].time != 0 || dual[1].time != 2 || dual[2].time != 4) {
            std::cerr << "dual-dialect mixed-event ordering/timestamps not preserved\n";
            return 1;
        }

        // Legacy compatibility fallback: no clap.note-ports extension -> raw MIDI.
        auto legacy = deliverNotesToFakeClap(factory, "__aestra_test_clap_legacy__", ran);
        if (legacy.size() != 3 || legacy[0].type != 10 || legacy[1].type != 10 || legacy[2].type != 10) {
            std::cerr << "legacy (no note-ports) fallback: expected 3 raw MIDI events\n";
            return 1;
        }
    }

    auto missingVst3 = create(factory, makeInfo("com.aestra.missing-vst3"));
    if (missingVst3) {
        std::cerr << "missing real VST3 plugin should not fall back to echo processing\n";
        return 1;
    }

    auto crashed = create(factory, makeInfo("__aestra_test_crash__"));
    if (crashed) {
        std::cerr << "crashing plugin should not produce a usable instance\n";
        return 1;
    }

    if (argc >= 3) {
        auto realClap = create(factory, makeRealClapProbeInfo(argv[2]));
        if (!realClap) {
            std::cerr << "failed to load real CLAP plugin out of process: " << argv[2] << "\n";
            return 1;
        }
        if (!realClap->initialize(48000.0, 64)) {
            std::cerr << "failed to initialize real CLAP plugin proxy\n";
            return 1;
        }
        realClap->activate();
        if (!realClap->isActive() || realClap->isCrashed()) {
            std::cerr << "real CLAP plugin proxy not active\n";
            return 1;
        }
        std::memset(outL, 0, sizeof(outL));
        std::memset(outR, 0, sizeof(outR));
        realClap->process(inputs, outputs, 2, 2, 4);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        realClap->process(nextInputs, outputs, 2, 2, 4);
        for (float sample : outL) {
            if (!std::isfinite(sample)) {
                std::cerr << "real CLAP plugin returned non-finite left-channel audio\n";
                return 1;
            }
        }
        for (float sample : outR) {
            if (!std::isfinite(sample)) {
                std::cerr << "real CLAP plugin returned non-finite right-channel audio\n";
                return 1;
            }
        }
        if (realClap->isCrashed()) {
            std::cerr << "real CLAP plugin proxy crashed during process\n";
            return 1;
        }
        realClap->shutdown();
    }

    std::cout << "[PASS] Out-of-process plugin host contains helper crashes and keeps parent alive.\n";
    return 0;
}
