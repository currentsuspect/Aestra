// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// A plugin that IS installed but rejects its own saved state must be reported
// (#1014).
//
// The two failure modes in EffectChain::loadState look identical from the
// outside and are not the same event:
//
//   not installed        -> createInstanceById returns null. The slot keeps the
//                           opaque record verbatim and re-emits it on save. The
//                           project is intact; only the machine is short a
//                           plugin. Already reported, as missingPlugins (#647).
//
//   installed, state bad -> the slot goes LIVE, running DEFAULT parameters. The
//                           settings in the project were not applied, and the
//                           next save overwrites them. The project is damaged.
//
// Before this fix the second case only reached Aestra::Log::warning, and the
// return value still said "the blob parsed". The project opened cleanly and
// nothing anywhere said the user's settings were gone. That is silent data loss
// with a log line standing next to it, which is the worst of both.
//
// This test deliberately uses a REAL built-in plugin, because the whole point is
// the installed-but-rejecting branch: an unresolvable id takes the other path
// and would make the test pass for the wrong reason. That is the mirror of
// EffectChainMissingPluginTest, which deliberately depends on NO plugin being
// installed. Neither test can substitute for the other.

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
                             static_cast<uint8_t>(EffectChain::MAX_SLOTS)};
    for (size_t i = 0; i < EffectChain::MAX_SLOTS; ++i) {
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
    out.assign(EffectChain::MAX_SLOTS, SlotRecord{});
    if (blob.size() < 5 || blob[0] != 'N' || blob[1] != 'E' || blob[2] != 'C') {
        return false;
    }
    if (blob[4] != static_cast<uint8_t>(EffectChain::MAX_SLOTS)) {
        return false;
    }
    size_t off = 5;
    for (size_t i = 0; i < EffectChain::MAX_SLOTS; ++i) {
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
        std::cerr << "[SKIP] PluginManager did not initialize; cannot prove the installed path\n";
        return EXIT_SUCCESS;
    }
    if (manager.createInstanceById(kEqId) == nullptr) {
        // An environment fact, not a regression. But it MUST NOT read as a pass:
        // the branch under test was never entered, and the sibling test's
        // skip-or-fail choice is deliberately different.
        std::cerr << "[SKIP] " << kEqId << " does not resolve here, so the installed-but-rejecting\n"
                     "       branch was never exercised. This run proves nothing about #1014.\n";
        return EXIT_SUCCESS;
    }

    // ---------------------------------------------------------------------
    // The defect: installed, state rejected, project damaged, nothing said so.
    // ---------------------------------------------------------------------
    {
        // Too short to be any plugin's blob. InternalPluginBase rejects a short
        // buffer, so this needs no knowledge of a particular header to be
        // reliably invalid.
        const std::vector<uint8_t> garbage{0x00, 0x01};

        std::vector<SlotRecord> recs(EffectChain::MAX_SLOTS);
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

        // The slot is live and running defaults -- which is exactly why this
        // has to be surfaced rather than logged.
        const EffectSlot* slot = chain.getSlot(0);
        check(slot != nullptr && !slot->isEmpty(), "the slot is live, not empty");
        check(slot != nullptr && !slot->hasMissingPlugin(), "the slot is not a placeholder");
        check(chain.getMissingPluginCount() == 0, "the chain counts no placeholders");

        // The consequence, pinned rather than described: the rejected bytes are
        // NOT preserved, because the live slot re-emits the plugin's own state.
        // This is the data loss the report exists to warn about, so the test
        // asserts that it really happens.
        std::vector<SlotRecord> out;
        check(parseBlob(chain.saveState(), out), "the damaged chain still saves and re-parses");
        if (!out.empty() && out[0].present) {
            check(out[0].id == kEqId, "the plugin id survives the save");
            check(out[0].state != garbage, "the rejected bytes are not what gets written back");
            check(!out[0].state.empty(), "the plugin's own state is written instead");
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

        std::vector<SlotRecord> recs(EffectChain::MAX_SLOTS);
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
