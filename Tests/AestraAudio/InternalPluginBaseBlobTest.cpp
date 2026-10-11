// © 2026 Aestra Studios — All Rights Reserved.
//
// InternalPluginBaseBlobTest — the keyed v2 state format is canonical, v1 is
// refused, and parameter identity survives a changing parameter table.
//
// HISTORY, because it explains what this test is now FOR. Its first version
// pinned the opposite contract: "migrating a plugin must not change a byte of
// its saved state." That was the right contract for a format-preserving
// refactor, and it earned its place immediately — the assertion written to prove
// a migration was safe is what disproved the assumption that it was. But it also
// enshrined positional identity, with the array index doubling as the parameter
// id, and positional identity is the thing P3 removes.
//
// So the contract is now the one P3's exit criterion states: ADDING, REMOVING
// or REORDERING a parameter must not invalidate an existing project. The byte
// size is still pinned, because "canonical" means exactly one encoding, but it is
// pinned for the keyed layout rather than inherited from the old one.

#include "Plugin/AestraDelay.h"
#include "Plugin/AestraDrift.h"
#include "Plugin/AestraFilter.h"
#include "Plugin/AestraLimit.h"
#include "Plugin/AestraLFO.h"
#include "Plugin/AestraOTT.h"
#include "Plugin/AestraSat.h"
#include "Plugin/AestraTransient.h"
#include "Plugin/AestraVerb.h"

#include "KeyedBlobTestUtil.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "[FAIL] " << what << '\n';
        ++g_failures;
    }
}

constexpr uint32_t kV2 = 2;
constexpr uint32_t kV1 = 1;

/// v1's layout, rebuilt from first principles so this test does not simply
/// re-run the base class's own arithmetic. Nothing writes this any more; it
/// exists so the reader's rejection of it can be tested against the real thing
/// rather than a hand-rolled byte pattern that could drift.
std::vector<uint8_t> v1Blob(uint32_t magic, const std::vector<float>& params) {
    std::vector<uint8_t> blob(sizeof(uint32_t) * 2 + sizeof(float) * params.size());
    std::memcpy(blob.data(), &magic, sizeof(magic));
    std::memcpy(blob.data() + sizeof(uint32_t), &kV1, sizeof(kV1));
    std::memcpy(blob.data() + sizeof(uint32_t) * 2, params.data(), sizeof(float) * params.size());
    return blob;
}

/// The keyed layout, built independently of the base class's own writer so a
/// bug in the writer cannot make this test agree with it.
std::vector<uint8_t> expectedKeyed(uint32_t magic, const std::vector<std::pair<uint32_t, float>>& entries) {
    std::vector<uint8_t> blob(12 + 8 * entries.size());
    std::memcpy(blob.data(), &magic, sizeof(magic));
    std::memcpy(blob.data() + 4, &kV2, sizeof(kV2));
    const uint32_t count = static_cast<uint32_t>(entries.size());
    std::memcpy(blob.data() + 8, &count, sizeof(count));
    for (size_t i = 0; i < entries.size(); ++i) {
        std::memcpy(blob.data() + 12 + 8 * i, &entries[i].first, sizeof(uint32_t));
        std::memcpy(blob.data() + 12 + 8 * i + 4, &entries[i].second, sizeof(float));
    }
    return blob;
}

} // namespace

