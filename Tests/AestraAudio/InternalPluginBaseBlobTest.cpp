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

#include "Plugin/AestraDelay.h"
#include "Plugin/AestraDrift.h"
#include "Plugin/AestraFilter.h"
#include "Plugin/AestraLimit.h"
#include "Plugin/AestraLFO.h"
#include "Plugin/AestraOTT.h"
#include "Plugin/AestraSat.h"
#include "Plugin/AestraTransient.h"
#include "Plugin/AestraVerb.h"

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

    // AestraLimit. Byte-identity here is a real constraint rather than a
    // courtesy: its blob is exactly {magic, version, params[4]} at 24 bytes,
    // which is what the base emits for the same magic and row count. If this
    // check ever fails, the migration changed a saved project's bytes.
    {
        Plugins::AestraLimit limit;
        limit.initialize(48000.0, 256);
        limit.setParameter(Plugins::AestraLimit::kCeiling, 0.8123f);
        limit.setParameter(Plugins::AestraLimit::kRelease, 0.4456f);
        const auto state = limit.saveState();
        check(state.size() == 8 + 4 * 4, "Limit blob is 24 bytes (magic+version+4 floats)");
        std::vector<float> expected(4);
        for (uint32_t i = 0; i < 4; ++i)
            expected[i] = limit.getParameter(i);
        check(state == legacyBlob(limit.kStateMagic, expected), "Limit blob is byte-identical to the legacy layout");
    }

    // AestraVerb. This plugin wrote version 5 and carried readers for v1..v5.
    // Under the pre-user format reset those readers are gone and the base emits
    // version 1, so the blob is NOT byte-identical to what shipped before. What
    // is pinned here is the NEW canonical contract, plus the fact that a
    // pre-reset blob is now rejected outright rather than half-read: a format
    // that silently accepted v5 would mean the reset never happened.
    {
        Plugins::AestraVerb verb;
        verb.initialize(48000.0, 256);
        verb.setParameter(Plugins::AestraVerb::kDecay, 0.6789f);
        verb.setParameter(Plugins::AestraVerb::kMix, 0.3210f);
        const auto state = verb.saveState();
        check(state.size() == 8 + Plugins::AestraVerb::kParamCount * 4,
              "Verb blob is magic+version+one float per parameter");

        uint32_t writtenMagic = 0;
        uint32_t writtenVersion = 0;
        std::memcpy(&writtenMagic, state.data(), sizeof(writtenMagic));
        std::memcpy(&writtenVersion, state.data() + sizeof(uint32_t), sizeof(writtenVersion));
        check(writtenMagic == verb.kStateMagic, "Verb blob keeps its plugin magic");
        check(writtenVersion == 1u, "Verb blob is written at the canonical base version 1");

        std::vector<float> expected(Plugins::AestraVerb::kParamCount);
        for (uint32_t i = 0; i < Plugins::AestraVerb::kParamCount; ++i)
            expected[i] = verb.getParameter(i);
        check(state == legacyBlob(verb.kStateMagic, expected),
              "Verb blob matches the canonical {magic, version=1, params[]} layout");

        // The pre-reset shape: same magic and size, version 5. Must be rejected,
        // not silently interpreted as v1 parameters.
        Plugins::AestraVerb fresh;
        fresh.initialize(48000.0, 256);
        auto legacy = legacyBlob(fresh.kStateMagic, expected);
        uint32_t five = 5u;
        std::memcpy(legacy.data() + sizeof(uint32_t), &five, sizeof(five));
        check(!fresh.loadState(legacy), "a pre-reset Verb blob (version 5) is rejected, not half-read");

        // And the canonical blob round-trips.
        Plugins::AestraVerb reloaded;
        reloaded.initialize(48000.0, 256);
        check(reloaded.loadState(state), "canonical Verb blob loads");
        check(std::abs(reloaded.getParameter(Plugins::AestraVerb::kDecay) - 0.6789f) < 1e-6f,
              "Verb decay survives the round-trip");
    }

    // AestraDelay and AestraDrift. Both previously wrote version 3 and carried
    // readers for their older layouts; under the reset those readers are gone
    // and the base emits version 1, so neither blob is byte-identical to what
    // shipped. What is pinned here is the NEW canonical contract, plus the fact
    // that a pre-reset blob is now rejected outright rather than half-read -- a
    // format that still quietly accepted v3 would mean the reset never happened.
    {
        Plugins::AestraDelay delay;
        delay.initialize(48000.0, 256);
        delay.setParameter(Plugins::AestraDelay::kTime, 0.375f);
        delay.setParameter(Plugins::AestraDelay::kFeedback, 0.625f);
        const auto state = delay.saveState();
        check(state.size() == 8 + 13 * 4, "Delay blob is magic+version+13 floats");

        uint32_t dmagic = 0;
        uint32_t dversion = 0;
        std::memcpy(&dmagic, state.data(), sizeof(dmagic));
        std::memcpy(&dversion, state.data() + sizeof(uint32_t), sizeof(dversion));
        check(dmagic == delay.kStateMagic, "Delay blob keeps its plugin magic ('DLY' v3 value)");
        check(dversion == 1u, "Delay blob is written at the canonical base version 1");

        std::vector<float> dexpected(13);
        for (uint32_t i = 0; i < 13; ++i)
            dexpected[i] = delay.getParameter(i);
        check(state == legacyBlob(delay.kStateMagic, dexpected),
              "Delay blob matches the canonical {magic, version=1, params[]} layout");

        Plugins::AestraDelay dfresh;
        dfresh.initialize(48000.0, 256);
        auto dlegacy = legacyBlob(dfresh.kStateMagic, dexpected);
        uint32_t three = 3u;
        std::memcpy(dlegacy.data() + sizeof(uint32_t), &three, sizeof(three));
        check(!dfresh.loadState(dlegacy), "a pre-reset Delay blob (version 3) is rejected, not half-read");

        Plugins::AestraDelay dreload;
        dreload.initialize(48000.0, 256);
        check(dreload.loadState(state), "canonical Delay blob loads");
        check(std::abs(dreload.getParameter(Plugins::AestraDelay::kTime) - 0.375f) < 1e-6f,
              "Delay time survives the round-trip");

        // The Division default is a computed expression (kDiv1_8 / 12.0f) that
        // moved from a runtime local into the constexpr table. Prove it folded
        // to the same value rather than trusting that it did.
        check(std::abs(delay.getParameter(Plugins::AestraDelay::kNoteDivision) - 4.0f / 12.0f) < 1e-6f,
              "Delay Division default folded to kDiv1_8/12 in the constexpr table");
    }

    {
        Plugins::AestraDrift drift;
        drift.initialize(48000.0, 256);
        drift.setParameter(Plugins::AestraDrift::kPitch, 0.8125f);
        drift.setParameter(Plugins::AestraDrift::kTexture, 0.4375f);
        const auto state = drift.saveState();
        check(state.size() == 8 + 10 * 4, "Drift blob is magic+version+10 floats");

        uint32_t gmagic = 0;
        uint32_t gversion = 0;
        std::memcpy(&gmagic, state.data(), sizeof(gmagic));
        std::memcpy(&gversion, state.data() + sizeof(uint32_t), sizeof(gversion));
        check(gmagic == drift.kStateMagic, "Drift blob keeps its plugin magic");
        check(gversion == 1u, "Drift blob is written at the canonical base version 1");

        std::vector<float> gexpected(10);
        for (uint32_t i = 0; i < 10; ++i)
            gexpected[i] = drift.getParameter(i);
        check(state == legacyBlob(drift.kStateMagic, gexpected),
              "Drift blob matches the canonical {magic, version=1, params[]} layout");

        Plugins::AestraDrift gfresh;
        gfresh.initialize(48000.0, 256);
        auto glegacy = legacyBlob(gfresh.kStateMagic, gexpected);
        uint32_t gthree = 3u;
        std::memcpy(glegacy.data() + sizeof(uint32_t), &gthree, sizeof(gthree));
        check(!gfresh.loadState(glegacy), "a pre-reset Drift blob (version 3) is rejected, not half-read");

        Plugins::AestraDrift greload;
        greload.initialize(48000.0, 256);
        check(greload.loadState(state), "canonical Drift blob loads");
        check(std::abs(greload.getParameter(Plugins::AestraDrift::kPitch) - 0.8125f) < 1e-6f,
              "Drift pitch survives the round-trip");
    }

    if (failures > 0) {
        std::cout << failures << " blob-layout check(s) failed\n";
        return 1;
    }
    std::cout << "All internal-plugin blob layout checks passed\n";
    return 0;
}
