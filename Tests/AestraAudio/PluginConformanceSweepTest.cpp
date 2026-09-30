// © 2026 Aestra Studios — All Rights Reserved.
// PluginConformanceSweepTest — one contract, every built-in plugin, forever.
//
// Why this exists: every plugin used to carry its own ~450-520 line contract
// test file, and nine of every sixteen checks in one of those files are
// properties of *any* effect, not of that effect: silence in gives silence
// out, hostile input stays finite, NaN parameters are rejected, state
// round-trips, garbage state is refused, reported latency is stable, bypass is
// sample-aligned, and the audio callback does not allocate. Registering a
// plugin in InternalPluginRegistry now buys all of that automatically, so a new
// effect arrives with its generic contract already covered and its own test
// file only carries what is actually specific to it.
//
// The contracts are written against IPluginInstance + PluginInfo alone. No
// plugin-specific knowledge lives here — that is what makes this a sweep rather
// than a thirteenth hand-maintained list (see PluginInitContractTest, which
// hand-lists nine plugins and therefore silently skips EQ, Transient and the
// sampler the moment one of them regresses).

#include "Plugin/BuiltInPlugins.h"
#include "Plugin/InternalPluginRegistry.h"
#include "RealtimeThreadGuard.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <vector>

// =============================================================================
// Allocation trap (test binary only)
// =============================================================================
//
// Mirrors RTAllocationTrapTest: overriding global operator new/delete means any
// std container, string or shared_ptr control block touched on the audio path
// is counted. Gated on isRealtimeAudioThread() so only allocations made inside
// an explicit ScopedRealtimeAudioThread count, which is how this test marks a
// process() call as being the audio callback.

