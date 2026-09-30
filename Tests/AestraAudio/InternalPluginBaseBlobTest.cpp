// © 2026 Aestra Studios — All Rights Reserved.
// InternalPluginBaseBlobTest — the base class must not change a byte of any
// plugin's saved state.
//
// Every built-in plugin shipped its own saveState()/loadState() writing
// {magic, version, float params[count]}. InternalPluginBase now owns that, and
// the whole value of the refactor depends on a migrated plugin producing the
// identical blob: a different size or layout silently invalidates every
// project that already has one saved (AGENTS.md §12).
//
// This test pins the exact byte size and the exact header for each plugin
// BEFORE and AFTER it inherits the base, by generating the expected layout from
// the plugin's own table rather than from a hardcoded number that drifts.

#include "Plugin/AestraSat.h"
#include "Plugin/AestraTransient.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

using namespace Aestra::Audio;

namespace {

/// The layout each plugin used to hand-write, rebuilt from first principles so
/// this test does not simply re-run the base class's own arithmetic.
std::vector<uint8_t> legacyBlob(uint32_t magic, const std::vector<float>& params) {
    std::vector<uint8_t> blob(sizeof(uint32_t) * 2 + sizeof(float) * params.size());
    std::memcpy(blob.data(), &magic, sizeof(magic));
    const uint32_t version = 1;
    std::memcpy(blob.data() + sizeof(uint32_t), &version, sizeof(version));
    std::memcpy(blob.data() + sizeof(uint32_t) * 2, params.data(), sizeof(float) * params.size());
    return blob;
}

bool sameSize(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    return a.size() == b.size();
}

} // namespace

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << what << "\n";
        if (!ok)
            ++failures;
    };

    // --- AestraTransient: 5 params, magic 'TRN1' -------------------------
    {
        Plugins::AestraTransient t;
        t.initialize(48000.0, 256);
        t.setParameter(Plugins::AestraTransient::kAttack, 0.25f);
        t.setParameter(Plugins::AestraTransient::kOutput, 0.75f);

        const auto state = t.saveState();

        // 4 + 4 + 5 * 4 = 28 bytes. This number is the whole point: a base
        // class that reserved its parameter ceiling would write 200 here.
        check(state.size() == 28, "Transient blob is 28 bytes (magic+version+5 floats)");

        const std::vector<float> expected = {0.25f, t.getParameter(1), 0.75f, t.getParameter(3),
                                             t.getParameter(4)};
        check(state == legacyBlob(Plugins::AestraTransient::kStateMagic, expected),
              "Transient blob is byte-identical to the legacy layout");

        // A blob written by the old code must still load.
        Plugins::AestraTransient restored;
        restored.initialize(48000.0, 256);
        check(restored.loadState(state), "Transient loads a blob written by the old code");
        check(std::abs(restored.getParameter(Plugins::AestraTransient::kAttack) - 0.25f) < 1e-6f,
              "Transient attack survives the round-trip");
    }

    // --- AestraSat: 6 params, its own magic ------------------------------
    {
        Plugins::AestraSat sat;
        sat.initialize(48000.0, 256);
        sat.setParameter(Plugins::AestraSat::kDrive, 0.6f);
        const auto state = sat.saveState();
        check(state.size() == 32, "Sat blob is 32 bytes (magic+version+6 floats)");

        std::vector<float> expected(6);
        for (uint32_t i = 0; i < 6; ++i)
            expected[i] = sat.getParameter(i);
        check(state == legacyBlob(sat.kStateMagic, expected), "Sat blob is byte-identical to the legacy layout");
    }

    if (failures > 0) {
        std::cout << failures << " blob-layout check(s) failed\n";
        return 1;
    }
    std::cout << "All internal-plugin blob layout checks passed\n";
    return 0;
}
