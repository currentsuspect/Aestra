// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// A plugin that IS installed but rejects its own saved state must be reported,
// and its settings must survive (#1014).
//
// Two things used to go wrong at once, and the second is the serious one.
//
// Reported: the case only reached Aestra::Log::warning while the return value
// still said "the blob parsed", so the project opened cleanly and nothing said
// the settings had not been applied.
//
// Destroyed: the slot stayed LIVE and ran with DEFAULT parameters. Defaults
// sound plausible, so the user would not notice, and the next save wrote those
// defaults over the project's settings. The loss was not merely unreported, it
// was guaranteed on the first save.
//
// The fix puts a rejected state on the same path as a missing plugin: the slot
// ends up empty and the opaque blob is preserved verbatim, so the settings
// survive the round trip. What differs is the diagnosis, which is why the
// report keeps two lists -- telling a user to install a plugin they already
// have is its own kind of wrong.
//
// This test deliberately uses a REAL built-in plugin, because the whole point is
// the installed-but-rejecting branch: an unresolvable id takes the other path
// and would make the test pass for the wrong reason. That is the mirror of
// EffectChainMissingPluginTest, which deliberately depends on NO plugin being
// installed. Neither test can substitute for the other, and neither skips: if
// the plugin will not resolve this file FAILS, because a green run that never
// entered the branch is a test that has stopped testing.

#include "Plugin/EffectChain.h"
#include "Plugin/PluginManager.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
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

// A real built-in that resolves through the scanner, as MasterEffectChainTest
// already relies on.
const char* kEqId = "com.Aestrastudios.eq";
constexpr double kSampleRate = 48000.0;
constexpr uint32_t kBlockFrames = 512;

struct SlotRecord {
    bool present = false;
    uint64_t instanceId = 0;
    std::string id;
    bool bypassed = false;
    float dryWet = 1.0f;
    std::vector<uint8_t> state;
};

template <typename T> void appendRaw(std::vector<uint8_t>& out, const T& value) {
    const auto* p = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), p, p + sizeof(T));
}

// Mirrors EffectChain::saveState, written independently so the test pins the
// wire format rather than restating the implementation.
std::vector<uint8_t> buildBlob(const std::vector<SlotRecord>& slots) {
    std::vector<uint8_t> out{'N', 'E', 'C', EffectChain::kStateFormatVersion,
                             static_cast<uint8_t>(EffectChain::kInitialSlots)};
    for (size_t i = 0; i < EffectChain::kInitialSlots; ++i) {
        const SlotRecord empty;
        const SlotRecord& s = i < slots.size() ? slots[i] : empty;
        if (!s.present) {
            out.push_back(0);
            continue;
        }
        out.push_back(1);
        appendRaw(out, s.instanceId);
        appendRaw(out, static_cast<uint32_t>(s.id.size()));
        out.insert(out.end(), s.id.begin(), s.id.end());
        out.push_back(s.bypassed ? 1 : 0);
        appendRaw(out, s.dryWet);
        appendRaw(out, static_cast<uint32_t>(s.state.size()));
        out.insert(out.end(), s.state.begin(), s.state.end());
    }
    return out;
}

// Parse a blob back so assertions talk about slots rather than byte offsets.
bool parseBlob(const std::vector<uint8_t>& blob, std::vector<SlotRecord>& out) {
    out.assign(EffectChain::kInitialSlots, SlotRecord{});
    if (blob.size() < 5 || blob[0] != 'N' || blob[1] != 'E' || blob[2] != 'C') {
        return false;
    }
    if (blob[4] != static_cast<uint8_t>(EffectChain::kInitialSlots)) {
        return false;
    }
    size_t off = 5;
    for (size_t i = 0; i < EffectChain::kInitialSlots; ++i) {
        if (off >= blob.size()) return false;
        const uint8_t has = blob[off++];
        if (!has) continue;

        SlotRecord r;
        r.present = true;
        if (blob[3] >= 2) {
            if (off + sizeof(uint64_t) > blob.size()) return false;
            std::memcpy(&r.instanceId, &blob[off], sizeof(uint64_t));
            off += sizeof(uint64_t);
        }
        uint32_t idLen = 0;
        if (off + sizeof(idLen) > blob.size()) return false;
        std::memcpy(&idLen, &blob[off], sizeof(idLen));
        off += sizeof(idLen);
        if (off + idLen > blob.size()) return false;
        r.id.assign(reinterpret_cast<const char*>(&blob[off]), idLen);
        off += idLen;

        if (off + 1 + sizeof(float) + sizeof(uint32_t) > blob.size()) return false;
        r.bypassed = blob[off++] != 0;
        std::memcpy(&r.dryWet, &blob[off], sizeof(float));
        off += sizeof(float);
        uint32_t stateLen = 0;
        std::memcpy(&stateLen, &blob[off], sizeof(stateLen));
        off += sizeof(stateLen);
        if (off + stateLen > blob.size()) return false;
        r.state.assign(blob.begin() + static_cast<long>(off), blob.begin() + static_cast<long>(off + stateLen));
        off += stateLen;

        out[i] = std::move(r);
    }
    return true;
}

} // namespace

