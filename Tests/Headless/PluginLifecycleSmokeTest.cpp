// © 2026 Aestra Studios — All Rights Reserved.
// PluginLifecycleSmokeTest — headless coverage for the plugin-lifecycle smoke:
// a real Sat -> Filter -> OTT -> LFO EffectChain taken through
//   (a) parameter save/reload (project persistence),
//   (b) reprepare at a different sample rate + block size (device change —
//       EffectChain::prepare() re-inits every plugin), and
//   (c) audio processing before and after reprepare.
// Asserts no parameter loss, no non-finite audio, and no gross gain jump.
//
// This covers the automatable parts of the owner smoke test. GUI editor
// open/close and audible-quality judgement remain manual.

#include "Plugin/BuiltInPlugins.h"
#include "Plugin/EffectChain.h"
#include "Plugin/PluginManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;
void check(bool cond, const std::string& what) {
    if (!cond) {
        std::cout << "[FAIL] " << what << "\n";
        ++g_failures;
    }
}

const char* const kPluginIds[] = {
    "com.Aestrastudios.sat",
    "com.Aestrastudios.filter",
    "com.Aestrastudios.ott",
    "com.Aestrastudios.lfo",
};

float distinctive(float def) {
    float v = (def <= 0.5f) ? def + 0.3f : def - 0.3f;
    v = std::clamp(v, 0.05f, 0.95f);
    if (std::abs(v - def) < 0.05f)
        v = (def < 0.5f) ? 0.9f : 0.1f;
    return v;
}

// Peak magnitude of a stereo block.
float peak(const std::vector<float>& l, const std::vector<float>& r) {
    float p = 0.0f;
    for (float s : l)
        p = std::max(p, std::abs(s));
    for (float s : r)
        p = std::max(p, std::abs(s));
    return p;
}

bool allFinite(const std::vector<float>& l, const std::vector<float>& r) {
    for (float s : l)
        if (!std::isfinite(s))
            return false;
    for (float s : r)
        if (!std::isfinite(s))
            return false;
    return true;
}

// Process one block through the chain (in place, stereo). Returns peak.
float processBlock(EffectChain& chain, uint32_t frames, std::vector<float>& l, std::vector<float>& r) {
    l.assign(frames, 0.0f);
    r.assign(frames, 0.0f);
    for (uint32_t i = 0; i < frames; ++i)
        l[i] = r[i] = 0.4f * std::sin(2.0f * 3.14159265f * 220.0f * static_cast<float>(i) / 48000.0f);
    float* buf[] = {l.data(), r.data()};
    chain.process(buf, 2, frames);
    return peak(l, r);
}

} // namespace

int main() {
    std::cout << "=== Plugin lifecycle smoke (headless) ===\n";

    auto& pm = PluginManager::getInstance();
    if (!pm.initialize()) {
        std::cout << "[FAIL] PluginManager init\n";
        return 1;
    }
    BuiltInPlugins::registerCoreBuiltIns();

    // ── Build the chain: Sat -> Filter -> OTT -> LFO ──
    EffectChain chain;
    chain.prepare(48000.0, 512);
    for (size_t slot = 0; slot < 4; ++slot) {
        auto inst = pm.createInstanceById(kPluginIds[slot]);
        check(inst != nullptr, std::string("create ") + kPluginIds[slot]);
        if (inst)
            check(chain.insertPlugin(slot, inst), std::string("insert ") + kPluginIds[slot]);
    }

    // ── Set every parameter on every plugin to a distinctive non-default value ──
    // Record (slot, id) -> value for later verification.
    std::vector<std::vector<float>> expected(4);
    for (size_t slot = 0; slot < 4; ++slot) {
        auto p = chain.getPlugin(slot);
        if (!p)
            continue;
        const uint32_t n = p->getParameterCount();
        expected[slot].resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            p->setParameter(i, distinctive(p->getParameter(i)));
            expected[slot][i] = p->getParameter(i);
        }
    }

    auto verify = [&](EffectChain& c, const char* stage) {
        for (size_t slot = 0; slot < 4; ++slot) {
            auto p = c.getPlugin(slot);
            check(p != nullptr, std::string(stage) + ": slot " + std::to_string(slot) + " present");
            if (!p)
                continue;
            for (uint32_t i = 0; i < expected[slot].size(); ++i) {
                if (std::abs(p->getParameter(i) - expected[slot][i]) > 1e-6f) {
                    std::cout << "  [" << stage << "] " << kPluginIds[slot] << " param " << i << " changed ("
                              << expected[slot][i] << " -> " << p->getParameter(i) << ")\n";
                    ++g_failures;
                }
            }
        }
    };

    // ── (c) Audio through the freshly-configured chain: finite ──
    std::vector<float> l, r;
    const float peakBefore = processBlock(chain, 512, l, r);
    check(allFinite(l, r), "audio finite before reprepare");

    // ── (a) Save / reload (project persistence) ──
    const auto state = chain.saveState();
    check(!state.empty(), "chain saveState non-empty");
    EffectChain reloaded;
    reloaded.prepare(48000.0, 512);
    check(reloaded.loadState(state, pm), "chain loadState");
    verify(reloaded, "save-reload");

    // ── (b) Reprepare at a different sample rate + block size (device change) ──
    // EffectChain::prepare() re-inits every plugin; parameters must survive.
    chain.prepare(96000.0, 256);
    verify(chain, "reprepare-96k-256");
    chain.prepare(44100.0, 128);
    verify(chain, "reprepare-44k-128");

    // ── (c) Audio after reprepare: finite, no gross gain jump vs before ──
    const float peakAfter = processBlock(chain, 128, l, r);
    check(allFinite(l, r), "audio finite after reprepare");
    // Same input; output peak should be in the same ballpark (no >12 dB jump).
    if (peakBefore > 1e-4f && peakAfter > 1e-4f) {
        const float ratioDb = 20.0f * std::log10(peakAfter / peakBefore);
        check(std::abs(ratioDb) < 12.0f, "no gross gain jump across reprepare");
    }

    if (g_failures > 0) {
        std::cout << g_failures << " lifecycle check(s) failed\n";
        return 1;
    }
    std::cout << "All plugin-lifecycle smoke checks passed\n";
    return 0;
}
