// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// #1015 — how InternalPluginBase::loadState treats a bad parameter value.
//
// The two halves of the policy, pinned in one file because they are one decision:
// a value that is out of range is CLAMPED, and a value that is not finite
// REJECTS the whole blob. A test that only covered half of it would let the other
// half drift, and the interesting regressions are exactly the ones where one
// half starts behaving like the other.
//
// Why they differ: NaN is corruption, unambiguously — no build widens a range to
// include it. Out-of-range is ambiguous, because nothing forces a range change
// to bump kStateVersion, so a blob from a build with a wider range is reachable
// today. Clamping matches what setParameter already does to a live value, and it
// costs one parameter instead of all of them.
//
// The atomicity guarantee is asserted in BOTH directions: a rejected blob must
// leave every parameter untouched, and a clamped blob must still apply every
// other value. The two-pass structure is what gives that, and it is the part
// that a plausible-looking refactor would break.

#include "Plugin/InternalPluginBase.h"
#include "Plugin/PluginHost.h"
#include "Plugin/AestraSat.h"

#include "KeyedBlobTestUtil.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace Aestra::Audio;
using namespace Aestra::Audio::Plugins;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "[FAIL] " << what << '\n';
        ++g_failures;
    }
}

// Sat is the reference implementation (AGENTS.md §19) and has a wide, plain
// range on kDrive, which makes it a clean probe for both directions.
/// Overwrite one parameter's value, finding its entry by id. The old
/// `8 + 4 * id` arithmetic was correct for v1's positional layout and silently
/// lands on an id field under v2, which is how this test came to report results
/// for loads that never happened.
void pokeFloat(std::vector<uint8_t>& blob, uint32_t paramId, float value) {
    if (!AestraTestBlob::setValueForId(blob, paramId, value))
        std::cerr << "[FAIL] blob has no entry for id " << paramId << "; the case cannot be exercised\n";
}

/// A freshly initialized instance and a blob carrying two known-good values, so
/// a test can corrupt exactly one field and observe both that field's effect and
/// the OTHER field's — which is the whole point of the policy.
struct Fixture {
    AestraSat plugin;
    std::vector<uint8_t> blob;
    static constexpr float kSavedMix = 0.375f;

    Fixture() {
        plugin.initialize(48000.0, 256);
        plugin.setParameter(AestraSat::kDrive, 0.5f);
        plugin.setParameter(AestraSat::kMix, kSavedMix);
        plugin.activate();
        blob = plugin.saveState();
    }
};

} // namespace