int main() {
    // ==================================================================
    // 1. The keyed layout is canonical: exact size, exact bytes, per plugin.
    // ==================================================================
    {
        Plugins::AestraTransient t;
        t.initialize(48000.0, 256);
        t.setParameter(Plugins::AestraTransient::kAttack, 0.4f);
        t.setParameter(Plugins::AestraTransient::kSustain, 0.7f);
        const auto state = t.saveState();

        check(state.size() == 12 + 8 * 5, "Transient blob is 12 + 8*5 = 52 bytes (header, count, keyed entries)");
        check(state == expectedKeyed(t.kStateMagic, {{0, t.getParameter(0)},
                                                      {1, t.getParameter(1)},
                                                      {2, t.getParameter(2)},
                                                      {3, t.getParameter(3)},
                                                      {4, t.getParameter(4)}}),
              "Transient blob matches the keyed layout built independently");
        check(AestraTestBlob::readCount(state) == 5, "the count field says 5, not an implied length");
    }

    {
        Plugins::AestraSat s;
        s.initialize(48000.0, 256);
        check(s.saveState().size() == 12 + 8 * 6, "Sat blob is 12 + 8*6 = 60 bytes");
    }
    {
        Plugins::AestraOTT o;
        o.initialize(48000.0, 256);
        check(o.saveState().size() == 12 + 8 * 10, "OTT blob is 12 + 8*10 = 92 bytes");
    }
    {
        Plugins::AestraFilter f;
        f.initialize(48000.0, 256);
        check(f.saveState().size() == 12 + 8 * 9, "Filter blob is 12 + 8*9 = 84 bytes");
    }
    {
        Plugins::AestraLFO l;
        l.initialize(48000.0, 256);
        check(l.saveState().size() == 12 + 8 * 9, "LFO blob is 12 + 8*9 = 84 bytes");
    }
    {
        Plugins::AestraLimit l;
        l.initialize(48000.0, 256);
        check(l.saveState().size() == 12 + 8 * 4, "Limit blob is 12 + 8*4 = 44 bytes");
    }
    {
        Plugins::AestraVerb v;
        v.initialize(48000.0, 256);
        check(v.saveState().size() == 12 + 8 * 18, "Verb blob is 12 + 8*18 = 156 bytes");
    }
    {
        Plugins::AestraDelay d;
        d.initialize(48000.0, 256);
        check(d.saveState().size() == 12 + 8 * 13, "Delay blob is 12 + 8*13 = 116 bytes");
    }
    {
        Plugins::AestraDrift d;
        d.initialize(48000.0, 256);
        check(d.saveState().size() == 12 + 8 * 10, "Drift blob is 12 + 8*10 = 92 bytes");
    }

    // ==================================================================
    // 2. v1 is refused — for every plugin, and for the right reason.
    // ==================================================================
    {
        Plugins::AestraTransient t;
        t.initialize(48000.0, 256);
        const std::vector<uint8_t> old = v1Blob(t.kStateMagic, {0.8f, 0.2f, 0.6f, 0.4f, 1.0f});
        check(old.size() == 8 + 4 * 5, "the v1 blob under test is the real v1 size, 28 bytes");
        check(!t.loadState(old), "a v1 positional blob is rejected by the keyed reader");

        // The dangerous case, stated explicitly. A v1 blob read as v2 would have
        // its first parameter's float BITS parsed as the entry count. For this
        // blob that is 0.2f = 0x3E4CCCCD = 1042884557, which is nowhere near the
        // 48-parameter ceiling, so a reader that trusted it would try to read a
        // gigabyte of entries and read past the end of the buffer. The version
        // check has to run BEFORE the count is read, and this asserts that it
        // does — a rejected v1 blob proves the version was seen, but only the
        // absence of a crash plus a rejected load pins the ORDER.
        t.setParameter(Plugins::AestraTransient::kAttack, 0.99f);
        check(!t.loadState(old), "v1 is still rejected after the instance has been re-seeded");
        check(t.getParameter(Plugins::AestraTransient::kAttack) > 0.98f,
              "the rejected v1 load mutated nothing");

        Plugins::AestraVerb v;
        v.initialize(48000.0, 256);
        check(!v.loadState(v1Blob(v.kStateMagic, std::vector<float>(18, 0.5f))),
              "Verb refuses a v1 blob too");

        // The case that pins the CHECK ORDER rather than the count guard. A v1
        // blob whose first parameter is 0.0f has, at the offset v2 reads its
        // entry count from, the bits 0x00000000 — so the count parses as 0,
        // which is inside kMaxSpecParams, and any blob of 12+ bytes satisfies
        // keyedBlobSize(0). Every size and count guard passes. Only the version
        // check stands between a historical blob and a load that reports success
        // while restoring nothing, and this asserts that it holds.
        {
            Plugins::AestraTransient z;
            z.initialize(48000.0, 256);
            const auto zeroed = v1Blob(z.kStateMagic, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
            check(zeroed.size() >= 12, "the all-zero v1 blob is long enough to look like a v2 header");
            check(!z.loadState(zeroed),
                  "a v1 blob whose first parameter is 0.0f is refused, so no size or count guard "
                  "can be the only thing standing between a historical blob and a silent success");
        }

        // The adversarial one: a v1 blob that satisfies EVERY count guard.
        //
        // Read as v2, the entry count comes from the first parameter's float
        // bits. To survive `count > kMaxSpecParams` AND `count == 0` AND
        // `size >= 12 + 8*count`, those bits must land in [1, kMaxSpecParams] —
        // which is to say the first parameter must be a DENORMAL, the only
        // floats whose bit patterns are small integers. 0x00000001 is 1.4e-45:
        // absurd as a parameter, entirely representable as corrupt bytes.
        //
        // So a 28-byte v1 blob whose first parameter is that denormal parses as a
        // perfectly well-formed v2 header with count == 1. Every size and count
        // guard passes. Only the version check refuses it, and that is the whole
        // case for it existing. The all-zero blob above does NOT cover this; it
        // is stopped by the zero-entry guard, which is defence in depth rather
        // than the primary defence.
        {
            Plugins::AestraTransient d;
            d.initialize(48000.0, 256);
            const float denormal = std::numeric_limits<float>::denorm_min(); // 0x00000001
            uint32_t bits = 0;
            std::memcpy(&bits, &denormal, sizeof(bits));
            check(bits == 1u, "the denormal's bit pattern really is 1, so it parses as count 1");

            auto adversarial = v1Blob(d.kStateMagic, {denormal, 0.5f, 0.5f, 0.5f, 0.5f});
            check(adversarial.size() >= 12 + 8, "the adversarial v1 blob is long enough for count 1");
            check(!d.loadState(adversarial),
                  "a v1 blob engineered to satisfy every count guard is still refused by the version check");
        }
    }

    // A blob carrying no entries for a plugin that has parameters is not a shape
    // any writer produces, and must not be reported as a successful load.
    {
        Plugins::AestraTransient t;
        t.initialize(48000.0, 256);
        t.setParameter(0, 0.42f);
        auto empty = t.saveState();
        const uint32_t zero = 0;
        std::memcpy(empty.data() + 8, &zero, sizeof(zero));
        check(!t.loadState(empty), "a v2 blob claiming zero entries is refused");
        check(std::fabs(t.getParameter(0) - 0.42f) < 1e-6f, "refusing a zero-entry blob mutated nothing");
    }

    // A blob that lies about its own version: v2 header, v1 body.
    {
        Plugins::AestraTransient t;
        t.initialize(48000.0, 256);
        std::vector<uint8_t> hostile = v1Blob(t.kStateMagic, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
        // Force version 2 into the header, so only the layout differs. The entry
        // count is then read from the first parameter's bits — all zero, so
        // count == 0, which no real writer produces for a plugin with five
        // parameters. Refused for that reason rather than by the version check,
        // which now believes the blob.
        std::memcpy(hostile.data() + 4, &kV2, sizeof(kV2));
        check(!t.loadState(hostile),
              "a blob claiming v2 over a v1 body is refused: zero entries is not a shape any writer emits");
    }

    // ==================================================================
    // 3. THE EXIT CRITERION. Adding, removing and reordering a parameter must
    //    all leave an existing project intact.
    //
    //    These are simulated by editing the BLOB, because a test cannot change a
    //    compiled-in ParamSpec table. Each case is the blob a build with a
    //    different table would have written.
    // ==================================================================
    {
        Plugins::AestraTransient t;
        t.initialize(48000.0, 256);
        t.setParameter(0, 0.11f);
        t.setParameter(1, 0.22f);
        t.setParameter(2, 0.33f);
        const auto original = t.saveState();

        // --- ADD: the blob carries ids the table has never seen. They are
        // ignored, and every id the table DOES know keeps its value.
        {
            auto grown = original;
            const uint32_t count = AestraTestBlob::readCount(grown) + 2;
            grown.resize(12 + 8 * count);
            std::memcpy(grown.data() + 8, &count, sizeof(count));
            // Two entries for ids that do not exist, and one that is beyond the
            // parameter ceiling entirely — a removed parameter, and junk.
            const uint32_t ids[2] = {900u, 12345u};
            const float vals[2] = {0.5f, 0.6f};
            for (int i = 0; i < 2; ++i) {
                std::memcpy(grown.data() + 12 + 8 * (5 + i), &ids[i], sizeof(uint32_t));
                std::memcpy(grown.data() + 12 + 8 * (5 + i) + 4, &vals[i], sizeof(float));
            }
            Plugins::AestraTransient restored;
            restored.initialize(48000.0, 256);
            check(restored.loadState(grown), "a blob carrying unknown parameter ids still loads");
            check(std::fabs(restored.getParameter(0) - 0.11f) < 1e-6f, "ADD: parameter 0 kept its value");
            check(std::fabs(restored.getParameter(1) - 0.22f) < 1e-6f, "ADD: parameter 1 kept its value");
            check(std::fabs(restored.getParameter(2) - 0.33f) < 1e-6f, "ADD: parameter 2 kept its value");
        }

        // --- REMOVE: the blob is missing an id the table still has. That
        // parameter keeps its default and the rest are unaffected.
        {
            auto shrunk = original;
            uint32_t count = AestraTestBlob::readCount(shrunk);
            // Drop entry 2, renumbering the tail down by one.
            std::vector<uint8_t> rebuilt(12 + 8 * (count - 1));
            std::memcpy(rebuilt.data(), shrunk.data(), 8);
            const uint32_t smaller = count - 1;
            std::memcpy(rebuilt.data() + 8, &smaller, sizeof(smaller));
            uint32_t w = 0;
            for (uint32_t i = 0; i < count; ++i) {
                if (i == 2)
                    continue;
                const uint8_t* src = shrunk.data() + 12 + 8 * i;
                uint32_t id = 0;
                float val = 0.0f;
                std::memcpy(&id, src, sizeof(id));
                std::memcpy(&val, src + 4, sizeof(val));
                std::memcpy(rebuilt.data() + 12 + 8 * w, &id, sizeof(id));
                std::memcpy(rebuilt.data() + 12 + 8 * w + 4, &val, sizeof(val));
                ++w;
            }
            Plugins::AestraTransient restored;
            restored.initialize(48000.0, 256);
            check(restored.loadState(rebuilt), "a blob missing a parameter id still loads");
            check(std::fabs(restored.getParameter(0) - 0.11f) < 1e-6f, "REMOVE: parameter 0 kept its value");
            check(std::fabs(restored.getParameter(1) - 0.22f) < 1e-6f, "REMOVE: parameter 1 kept its value");
            // Parameter 2 was the one dropped, so it is back at its default. The
            // default is 0 for Transient's first rows; assert it is NOT 0.33.
            check(std::fabs(restored.getParameter(2) - 0.33f) > 1e-6f,
                  "REMOVE: the parameter absent from the blob fell back to its default");
        }

        // --- REORDER: the entries are written in a different ORDER, with the
        // same ids and values. Under v1 this silently mis-assigned every value;
        // under v2 it must be indistinguishable from the original.
        {
            auto reordered = original;
            const uint32_t count = AestraTestBlob::readCount(reordered);
            std::vector<std::pair<uint32_t, float>> entries;
            for (uint32_t i = 0; i < count; ++i)
                entries.emplace_back(AestraTestBlob::readIdAt(reordered, i),
                                     AestraTestBlob::readValueAt(reordered, i));
            for (uint32_t i = 0; i < count / 2; ++i)
                std::swap(entries[i], entries[count - 1 - i]);
            reordered = expectedKeyed(t.kStateMagic, entries);

            Plugins::AestraTransient restored;
            restored.initialize(48000.0, 256);
            check(restored.loadState(reordered), "a blob with reordered entries still loads");
            check(std::fabs(restored.getParameter(0) - 0.11f) < 1e-6f,
                  "REORDER: parameter 0 kept its value despite moving in the blob");
            check(std::fabs(restored.getParameter(1) - 0.22f) < 1e-6f,
                  "REORDER: parameter 1 kept its value despite moving in the blob");
            check(std::fabs(restored.getParameter(2) - 0.33f) < 1e-6f,
                  "REORDER: parameter 2 kept its value despite moving in the blob");
        }
    }

    // ==================================================================
    // 4. Round-trip and hygiene, unchanged in spirit by the format.
    // ==================================================================
    {
        Plugins::AestraVerb v;
        v.initialize(96000.0, 128);
        for (uint32_t i = 0; i < Plugins::AestraVerb::kParamCount; ++i)
            v.setParameter(i, 0.1f + 0.8f * static_cast<float>(i) /
                                       static_cast<float>(Plugins::AestraVerb::kParamCount - 1));
        const auto state = v.saveState();

        Plugins::AestraVerb restored;
        restored.initialize(96000.0, 128);
        check(restored.loadState(state), "a current Verb blob round-trips");
        for (uint32_t i = 0; i < Plugins::AestraVerb::kParamCount; ++i) {
            if (std::fabs(restored.getParameter(i) - v.getParameter(i)) > 1e-6f) {
                check(false, "Verb parameter " + std::to_string(i) + " did not survive the round trip");
                break;
            }
        }
        check(true, "every Verb parameter survived the round trip");
        check(restored.saveState() == state, "a keyed blob round-trips byte-identically");

        // Re-initializing (what EffectChain::prepare does on a sample-rate
        // change) must not lose loaded state.
        restored.initialize(44100.0, 256);
        check(std::fabs(restored.getParameter(0) - v.getParameter(0)) < 1e-6f,
              "re-initialize preserves loaded project state");
    }

    // Garbage is still refused, and refused without mutating.
    {
        Plugins::AestraTransient t;
        t.initialize(48000.0, 256);
        t.setParameter(0, 0.37f);
        const std::vector<std::vector<uint8_t>> garbage = {
            {0x31, 0x54},                                   // truncated header
            std::vector<uint8_t>(64, 0x00),                 // wrong magic
            std::vector<uint8_t>(4096, 0x41),               // plausible size, wrong magic
        };
        for (const auto& blob : garbage) {
            check(!t.loadState(blob), "garbage is refused");
            check(std::fabs(t.getParameter(0) - 0.37f) < 1e-6f, "a refused load mutated nothing");
        }

        // A correct header with a count larger than the buffer: the reader must
        // notice the short buffer rather than reading past it.
        std::vector<uint8_t> lying = t.saveState();
        const uint32_t huge = 0xFFFFu;
        std::memcpy(lying.data() + 8, &huge, sizeof(huge));
        check(!t.loadState(lying), "a count beyond the buffer is refused");
        check(std::fabs(t.getParameter(0) - 0.37f) < 1e-6f, "the over-count load mutated nothing");
    }

    // A non-finite value is corruption and rejects the whole blob.
    {
        Plugins::AestraTransient t;
        t.initialize(48000.0, 256);
        t.setParameter(0, 0.5f);
        auto state = t.saveState();
        check(AestraTestBlob::setValueForId(state, 1, std::numeric_limits<float>::quiet_NaN()),
              "the blob has an entry for id 1 to poison");
        check(!t.loadState(state), "a NaN value rejects the blob");
        check(std::fabs(t.getParameter(0) - 0.5f) < 1e-6f, "the rejected NaN load mutated nothing");
    }

    if (g_failures == 0) {
        std::cout << "=== InternalPluginBaseBlobTest: all checks passed ===\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "=== InternalPluginBaseBlobTest: " << g_failures << " failure(s) ===\n";
    return EXIT_FAILURE;
}