int main() {
    auto& manager = PluginManager::getInstance();
    if (!manager.initialize()) {
        // FAIL, not skip-as-pass. If the manager will not start, neither case
        // below ran, and a CTest green here would mean this file has stopped
        // guarding #1014 without anyone noticing (AGENTS.md §2: no misleading
        // success paths). The built-ins are always present in core mode, so a
        // failure here is a real problem, not an environment quirk.
        std::cerr << "[FAIL] PluginManager did not initialize; the installed-but-rejecting branch\n"
                     "       of #1014 was never exercised\n";
        return EXIT_FAILURE;
    }
    if (manager.createInstanceById(kEqId) == nullptr) {
        // Same reasoning. This used to return EXIT_SUCCESS with a SKIP message,
        // which is a test that silently stops testing.
        std::cerr << "[FAIL] " << kEqId << " does not resolve here, so the installed-but-rejecting\n"
                     "       branch of #1014 was never exercised\n";
        return EXIT_FAILURE;
    }

    // ---------------------------------------------------------------------
    // The defect: installed, state rejected, project damaged, nothing said so.
    // ---------------------------------------------------------------------
    {
        // Too short to be any plugin's blob. InternalPluginBase rejects a short
        // buffer, so this needs no knowledge of a particular header to be
        // reliably invalid.
        const std::vector<uint8_t> garbage{0x00, 0x01};

        std::vector<SlotRecord> recs(EffectChain::kInitialSlots);
        recs[0] = {true, 11, kEqId, false, 1.0f, garbage};

        EffectChain chain;
        chain.prepare(kSampleRate, kBlockFrames);

        LoadReport report;
        const bool ok = chain.loadState(buildBlob(recs), manager, &report);

        // The chain blob itself was well formed, so loadState's return value
        // must keep meaning "the chain parsed". A plugin's own rejection is not
        // a chain parse failure, and folding it in here would make every caller
        // report a corrupt project because one plugin was unhappy.
        check(ok, "a plugin rejecting its state is not a chain parse failure");

        check(report.unreadableState.size() == 1, "the rejected plugin is reported");
        if (report.unreadableState.size() == 1) {
            check(report.unreadableState[0] == kEqId, "the report names the offending plugin");
        }
        // Distinct facts, so distinct lists. If this id also appeared under
        // missingPlugins, the project-load path would tell the user to install a
        // plugin they already have.
        check(report.missingPlugins.empty(), "an installed plugin is never reported as missing");

        // The slot goes EMPTY, not live-on-defaults. Running the plugin with
        // defaults would sound plausible, so the user would not notice — and
        // the next save would write those defaults over their settings. Leaving
        // the slot empty is the only choice that preserves them.
        const EffectSlot* slot = chain.getSlot(0);
        check(slot != nullptr && slot->isEmpty(), "the slot is empty, not running on defaults");
        check(slot != nullptr && slot->hasMissingPlugin(), "the slot holds a placeholder");
        check(chain.getMissingPluginCount() == 1, "the chain counts the placeholder");

        // The consequence, pinned rather than described: the rejected bytes
        // come back out EXACTLY as they went in. That is the whole difference
        // from the old behaviour, where the next save destroyed them.
        std::vector<SlotRecord> out;
        check(parseBlob(chain.saveState(), out), "the chain still saves and re-parses");
        if (!out.empty() && out[0].present) {
            check(out[0].id == kEqId, "the plugin id survives the save");
            check(out[0].state == garbage, "the rejected state is preserved byte-for-byte");
        }
    }

    // ---------------------------------------------------------------------
    // Control: a valid plugin state reports nothing, so the assertions above
    // are detecting the rejection and not merely the presence of a plugin.
    // ---------------------------------------------------------------------
    {
        auto eq = manager.createInstanceById(kEqId);
        eq->initialize(kSampleRate, kBlockFrames);
        if (eq->getParameterCount() > 0) {
            eq->setParameter(0, 0.42f); // non-default, so the state is not trivially empty
        }
        eq->activate();
        const std::vector<uint8_t> goodState = eq->saveState();

        std::vector<SlotRecord> recs(EffectChain::kInitialSlots);
        recs[0] = {true, 22, kEqId, false, 1.0f, goodState};

        EffectChain chain;
        chain.prepare(kSampleRate, kBlockFrames);

        LoadReport report;
        const bool ok = chain.loadState(buildBlob(recs), manager, &report);

        check(ok, "a chain carrying a valid plugin state loads");
        check(report.unreadableState.empty(), "a valid plugin state reports nothing unreadable");
        check(report.missingPlugins.empty(), "a valid plugin state reports nothing missing");

        // And the state really did apply, which is what a user would notice if
        // the load were broken.
        const EffectSlot* slot = chain.getSlot(0);
        check(slot != nullptr && slot->plugin != nullptr, "the slot holds the live plugin");
        if (slot != nullptr && slot->plugin != nullptr && slot->plugin->getParameterCount() > 0) {
            const float applied = slot->plugin->getParameter(0);
            check(applied > 0.41f && applied < 0.43f, "the saved parameter value was applied");
        }

        // Round-trip fidelity for the healthy path, so the two cases are shown
        // to differ in exactly the way the report describes.
        std::vector<SlotRecord> out;
        check(parseBlob(chain.saveState(), out), "the healthy chain saves and re-parses");
        if (!out.empty() && out[0].present) {
            check(out[0].state == goodState, "a valid plugin state round-trips byte-identically");
        }
    }

    if (g_failures == 0) {
        std::cout << "=== EffectChainUnreadablePluginStateTest: all checks passed ===\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "=== EffectChainUnreadablePluginStateTest: " << g_failures << " failure(s) ===\n";
    return EXIT_FAILURE;
}