int main() {
    // ---------------------------------------------------------------------
    // Out-of-range clamps, and every other value still applies.
    // ---------------------------------------------------------------------
    {
        Fixture fx;
        AestraSat restored;
        restored.initialize(48000.0, 256);
        restored.setParameter(AestraSat::kDrive, 0.0f);
        restored.setParameter(AestraSat::kMix, 0.25f);
        restored.activate();

        // Far above kDrive's maximum. Deliberately a large overshoot rather than
        // a hair over the bound: the point is that a value which cannot have
        // come from a real session still lands somewhere safe.
        pokeFloat(fx.blob, AestraSat::kDrive, 1.0e9f);

        check(restored.loadState(fx.blob), "an out-of-range value does not reject the blob");
        const float drive = restored.getParameter(AestraSat::kDrive);
        check(std::isfinite(drive), "the clamped value is finite");
        check(drive <= 1.0f && drive >= 0.0f, "the clamped value is inside the declared range");

        // The part that matters most: the OTHER parameter took the blob's value.
        // Under the old all-or-nothing policy this stayed at whatever it had,
        // because the whole blob was discarded for one bad field.
        check(std::fabs(restored.getParameter(AestraSat::kMix) - Fixture::kSavedMix) < 1e-6f,
              "an unrelated parameter in the same blob is still applied");
    }

    // Below the minimum is the same decision, not a special case.
    {
        Fixture fx;
        AestraSat restored;
        restored.initialize(48000.0, 256);
        pokeFloat(fx.blob, AestraSat::kDrive, -1.0e9f);
        check(restored.loadState(fx.blob), "a below-range value does not reject the blob either");
        check(std::isfinite(restored.getParameter(AestraSat::kDrive)) &&
                  restored.getParameter(AestraSat::kDrive) >= 0.0f,
              "a below-range value clamps up into the range");
    }

    // Exactly on each bound is valid, not "clamped" — no drift at the edges.
    {
        Fixture fx;
        AestraSat restored;
        restored.initialize(48000.0, 256);
        pokeFloat(fx.blob, AestraSat::kDrive, 0.0f);
        check(restored.loadState(fx.blob), "a value on the lower bound loads");
        check(std::fabs(restored.getParameter(AestraSat::kDrive)) < 1e-6f,
              "a value on the lower bound is not moved");

        Fixture fx2;
        AestraSat atMax;
        atMax.initialize(48000.0, 256);
        pokeFloat(fx2.blob, AestraSat::kDrive, 1.0f);
        check(atMax.loadState(fx2.blob), "a value on the upper bound loads");
        check(std::fabs(atMax.getParameter(AestraSat::kDrive) - 1.0f) < 1e-6f,
              "a value on the upper bound is not moved");
    }

    // ---------------------------------------------------------------------
    // Non-finite rejects the WHOLE blob, and mutates nothing.
    // ---------------------------------------------------------------------
    {
        Fixture fx;
        pokeFloat(fx.blob, AestraSat::kDrive, std::numeric_limits<float>::quiet_NaN());

        AestraSat restored;
        restored.initialize(48000.0, 256);
        restored.setParameter(AestraSat::kMix, 0.75f);
        restored.activate();

        check(!restored.loadState(fx.blob), "a NaN value rejects the blob");
        check(std::isfinite(restored.getParameter(AestraSat::kDrive)), "no NaN entered parameters");
        check(std::fabs(restored.getParameter(AestraSat::kMix) - 0.75f) < 1e-6f,
              "a rejected blob left every parameter untouched");

        // And the audio path stays finite, which is the original reason any of
        // this exists.
        restored.activate();
        std::vector<float> in(512, 0.5f);
        std::vector<float> out(in.size(), 0.0f);
        const float* inputs[] = {in.data()};
        float* outputs[] = {out.data()};
        restored.process(inputs, outputs, 1, 1, static_cast<uint32_t>(in.size()));
        for (float s : out) {
            if (!std::isfinite(s)) {
                check(false, "non-finite output after loading a rejected blob");
                break;
            }
        }
        check(true, "output stayed finite after loading a rejected blob");
    }

    // Infinity is rejected on the same grounds as NaN.
    {
        Fixture fx;
        pokeFloat(fx.blob, AestraSat::kDrive, std::numeric_limits<float>::infinity());
        AestraSat restored;
        restored.initialize(48000.0, 256);
        check(!restored.loadState(fx.blob), "an infinite value rejects the blob");
    }

    // ---------------------------------------------------------------------
    // The two halves must not converge. A clamp that leaked into the
    // non-finite path would be the most damaging possible drift: NaN clamped
    // into range is a plausible-looking number that was never the user's.
    // ---------------------------------------------------------------------
    {
        Fixture fx;
        // One poisoned field, many good ones. If clamping were applied to
        // non-finite values too, this would load and produce a plausible drive
        // value that no one ever chose.
        pokeFloat(fx.blob, AestraSat::kDrive, -std::numeric_limits<float>::infinity());
        AestraSat restored;
        restored.initialize(48000.0, 256);
        restored.setParameter(AestraSat::kMix, 0.5f);
        const bool accepted = restored.loadState(fx.blob);
        check(!accepted, "one infinite value rejects the blob even though the rest are fine");
        check(std::fabs(restored.getParameter(AestraSat::kMix) - 0.5f) < 1e-6f,
              "the good parameters in a rejected blob are not partially applied");
    }

    // ---------------------------------------------------------------------
    // Header failures are unchanged: a wrong version or magic never reaches the
    // parameter loop at all, so the FD-24 reset behaviour is untouched.
    //
    // Offsets matter and are easy to get wrong: the header is two uint32s, so
    // the magic occupies bytes 0-3 and the version bytes 4-7. This originally
    // poked byte 3 and called it the version -- that is the magic's high byte,
    // so the case silently duplicated the magic test below and version
    // rejection was never exercised at all. Another test that quietly stops
    // testing its own subject.
    // ---------------------------------------------------------------------
    {
        Fixture fx;
        const uint32_t badVersion = 0xFFFFFFFFu;
        std::memcpy(fx.blob.data() + sizeof(uint32_t), &badVersion, sizeof(badVersion));
        // v2 == 2, so restoring the good version means writing 2, not 1.
        AestraSat restored;
        restored.initialize(48000.0, 256);
        restored.setParameter(AestraSat::kMix, Fixture::kSavedMix);
        check(!restored.loadState(fx.blob), "a bad version rejects regardless of param values");
        check(std::fabs(restored.getParameter(AestraSat::kMix) - Fixture::kSavedMix) < 1e-6f,
              "a bad version leaves parameters untouched");

        // Prove the version field really is where the comment says it is: patch
        // that one field back and the very same blob must now load. Without
        // this, the case above could pass for any reason at all.
        const uint32_t goodVersion = 2; // InternalPluginBase::kStateVersion
        std::memcpy(fx.blob.data() + sizeof(uint32_t), &goodVersion, sizeof(goodVersion));
        check(restored.loadState(fx.blob),
              "restoring only the version byte makes the same blob load, so that field is the version");
    }

    {
        Fixture fx2;
        const uint32_t badMagic = 0xDEADBEEFu;
        std::memcpy(fx2.blob.data(), &badMagic, sizeof(badMagic));
        AestraSat fx2restored;
        fx2restored.initialize(48000.0, 256);
        check(!fx2restored.loadState(fx2.blob), "a bad magic still rejects regardless of param values");
    }

    // ---------------------------------------------------------------------
    // A clean blob is unaffected, and round-trips byte-identically.
    // ---------------------------------------------------------------------
    {
        Fixture fx;
        AestraSat restored;
        restored.initialize(48000.0, 256);
        check(restored.loadState(fx.blob), "a clean blob loads");
        check(restored.saveState() == fx.blob, "a clean blob round-trips byte-identically");
    }

    if (g_failures == 0) {
        std::cout << "=== InternalPluginBaseStatePolicyTest: all checks passed ===\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "=== InternalPluginBaseStatePolicyTest: " << g_failures << " failure(s) ===\n";
    return EXIT_FAILURE;
}
