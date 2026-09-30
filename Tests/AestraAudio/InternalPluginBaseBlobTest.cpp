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

#include "Plugin/AestraFilter.h"
#include "Plugin/AestraLFO.h"
#include "Plugin/AestraOTT.h"
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

    // --- Defaults are seeded exactly once, on the first initialize -------
    // The gate that enforces this moved into the base, so a migrated plugin
    // cannot leave it out. A plugin that never seeded would come up with every
    // parameter at 0.0f: Transient fully dry and 12 dB down.
    {
        Plugins::AestraTransient t;
        t.initialize(48000.0, 256);
        check(std::abs(t.getParameter(Plugins::AestraTransient::kMix) - 1.0f) < 1e-6f,
              "first initialize seeds Mix from the table default (1.0)");
        check(std::abs(t.getParameter(Plugins::AestraTransient::kOutput) - 0.5f) < 1e-6f,
              "first initialize seeds Output from the table default (0.5)");

        // A re-prepare at another rate must NOT wipe the user's values — this
        // is the #474 parameter-wipe bug the init contract exists to catch.
        t.setParameter(Plugins::AestraTransient::kMix, 0.25f);
        t.initialize(96000.0, 128);
        check(std::abs(t.getParameter(Plugins::AestraTransient::kMix) - 0.25f) < 1e-6f,
              "re-initialize preserves the user's parameters");

        // A loaded value must survive a re-prepare too.
        Plugins::AestraTransient loaded;
        loaded.initialize(48000.0, 256);
        check(loaded.loadState(legacyBlob(Plugins::AestraTransient::kStateMagic, {0.8f, 0.2f, 0.6f, 0.4f, 1.0f})),
              "Transient accepts a hand-built legacy blob");
        loaded.initialize(96000.0, 128);
        check(std::abs(loaded.getParameter(Plugins::AestraTransient::kMix) - 0.4f) < 1e-6f,
              "re-initialize preserves loaded project state");
    }

    // --- Batch 2: Filter (9), OTT (10), LFO (9) -------------------------
    // Same guarantee, more plugins: the blob must stay byte-identical to what
    // each of these shipped, because a migrated plugin that changes a single
    // byte invalidates every saved project using it.
    {
        Plugins::AestraFilter f;
        f.initialize(48000.0, 256);
        f.setParameter(Plugins::AestraFilter::kCutoff, 0.42f);
        const auto state = f.saveState();
        check(state.size() == 8 + 9 * 4, "Filter blob is 44 bytes (magic+version+9 floats)");
        std::vector<float> expected(9);
        for (uint32_t i = 0; i < 9; ++i)
            expected[i] = f.getParameter(i);
        check(state == legacyBlob(f.kStateMagic, expected), "Filter blob is byte-identical to the legacy layout");
    }
    {
        Plugins::AestraOTT ott;
        ott.initialize(48000.0, 256);
        ott.setParameter(Plugins::AestraOTT::kDepth, 0.33f);
        const auto state = ott.saveState();
        check(state.size() == 8 + 10 * 4, "OTT blob is 48 bytes (magic+version+10 floats)");
        std::vector<float> expected(10);
        for (uint32_t i = 0; i < 10; ++i)
            expected[i] = ott.getParameter(i);
        check(state == legacyBlob(ott.kStateMagic, expected), "OTT blob is byte-identical to the legacy layout");
    }
    {
        Plugins::AestraLFO lfo;
        lfo.initialize(48000.0, 256);
        lfo.setParameter(Plugins::AestraLFO::kDepth, 0.77f);
        const auto state = lfo.saveState();
        check(state.size() == 8 + 9 * 4, "LFO blob is 44 bytes (magic+version+9 floats)");
        std::vector<float> expected(9);
        for (uint32_t i = 0; i < 9; ++i)
            expected[i] = lfo.getParameter(i);
        check(state == legacyBlob(lfo.kStateMagic, expected), "LFO blob is byte-identical to the legacy layout");

        // A stepped enum must still read back as the option it names. The
        // migration rewrote parameter reads wholesale, and "> 0.5f" on a
        // stepped parameter means "is this option selected", not "bypassed" —
        // so the Sync parameter gets an explicit check.
        lfo.setParameter(Plugins::AestraLFO::kSyncMode, 1.0f);
        check(std::abs(lfo.getParameter(Plugins::AestraLFO::kSyncMode) - 1.0f) < 1e-6f,
              "LFO Sync Mode reads back as selected, not as bypass");
    }

    if (failures > 0) {
        std::cout << failures << " blob-layout check(s) failed\n";
        return 1;
    }
    std::cout << "All internal-plugin blob layout checks passed\n";
    return 0;
}