namespace {
std::atomic<bool> g_trapArmed{false};
std::atomic<uint64_t> g_rtAllocCount{0};
std::atomic<uint64_t> g_rtFreeCount{0};

void noteAlloc(std::size_t) noexcept {
    if (g_trapArmed.load(std::memory_order_relaxed) && Aestra::Audio::isRealtimeAudioThread())
        g_rtAllocCount.fetch_add(1, std::memory_order_relaxed);
}

void noteFree() noexcept {
    if (g_trapArmed.load(std::memory_order_relaxed) && Aestra::Audio::isRealtimeAudioThread())
        g_rtFreeCount.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

void* operator new(std::size_t size) {
    noteAlloc(size);
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) {
    noteAlloc(size);
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    noteAlloc(size);
    return std::malloc(size ? size : 1);
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    noteAlloc(size);
    return std::malloc(size ? size : 1);
}
void operator delete(void* p) noexcept {
    noteFree();
    std::free(p);
}
void operator delete[](void* p) noexcept {
    noteFree();
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    noteFree();
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    noteFree();
    std::free(p);
}
void operator delete(void* p, const std::nothrow_t&) noexcept {
    noteFree();
    std::free(p);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept {
    noteFree();
    std::free(p);
}

// =============================================================================
// Harness
// =============================================================================

namespace {

using Aestra::Audio::IPluginInstance;
using Aestra::Audio::MidiBuffer;
using Aestra::Audio::PluginInfo;
using Aestra::Audio::PluginInstancePtr;

// A "should be silence" bar. A DC blocker stepping on a zero input, or a
// denormal guard flushing, lands around -120 dBFS; anything that means real
// signal is orders of magnitude above this.
constexpr float kSilencePeak = 1e-6f;

constexpr double kRates[] = {44100.0, 48000.0, 96000.0};
constexpr uint32_t kBlocks[] = {32, 256, 4096};

/// Channel-major scratch buffers sized for the plugin's declared layout.
struct Channels {
    std::vector<std::vector<float>> in;
    std::vector<std::vector<float>> out;
    std::vector<const float*> inPtrs;
    std::vector<float*> outPtrs;

    Channels(uint32_t nIn, uint32_t nOut, uint32_t frames) {
        in.assign(nIn, std::vector<float>(frames, 0.0f));
        out.assign(nOut, std::vector<float>(frames, 0.0f));
        remap();
    }

    void remap() {
        inPtrs.clear();
        for (auto& c : in) inPtrs.push_back(c.data());
        outPtrs.clear();
        for (auto& c : out) outPtrs.push_back(c.data());
    }

    void setFrame(uint32_t i, float v) {
        for (auto& c : in) c[i] = v;
    }

    float peak() const {
        float m = 0.0f;
        for (const auto& c : out)
            for (float s : c) m = std::max(m, std::abs(s));
        return m;
    }

    bool allFinite() const {
        for (const auto& c : out)
            for (float s : c)
                if (!std::isfinite(s)) return false;
        return true;
    }
};

/// Deterministic broadband noise, distinct per channel. Seeded from the frame
/// index so a failure is reproducible from the printed seed alone.
std::vector<float> makeNoise(uint32_t frames, uint32_t seed, float amp) {
    std::vector<float> buf(frames);
    uint32_t lcg = seed * 2654435761u + 1013904223u;
    for (uint32_t i = 0; i < frames; ++i) {
        lcg = lcg * 1664525u + 1013904223u;
        buf[i] = (static_cast<float>(lcg >> 8) / 8388608.0f - 1.0f) * amp;
    }
    return buf;
}

// Returns by value on purpose. An earlier version handed back a pointer into a
// shared static buffer, so a message that interpolated two of these printed
// the same number twice and hid the actual mismatch.
std::string fmt(float v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(v));
    return buf;
}

// A value in [0,1] that is clearly not the default, and never on a step
// boundary, so a read-back comparison is meaningful.
float distinctiveValue(float def) {
    float v = (def <= 0.5f) ? def + 0.3f : def - 0.3f;
    v = std::clamp(v, 0.05f, 0.95f);
    if (std::abs(v - def) < 0.05f)
        v = (def < 0.5f) ? 0.9f : 0.1f;
    return v;
}

struct Report {
    std::string id;
    std::vector<std::pair<std::string, std::string>> failures;
    std::vector<std::string> skips;
    std::vector<std::pair<std::string, std::string>> contracts;

    void fail(const std::string& contract, const std::string& detail) {
        failures.emplace_back(contract, detail);
    }
    void skip(const std::string& contract, const std::string& why) { skips.push_back(contract + " — " + why); }
    void pass(const std::string& contract, const std::string& note = std::string()) {
        contracts.emplace_back(contract, note);
    }
};

/// Feeds one block through the plugin, marking it as the audio thread so the
/// allocation trap sees it.
void processBlock(IPluginInstance& plugin, Channels& ch, uint32_t frames) {
    Aestra::Audio::ScopedRealtimeAudioThread rtScope;
    plugin.process(ch.inPtrs.data(), ch.outPtrs.data(), static_cast<uint32_t>(ch.in.size()),
                   static_cast<uint32_t>(ch.out.size()), frames, nullptr, nullptr);
}

// =============================================================================
// Contracts
// =============================================================================

/// initialize() succeeds and activate() takes, at every supported rate/block pair.
void contractPrepareMatrix(IPluginInstance& plugin, const PluginInfo& info, Report& rep) {
    (void)plugin; // builds its own fresh instance per rate/block pair
    for (double rate : kRates) {
        for (uint32_t block : kBlocks) {
            auto fresh = Aestra::Audio::InternalPluginRegistry::instance().createInstance(info.id);
            if (!fresh) {
                rep.fail("PrepareMatrix", "createInstance returned null");
                return;
            }
            if (!fresh->initialize(rate, block)) {
                rep.fail("PrepareMatrix", "initialize(" + std::to_string(static_cast<int>(rate)) + ", " +
                                              std::to_string(block) + ") returned false");
                continue;
            }
            fresh->activate();
            if (!fresh->isActive()) {
                rep.fail("PrepareMatrix", "isActive() false after activate() at " +
                                              std::to_string(static_cast<int>(rate)) + "/" + std::to_string(block));
                continue;
            }
            fresh->deactivate();
            if (fresh->isActive()) {
                rep.fail("PrepareMatrix", "isActive() still true after deactivate() at " +
                                              std::to_string(static_cast<int>(rate)) + "/" + std::to_string(block));
            }
        }
    }
    if (rep.failures.empty())
        rep.pass("PrepareMatrix", "3 rates x 3 block sizes");
}

/// Silence in gives silence out. Effects only — the sampler declares zero audio
/// inputs and generates from MIDI, so there is no input to be silent.
void contractSilenceInSilenceOut(IPluginInstance& plugin, const PluginInfo& info, Report& rep) {
    if (info.numAudioInputs == 0) {
        rep.skip("SilenceInSilenceOut", "no audio inputs (instrument)");
        return;
    }
    if (!plugin.initialize(48000.0, 256)) {
        rep.fail("SilenceInSilenceOut", "initialize failed");
        return;
    }
    plugin.activate();

    constexpr uint32_t kBlock = 256;
    Channels ch(info.numAudioInputs, info.numAudioOutputs, kBlock);
    // Settle first: a filter ringing down from a previous block, or a DC
    // blocker stepping, must not be mistaken for the steady-state answer.
    for (int i = 0; i < 8; ++i)
        processBlock(plugin, ch, kBlock);

    const float peak = ch.peak();
    if (!std::isfinite(peak))
        rep.fail("SilenceInSilenceOut", "non-finite output from silent input");
    else if (peak > kSilencePeak)
        rep.fail("SilenceInSilenceOut", "peak " + fmt(peak) + " from silent input (bar " + fmt(kSilencePeak) + ")");
    else
        rep.pass("SilenceInSilenceOut", "peak " + fmt(peak));
}

/// NaN, +/-inf, denormals and a clipping-level signal all come out finite.
void contractHostileInputStaysFinite(IPluginInstance& plugin, const PluginInfo& info, Report& rep) {
    if (info.numAudioInputs == 0) {
        rep.skip("HostileInputStaysFinite", "no audio inputs (instrument)");
        return;
    }
    if (!plugin.initialize(48000.0, 256)) {
        rep.fail("HostileInputStaysFinite", "initialize failed");
        return;
    }
    plugin.activate();

    constexpr uint32_t kBlock = 256;
    Channels ch(info.numAudioInputs, info.numAudioOutputs, kBlock);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const float denorm = std::numeric_limits<float>::denorm_min();
    const float cases[] = {nan, inf, -inf, denorm, -denorm, 10.0f, -10.0f};
    const char* names[] = {"NaN", "+inf", "-inf", "denormal", "-denormal", "+10.0", "-10.0"};

    for (float v : cases) {
        for (uint32_t i = 0; i < kBlock; ++i)
            ch.setFrame(i, v);
        processBlock(plugin, ch, kBlock);
        if (!ch.allFinite()) {
            rep.fail("HostileInputStaysFinite", std::string("non-finite output from ") + names[&v - &cases[0]]);
            return;
        }
    }
    rep.pass("HostileInputStaysFinite", "7 hostile inputs");
}

/// The parameter list is self-consistent and every entry accepts a write and
/// returns a finite in-range value that is stable across a re-read.
void contractParameterTable(IPluginInstance& plugin, const PluginInfo& info, Report& rep) {
    (void)info;
    const auto params = plugin.getParameters();
    const uint32_t declared = plugin.getParameterCount();

    if (params.size() != declared) {
        rep.fail("ParameterTable", "getParameterCount()=" + std::to_string(declared) + " but getParameters().size()=" +
                                       std::to_string(params.size()));
        return;
    }
    if (params.empty()) {
        rep.fail("ParameterTable", "no parameters declared");
        return;
    }

    for (const auto& p : params) {
        if (p.name.empty()) {
            rep.fail("ParameterTable", "parameter " + std::to_string(p.id) + " has an empty display name");
            return;
        }
        if (p.minValue > p.maxValue) {
            rep.fail("ParameterTable", "parameter " + std::to_string(p.id) + " has min > max");
            return;
        }
        if (p.defaultValue < p.minValue - 1e-6f || p.defaultValue > p.maxValue + 1e-6f) {
            rep.fail("ParameterTable", "parameter " + std::to_string(p.id) + " default " + fmt(p.defaultValue) +
                                           " outside [" + fmt(p.minValue) + ", " + fmt(p.maxValue) + "]");
            return;
        }

        const float v = distinctiveValue(p.defaultValue);
        plugin.setParameter(p.id, v);
        const float back = plugin.getParameter(p.id);
        if (!std::isfinite(back)) {
            rep.fail("ParameterTable", "parameter " + std::to_string(p.id) + " read back non-finite");
            return;
        }
        if (back < p.minValue - 1e-6f || back > p.maxValue + 1e-6f) {
            rep.fail("ParameterTable", "parameter " + std::to_string(p.id) + " read back " + fmt(back) +
                                           " outside [" + fmt(p.minValue) + ", " + fmt(p.maxValue) + "]");
            return;
        }
        // A stepped parameter legitimately quantises the write, so idempotence
        // is the honest round-trip assertion: same write, same answer.
        plugin.setParameter(p.id, v);
        if (std::abs(plugin.getParameter(p.id) - back) > 1e-6f) {
            rep.fail("ParameterTable", "parameter " + std::to_string(p.id) + " is not idempotent");
            return;
        }
    }
    rep.pass("ParameterTable", std::to_string(params.size()) + " parameters");
}

/// NaN and infinity on the parameter ingress must leave the previous value in
/// place rather than poisoning the DSP.
void contractParameterRejectsNonFinite(IPluginInstance& plugin, const PluginInfo& info, Report& rep) {
    (void)info;
    const auto params = plugin.getParameters();
    if (params.empty()) {
        rep.fail("ParameterRejectsNonFinite", "no parameters declared");
        return;
    }
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    for (const auto& p : params) {
        if (p.isReadOnly)
            continue;
        const float anchor = distinctiveValue(p.defaultValue);
        plugin.setParameter(p.id, anchor);
        const float before = plugin.getParameter(p.id);

        plugin.setParameter(p.id, nan);
        if (!std::isfinite(plugin.getParameter(p.id))) {
            rep.fail("ParameterRejectsNonFinite", "parameter " + std::to_string(p.id) + " accepted NaN");
            return;
        }
        if (std::abs(plugin.getParameter(p.id) - before) > 1e-6f) {
            rep.fail("ParameterRejectsNonFinite", "parameter " + std::to_string(p.id) +
                                                     " changed on NaN (" + fmt(before) + " -> " +
                                                     fmt(plugin.getParameter(p.id)) + ")");
            return;
        }
        plugin.setParameter(p.id, inf);
        if (!std::isfinite(plugin.getParameter(p.id))) {
            rep.fail("ParameterRejectsNonFinite", "parameter " + std::to_string(p.id) + " accepted +inf");
            return;
        }
        if (std::abs(plugin.getParameter(p.id) - before) > 1e-6f) {
            rep.fail("ParameterRejectsNonFinite", "parameter " + std::to_string(p.id) +
                                                     " changed on +inf (" + fmt(before) + " -> " +
                                                     fmt(plugin.getParameter(p.id)) + ")");
            return;
        }
    }
    rep.pass("ParameterRejectsNonFinite", std::to_string(params.size()) + " parameters");
}

/// save() -> load() on a second instance restores every parameter.
void contractStateRoundTrip(IPluginInstance& plugin, const PluginInfo& info, Report& rep) {
    if (!plugin.initialize(48000.0, 256)) {
        rep.fail("StateRoundTrip", "initialize failed");
        return;
    }
    plugin.activate();

    std::vector<float> written;
    for (const auto& p : plugin.getParameters()) {
        const float v = distinctiveValue(p.defaultValue);
        plugin.setParameter(p.id, v);
        written.push_back(plugin.getParameter(p.id));
    }

    const auto state = plugin.saveState();
    if (state.empty()) {
        rep.fail("StateRoundTrip", "saveState() returned an empty blob");
        return;
    }

    auto restored = Aestra::Audio::InternalPluginRegistry::instance().createInstance(info.id);
    if (!restored) {
        rep.fail("StateRoundTrip", "createInstance returned null");
        return;
    }
    if (!restored->initialize(48000.0, 256)) {
        rep.fail("StateRoundTrip", "initialize failed on the restore instance");
        return;
    }
    if (!restored->loadState(state)) {
        rep.fail("StateRoundTrip", "loadState() rejected a blob this plugin saved");
        return;
    }

    const auto params = plugin.getParameters();
    for (size_t i = 0; i < params.size(); ++i) {
        const float back = restored->getParameter(params[i].id);
        if (std::abs(back - written[i]) > 1e-6f) {
            rep.fail("StateRoundTrip", "parameter " + std::to_string(params[i].id) + " came back " + fmt(back) +
                                           " after saving " + fmt(written[i]));
            return;
        }
    }
    rep.pass("StateRoundTrip", std::to_string(state.size()) + " byte blob");
}

/// Garbage state is refused, and refusing it leaves the plugin untouched — a
/// half-applied blob is how a corrupt project file becomes a corrupt session.
void contractGarbageStateRejected(IPluginInstance& plugin, const PluginInfo& info, Report& rep) {
    (void)info;
    if (!plugin.initialize(48000.0, 256)) {
        rep.fail("GarbageStateRejected", "initialize failed");
        return;
    }

    std::vector<float> anchor;
    for (const auto& p : plugin.getParameters()) {
        plugin.setParameter(p.id, distinctiveValue(p.defaultValue));
        anchor.push_back(plugin.getParameter(p.id));
    }

    const std::vector<std::vector<uint8_t>> garbage = {
        {0x31, 0x54},                                            // truncated header
        std::vector<uint8_t>(64, 0x00),                          // wrong magic
        std::vector<uint8_t>(64, 0xFF),                          // wrong magic, high bytes
        std::vector<uint8_t>(4096, 0x41),                        // plausible size, wrong magic
    };

    for (const auto& blob : garbage) {
        if (plugin.loadState(blob)) {
            rep.fail("GarbageStateRejected", "accepted a " + std::to_string(blob.size()) + "-byte garbage blob");
            return;
        }
        const auto params = plugin.getParameters();
        for (size_t i = 0; i < params.size(); ++i) {
            const float now = plugin.getParameter(params[i].id);
            if (std::abs(now - anchor[i]) > 1e-6f) {
                rep.fail("GarbageStateRejected", "parameter " + std::to_string(params[i].id) +
                                                     " moved to " + fmt(now) + " while rejecting garbage");
                return;
            }
        }
    }
    rep.pass("GarbageStateRejected", std::to_string(garbage.size()) + " blobs");
}

/// Reported latency and tail must not move while audio flows, across block
/// sizes, or the host's delay compensation is silently wrong.
void contractLatencyStable(IPluginInstance& plugin, const PluginInfo& info, Report& rep) {
    if (!plugin.initialize(48000.0, 64)) {
        rep.fail("LatencyStable", "initialize failed");
        return;
    }
    plugin.activate();

    const uint32_t latency = plugin.getLatencySamples();
    const uint32_t tail = plugin.getTailSamples();

    for (uint32_t block : kBlocks) {
        Channels ch(info.numAudioInputs, std::max<uint32_t>(info.numAudioOutputs, 1), block);
        if (!ch.in.empty()) {
            const auto noise = makeNoise(block, block, 0.25f);
            for (uint32_t i = 0; i < block; ++i)
                ch.setFrame(i, noise[i]);
        }
        processBlock(plugin, ch, block);
        if (plugin.getLatencySamples() != latency) {
            rep.fail("LatencyStable", "getLatencySamples() moved " + std::to_string(latency) + " -> " +
                                          std::to_string(plugin.getLatencySamples()) + " at block " +
                                          std::to_string(block));
            return;
        }
        if (plugin.getTailSamples() != tail) {
            rep.fail("LatencyStable", "getTailSamples() moved " + std::to_string(tail) + " -> " +
                                          std::to_string(plugin.getTailSamples()) + " at block " +
                                          std::to_string(block));
            return;
        }
    }
    rep.pass("LatencyStable", "latency " + std::to_string(latency) + ", tail " + std::to_string(tail));
}

/// Bypass must be a pure delay of exactly getLatencySamples() — an undelayed
/// copy lands early against every other PDC-compensated plugin in the chain.
void contractBypassSampleAligned(IPluginInstance& plugin, const PluginInfo& info, Report& rep) {
    if (info.numAudioInputs == 0) {
        rep.skip("BypassSampleAligned", "no audio inputs (instrument)");
        return;
    }
    if (!plugin.initialize(48000.0, 256)) {
        rep.fail("BypassSampleAligned", "initialize failed");
        return;
    }

    uint32_t bypassId = UINT32_MAX;
    for (const auto& p : plugin.getParameters()) {
        if (p.isBypass) {
            bypassId = p.id;
            break;
        }
    }
    if (bypassId == UINT32_MAX) {
        rep.skip("BypassSampleAligned", "no parameter declares isBypass");
        return;
    }

    plugin.activate();
    // Push the other knobs to their extremes: a bypass path that still lets
    // state leak through shows up only when the wet path is doing work.
    for (const auto& p : plugin.getParameters()) {
        if (p.id != bypassId && !p.isReadOnly)
            plugin.setParameter(p.id, 1.0f);
    }
    plugin.setParameter(bypassId, 1.0f);

    constexpr uint32_t kBlock = 512;
    Channels ch(info.numAudioInputs, info.numAudioOutputs, kBlock);

    // Prime with SILENCE, not with the probe signal. A delay line pre-loaded
    // with real audio would legitimately emit that audio for the first
    // `latency` frames, which makes "those frames are zero" unassertable.
    // From silence the expected output is exactly the input delayed by the
    // reported latency, and nothing else.
    processBlock(plugin, ch, kBlock);

    // Read the latency AFTER a block has run. A plugin may apply deferred
    // configuration on the first process() call — the compressor's oversampling
    // factor and its dry delay are set there, not in setParameter() — so a
    // read taken earlier reports the pre-configuration value and asserts
    // against a delay the plugin is not actually applying.
    const uint32_t latency = plugin.getLatencySamples();

    const auto signal = makeNoise(kBlock, 0xB0, 0.5f);
    for (uint32_t i = 0; i < kBlock; ++i)
        ch.setFrame(i, signal[i]);
    processBlock(plugin, ch, kBlock);

    for (uint32_t c = 0; c < ch.out.size(); ++c) {
        for (uint32_t i = 0; i < kBlock; ++i) {
            const float expect = (i >= latency) ? signal[i - latency] : 0.0f;
            const float got = ch.out[c][i];
            if (!std::isfinite(got)) {
                rep.fail("BypassSampleAligned", "non-finite bypassed output on channel " + std::to_string(c));
                return;
            }
            if (std::abs(got - expect) > 1e-5f) {
                rep.fail("BypassSampleAligned", "channel " + std::to_string(c) + " frame " + std::to_string(i) +
                                                    " expected " + fmt(expect) + " (delay " +
                                                    std::to_string(latency) + "), got " + fmt(got));
                return;
            }
        }
    }
    rep.pass("BypassSampleAligned", "delay " + std::to_string(latency));
}

/// The audio callback allocates nothing in steady state (AGENTS §10). The first
/// block is a warmup and is reported separately, the same split
/// RTAllocationTrapTest uses — a latent start-of-stream xrun is worth knowing
/// about but is not what this contract gates.
void contractNoSteadyStateAllocations(IPluginInstance& plugin, const PluginInfo& info, Report& rep) {
    if (info.numAudioInputs == 0) {
        rep.skip("NoSteadyStateAllocations", "no audio inputs (instrument)");
        return;
    }
    constexpr uint32_t kBlock = 256;
    if (!plugin.initialize(48000.0, kBlock)) {
        rep.fail("NoSteadyStateAllocations", "initialize failed");
        return;
    }
    plugin.activate();

    Channels ch(info.numAudioInputs, info.numAudioOutputs, kBlock);
    const auto noise = makeNoise(kBlock, 7u, 0.3f);
    for (uint32_t i = 0; i < kBlock; ++i)
        ch.setFrame(i, noise[i]);

    // Warmup, unarmed: anything allocated on the first block is start-up work.
    uint64_t warmupAllocs = 0;
    {
        g_trapArmed.store(true, std::memory_order_release);
        const uint64_t a0 = g_rtAllocCount.load(std::memory_order_relaxed);
        processBlock(plugin, ch, kBlock);
        warmupAllocs = g_rtAllocCount.load(std::memory_order_relaxed) - a0;
    }
    g_trapArmed.store(false, std::memory_order_release);

    const uint64_t a0 = g_rtAllocCount.load(std::memory_order_relaxed);
    const uint64_t f0 = g_rtFreeCount.load(std::memory_order_relaxed);
    g_trapArmed.store(true, std::memory_order_release);
    for (int i = 0; i < 32; ++i) {
        for (uint32_t f = 0; f < kBlock; ++f)
            ch.setFrame(f, noise[(f + static_cast<uint32_t>(i)) % kBlock]);
        processBlock(plugin, ch, kBlock);
    }
    g_trapArmed.store(false, std::memory_order_release);

    const uint64_t steadyAllocs = g_rtAllocCount.load(std::memory_order_relaxed) - a0;
    const uint64_t steadyFrees = g_rtFreeCount.load(std::memory_order_relaxed) - f0;
    if (steadyAllocs || steadyFrees) {
        rep.fail("NoSteadyStateAllocations", std::to_string(steadyAllocs) + " allocation(s) and " +
                                                std::to_string(steadyFrees) + " free(s) across 32 steady-state blocks");
        return;
    }
    rep.pass("NoSteadyStateAllocations", warmupAllocs ? (std::to_string(warmupAllocs) + " warmup alloc(s), 0 steady")
                                                      : "0 allocations");
}

/// Calling the interface's reset twice must land in the same observable state.
/// IPluginInstance exposes no plugin-level reset(), so resetWatchdog() is the
/// reset a registry-driven sweep can actually reach; the assertion is that its
/// clearing effect is idempotent, which is the same property a plugin reset needs.
void contractResetIsIdempotent(IPluginInstance& plugin, const PluginInfo& info, Report& rep) {
    (void)info;
    if (!plugin.initialize(48000.0, 256)) {
        rep.fail("ResetIsIdempotent", "initialize failed");
        return;
    }
    plugin.activate();

    plugin.resetWatchdog();
    const auto first = plugin.getWatchdogStats();
    plugin.resetWatchdog();
    const auto second = plugin.getWatchdogStats();

    if (second.violationCount != 0) {
        rep.fail("ResetIsIdempotent", "violationCount " + std::to_string(second.violationCount) +
                                          " after a second reset");
        return;
    }
    if (first.violationCount != second.violationCount || first.isBypassed != second.isBypassed) {
        rep.fail("ResetIsIdempotent", "stats differ between the first and second reset");
        return;
    }
    if (plugin.isBypassedByWatchdog()) {
        rep.fail("ResetIsIdempotent", "still bypassed by watchdog after reset");
        return;
    }
    rep.pass("ResetIsIdempotent");
}

// =============================================================================
// Driver
// =============================================================================

void sweepPlugin(const PluginInfo& info, Report& rep) {
    auto plugin = Aestra::Audio::InternalPluginRegistry::instance().createInstance(info.id);
    if (!plugin) {
        rep.fail("Instantiate", "createInstance returned null");
        return;
    }
    rep.pass("Instantiate", info.name);

    // Each contract gets a fresh instance so one contract's leftover state
    // cannot mask or fake another's.
    auto fresh = [&]() { return Aestra::Audio::InternalPluginRegistry::instance().createInstance(info.id); };

    contractPrepareMatrix(*plugin, info, rep);

    if (auto p = fresh()) contractSilenceInSilenceOut(*p, info, rep);
    if (auto p = fresh()) contractHostileInputStaysFinite(*p, info, rep);
    if (auto p = fresh()) contractParameterTable(*p, info, rep);
    if (auto p = fresh()) contractParameterRejectsNonFinite(*p, info, rep);
    if (auto p = fresh()) contractStateRoundTrip(*p, info, rep);
    if (auto p = fresh()) contractGarbageStateRejected(*p, info, rep);
    if (auto p = fresh()) contractLatencyStable(*p, info, rep);
    if (auto p = fresh()) contractBypassSampleAligned(*p, info, rep);
    if (auto p = fresh()) contractNoSteadyStateAllocations(*p, info, rep);
    if (auto p = fresh()) contractResetIsIdempotent(*p, info, rep);
}

} // namespace

int main() {
    std::cout << "=== Aestra built-in plugin conformance sweep ===\n\n";

    // Trap self-check: prove the override fires, so a zero allocation count
    // below means "clean" and not "the trap never ran". A runtime-derived size
    // is used because a paired new/delete the compiler can see is legal to
    // elide, which would fake a pass.
    {
        g_trapArmed.store(true, std::memory_order_release);
        const uint64_t a0 = g_rtAllocCount.load(std::memory_order_relaxed);
        const uint64_t f0 = g_rtFreeCount.load(std::memory_order_relaxed);
        {
            Aestra::Audio::ScopedRealtimeAudioThread rtScope;
            volatile std::size_t n = 64;
            void* p = ::operator new(n);
            ::operator delete(p);
        }
        g_trapArmed.store(false, std::memory_order_release);
        const bool trapWorks = (g_rtAllocCount.load(std::memory_order_relaxed) == a0 + 1) &&
                               (g_rtFreeCount.load(std::memory_order_relaxed) == f0 + 1);
        std::cout << (trapWorks ? "[PASS] " : "[FAIL] ")
                  << "trap self-check (the override fires, so a zero count below is meaningful)\n";
        if (!trapWorks)
            return 1;
        g_rtAllocCount.store(0, std::memory_order_relaxed);
        g_rtFreeCount.store(0, std::memory_order_relaxed);
    }

    const auto plugins = Aestra::Audio::BuiltInPlugins::all();
    if (plugins.empty()) {
        std::cout << "[FAIL] registry is empty — the sweep would silently pass nothing\n";
        return 1;
    }
    std::cout << plugins.size() << " built-in plugin(s) in the registry\n\n";

    std::vector<Report> reports;
    reports.reserve(plugins.size());
    for (const auto& info : plugins) {
        Report rep;
        rep.id = info.id;
        sweepPlugin(info, rep);

        std::cout << "--- " << info.id << " (" << info.name << ")\n";
        for (const auto& [contract, note] : rep.contracts)
            std::cout << "  [PASS] " << contract << (note.empty() ? "" : "  — " + note) << "\n";
        for (const auto& skipNote : rep.skips)
            std::cout << "  [SKIP] " << skipNote << "\n";
        for (const auto& [contract, detail] : rep.failures)
            std::cout << "  [FAIL] " << contract << "  — " << detail << "\n";
        std::cout << "\n";
        reports.push_back(std::move(rep));
    }

    size_t failures = 0;
    size_t passes = 0;
    size_t skips = 0;
    std::vector<std::string> failedPlugins;
    for (const auto& rep : reports) {
        failures += rep.failures.size();
        passes += rep.contracts.size();
        skips += rep.skips.size();
        if (!rep.failures.empty())
            failedPlugins.push_back(rep.id);
    }

    std::cout << "=== summary ===\n";
    std::cout << passes << " contract check(s) passed, " << skips << " skipped, " << failures << " failed across "
              << reports.size() << " plugin(s)\n";

    if (!failedPlugins.empty()) {
        std::cout << "\nplugins failing the sweep:\n";
        for (const auto& id : failedPlugins)
            std::cout << "  - " << id << "\n";
        return 1;
    }
    std::cout << "All built-in plugins pass the conformance sweep\n";
    return 0;
}
