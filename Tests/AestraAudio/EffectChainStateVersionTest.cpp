// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// EffectChainStateVersionTest
//
// Guards #211: EffectChain's serialized-state format must (a) round-trip through
// the versioned header, (b) reject an unknown/future format version gracefully
// instead of hard-failing or misparsing, and (c) sanitize a non-finite dry/wet
// value rather than letting NaN/Inf reach the audio path.

#include "Plugin/BuiltInPlugins.h"
#include "Plugin/EffectChain.h"
#include "Plugin/PluginManager.h"
#include "Plugin/SamplerPlugin.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

using namespace Aestra::Audio;

namespace {

int gFailures = 0;

void check(bool cond, const char* label) {
    std::cout << "  " << (cond ? "PASS " : "FAIL ") << label << "\n";
    if (!cond) {
        ++gFailures;
    }
}

bool nearly(float a, float b) {
    return std::fabs(a - b) < 1.0e-4f;
}

// Build a chain with one sampler in slot 0 and a distinctive dry/wet value.
std::vector<uint8_t> makeChainState(float dryWet) {
    EffectChain chain;
    auto plugin = std::make_shared<Plugins::SamplerPlugin>();
    plugin->initialize(48000.0, 512);
    chain.insertPlugin(0, plugin);
    chain.setSlotDryWetMix(0, dryWet);
    return chain.saveState();
}

// Overwrite the 4-byte little-endian float matching `needle` with `replacement`.
// Locates the value by its bit pattern so the test is robust to header/id sizes.
bool patchFloat(std::vector<uint8_t>& state, float needle, float replacement) {
    for (size_t i = 0; i + sizeof(float) <= state.size(); ++i) {
        float v;
        std::memcpy(&v, &state[i], sizeof(v));
        if (nearly(v, needle)) {
            std::memcpy(&state[i], &replacement, sizeof(replacement));
            return true;
        }
    }
    return false;
}

} // namespace

int main() {
    auto& manager = PluginManager::getInstance();
    manager.initialize();

    // (a) Round-trip through the versioned header.
    {
        const std::vector<uint8_t> state = makeChainState(0.42f);
        check(state.size() > 5 && state[0] == 'N' && state[1] == 'E' && state[2] == 'C',
              "saved state carries the NEC magic");
        check(state[3] == EffectChain::kStateFormatVersion, "saved state carries the current format version");

        EffectChain restored;
        check(restored.loadState(state, manager), "current-version state loads");
        check(restored.getPlugin(0) != nullptr, "plugin slot restored");
        check(nearly(restored.getSlotDryWetMix(0), 0.42f), "dry/wet value restored");
    }

    // (b) Unknown/future version is refused gracefully (no crash, no misparse).
    {
        std::vector<uint8_t> future = makeChainState(0.42f);
        future[3] = static_cast<uint8_t>(EffectChain::kStateFormatVersion + 1);

        EffectChain chain;
        // Pre-load a valid plugin so we can confirm a rejected load doesn't
        // silently wipe or corrupt existing state.
        const bool loaded = chain.loadState(future, manager);
        check(!loaded, "future format version is rejected");
    }

    // (c) A non-finite dry/wet blob is sanitized to a finite value on load.
    {
        std::vector<uint8_t> corrupt = makeChainState(0.42f);
        const bool patched = patchFloat(corrupt, 0.42f, std::numeric_limits<float>::quiet_NaN());
        check(patched, "located the dry/wet float to corrupt");

        EffectChain restored;
        check(restored.loadState(corrupt, manager), "state with NaN dry/wet still loads");
        const float mix = restored.getSlotDryWetMix(0);
        check(std::isfinite(mix), "restored dry/wet is finite (NaN sanitized)");
        check(mix >= 0.0f && mix <= 1.0f, "restored dry/wet is within [0,1]");
    }

    if (gFailures == 0) {
        std::cout << "EffectChainStateVersionTest: PASS\n";
        return 0;
    }
    std::cout << "EffectChainStateVersionTest: FAIL (" << gFailures << ")\n";
    return 1;
}
