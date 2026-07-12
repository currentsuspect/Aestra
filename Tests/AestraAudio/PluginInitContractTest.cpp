// © 2026 Aestra Studios — All Rights Reserved.
// PluginInitContractTest — enforces the initialization contract across every
// internal effect plugin:
//   * initialize() seeds parameter defaults only on the FIRST init of a fresh
//     instance;
//   * EffectChain::prepare() re-calls initialize() on the live instance during
//     sample-rate / device changes and must PRESERVE the user's parameters and
//     any loaded project state;
//   * smoothed/current DSP values snap safely to the preserved targets;
//   * a save/reinit/load/reinit cycle preserves state.
// This is the regression for the parameter-wipe bug found on #474.

#include "Plugin/AestraComp.h"
#include "Plugin/AestraDelay.h"
#include "Plugin/AestraDrift.h"
#include "Plugin/AestraFilter.h"
#include "Plugin/AestraLFO.h"
#include "Plugin/AestraLimit.h"
#include "Plugin/AestraOTT.h"
#include "Plugin/AestraSat.h"
#include "Plugin/AestraVerb.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

// A distinctive, in-range value that differs clearly from the default, so a
// reset-to-default would be caught. Plugins store the raw normalized value, so
// getParameter() returns exactly what we set (bit-identical comparison holds).
float distinctiveValue(float def) {
    float v = (def <= 0.5f) ? def + 0.3f : def - 0.3f;
    v = std::clamp(v, 0.05f, 0.95f);
    if (std::abs(v - def) < 0.05f)
        v = (def < 0.5f) ? 0.9f : 0.1f;
    return v;
}

template <typename PluginT> bool reinitContract(const char* name) {
    PluginT p;

    // 1. Construct and initialize.
    p.initialize(48000.0, 512);
    const uint32_t n = p.getParameterCount();

    // 2-3. Set every parameter (incl. bypass and stepped/enumerated) to a
    // distinctive non-default value and remember it.
    std::vector<float> saved(n);
    for (uint32_t i = 0; i < n; ++i) {
        p.setParameter(i, distinctiveValue(p.getParameter(i)));
        saved[i] = p.getParameter(i);
    }

    auto assertPreserved = [&](const char* stage) {
        for (uint32_t i = 0; i < n; ++i) {
            if (std::abs(p.getParameter(i) - saved[i]) > 1e-6f) {
                std::cout << "  [" << name << "] param " << i << " changed at " << stage << " (" << saved[i] << " -> "
                          << p.getParameter(i) << ")\n";
                return false;
            }
        }
        return true;
    };

    // 4. Reinitialize at the same sample rate.
    p.initialize(48000.0, 512);
    if (!assertPreserved("reinit-same-rate"))
        return false;

    // 5. Reinitialize at a different sample rate and block size.
    p.initialize(96000.0, 256);
    if (!assertPreserved("reinit-different-rate"))
        return false;

    // 6. (covered by assertPreserved above — bit/epsilon identical.)

    // 7. Process audio and require finite output.
    p.activate();
    constexpr uint32_t kFrames = 256;
    std::vector<float> inL(kFrames), inR(kFrames), outL(kFrames, 0.0f), outR(kFrames, 0.0f);
    for (uint32_t i = 0; i < kFrames; ++i)
        inL[i] = inR[i] = 0.3f * std::sin(0.05f * static_cast<float>(i));
    const float* ins[] = {inL.data(), inR.data()};
    float* outs[] = {outL.data(), outR.data()};
    p.process(ins, outs, 2, 2, kFrames);
    for (uint32_t i = 0; i < kFrames; ++i) {
        if (!std::isfinite(outL[i]) || !std::isfinite(outR[i])) {
            std::cout << "  [" << name << "] non-finite output at frame " << i << "\n";
            return false;
        }
    }

    // 8. Save state, clobber params, reinit, reload state, reinit again, verify
    //    persistence across the whole cycle.
    const auto state = p.saveState();
    for (uint32_t i = 0; i < n; ++i)
        p.setParameter(i, 0.5f); // clobber
    p.initialize(48000.0, 512);  // must preserve the clobbered values, not reset
    if (!p.loadState(state)) {
        std::cout << "  [" << name << "] loadState rejected a self-saved blob\n";
        return false;
    }
    p.initialize(96000.0, 512); // reinit after load must preserve the loaded state
    if (!assertPreserved("reload-then-reinit"))
        return false;

    return true;
}

struct Case {
    const char* name;
    bool (*fn)();
};

} // namespace

int main() {
    using namespace Aestra::Audio::Plugins;
    const Case cases[] = {
        {"AestraSat", [] { return reinitContract<AestraSat>("AestraSat"); }},
        {"AestraFilter", [] { return reinitContract<AestraFilter>("AestraFilter"); }},
        {"AestraComp", [] { return reinitContract<AestraComp>("AestraComp"); }},
        {"AestraVerb", [] { return reinitContract<AestraVerb>("AestraVerb"); }},
        {"AestraDelay", [] { return reinitContract<AestraDelay>("AestraDelay"); }},
        {"AestraDrift", [] { return reinitContract<AestraDrift>("AestraDrift"); }},
        {"AestraLimit", [] { return reinitContract<AestraLimit>("AestraLimit"); }},
        {"AestraLFO", [] { return reinitContract<AestraLFO>("AestraLFO"); }},
        {"AestraOTT", [] { return reinitContract<AestraOTT>("AestraOTT"); }},
    };

    int failures = 0;
    for (const auto& c : cases) {
        const bool ok = c.fn();
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << c.name << " init contract\n";
        if (!ok)
            ++failures;
    }

    if (failures > 0) {
        std::cout << failures << " plugin(s) failed the init contract\n";
        return 1;
    }
    std::cout << "All plugin init-contract checks passed\n";
    return 0;
}
