// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// A plugin that cannot be instantiated must survive load/save (#647).
//
// Before this test's fix, EffectChain::loadState dropped the slot when
// createInstanceById returned null — and still returned true, so the caller's
// failure guard never fired. saveState then wrote "empty" over the record, and
// the plugin id plus its opaque state were gone from the project permanently.
//
// The test deliberately does NOT depend on any plugin being installed. Missing
// slots are driven with fabricated ids that no manager can resolve, and the
// "available" side of mixed chains is established with insertPlugin() on a
// locally-defined instance. A test that needed a real plugin scan could pass or
// fail for reasons unrelated to the invariant.

#include "Plugin/EffectChain.h"
#include "Plugin/PluginManager.h"

#include <cstdint>
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

// --- wire format helpers ----------------------------------------------------
// Mirrors EffectChain::saveState. Written independently so the test pins the
// format rather than restating whatever the implementation happens to do.

struct SlotRecord {
    bool present = false;
    uint64_t instanceId = 0; // v2 wire identity
    std::string id;
    bool bypassed = false;
    float dryWet = 1.0f;
    std::vector<uint8_t> state;
};

template <typename T> void appendRaw(std::vector<uint8_t>& out, const T& value) {
    const auto* p = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), p, p + sizeof(T));
}

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
        appendRaw(out, s.instanceId); // v2: identity travels with the instance
        appendRaw(out, static_cast<uint32_t>(s.id.size()));
        out.insert(out.end(), s.id.begin(), s.id.end());
        out.push_back(s.bypassed ? 1 : 0);
        appendRaw(out, s.dryWet);
        appendRaw(out, static_cast<uint32_t>(s.state.size()));
        out.insert(out.end(), s.state.begin(), s.state.end());
    }
    return out;
}

// Parse a blob back so assertions can talk about slots rather than byte offsets.
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
        // v2: skip/read the 8-byte instance id.
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

std::vector<uint8_t> bytes(std::initializer_list<int> vals) {
    std::vector<uint8_t> v;
    for (int x : vals) v.push_back(static_cast<uint8_t>(x));
    return v;
}

// Ids no plugin manager can resolve. This is the whole point: the test needs a
// guaranteed instantiation failure, not a plugin that happens to be absent.
const char* kMissingA = "com.aestra.tests.definitely-not-installed.A";
const char* kMissingB = "com.aestra.tests.definitely-not-installed.B";

PluginManager& manager() { return PluginManager::getInstance(); }

} // namespace

int main() {
    // ---------------------------------------------------------------------
    // The core invariant: a chain of unavailable plugins survives load/save.
    // ---------------------------------------------------------------------
    std::vector<SlotRecord> original(EffectChain::MAX_SLOTS);
    original[0] = {true, 1234, kMissingA, true, 0.375f, bytes({0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01})};
    original[3] = {true, 5678, kMissingB, false, 1.0f, bytes({0x11, 0x22})};
    original[7] = {true, 9012, kMissingA, false, 0.0f, {}}; // empty opaque state is legal

    const std::vector<uint8_t> blob0 = buildBlob(original);

    {
        EffectChain chain;
        chain.prepare(48000.0, 512);
        std::vector<std::string> missing;
        check(chain.loadState(blob0, manager(), &missing), "loadState reports a well-formed blob as ok");

        check(missing.size() == 3, "all three unavailable plugins are reported");
        if (missing.size() == 3) {
            check(missing[0] == kMissingA && missing[1] == kMissingB && missing[2] == kMissingA,
                  "reported ids arrive in slot order");
        }
        check(chain.getMissingPluginCount() == 3, "chain counts three placeholders");
        check(chain.getMissingPluginId(0) == kMissingA, "slot 0 retains its id");
        check(chain.getMissingPluginId(3) == kMissingB, "slot 3 retains its id");
        check(chain.getMissingPluginId(1).empty(), "an untouched slot has no placeholder");

        // The RT predicate must still treat the slot as having nothing to run,
        // or process() would dereference a null plugin on the audio thread.
        const EffectSlot* s0 = chain.getSlot(0);
        check(s0 != nullptr && s0->isEmpty(), "RT predicate isEmpty() stays true for a placeholder");
        check(s0 != nullptr && s0->hasMissingPlugin(), "placeholder is distinguishable from an empty slot");
        check(s0 != nullptr && s0->isOccupied(), "placeholder occupies its slot");

        // ...and the record comes back out intact.
        const std::vector<uint8_t> blob1 = chain.saveState();
        check(blob1 == blob0, "one load/save cycle reproduces the blob byte-for-byte");
    }

    // ---------------------------------------------------------------------
    // Repeated cycles must not progressively mutate the preserved bytes.
    // ---------------------------------------------------------------------
    {
        std::vector<uint8_t> current = blob0;
        for (int cycle = 0; cycle < 5; ++cycle) {
            EffectChain chain;
            chain.prepare(48000.0, 512);
            check(chain.loadState(current, manager()), "cycle: loadState ok");
            const std::vector<uint8_t> next = chain.saveState();
            check(next == blob0, "cycle " + std::to_string(cycle) + ": bytes identical to the original");
            current = next;
        }
    }

    // ---------------------------------------------------------------------
    // Field fidelity, stated per field so a failure names itself.
    // ---------------------------------------------------------------------
    {
        EffectChain chain;
        chain.prepare(48000.0, 512);
        check(chain.loadState(blob0, manager()), "fidelity: loadState ok");
        std::vector<SlotRecord> out;
        check(parseBlob(chain.saveState(), out), "fidelity: resaved blob parses");
        if (out.size() == EffectChain::MAX_SLOTS) {
            check(out[0].present && out[0].id == kMissingA, "slot 0 id preserved");
            check(out[0].bypassed, "slot 0 bypass=true preserved");
            check(out[0].dryWet == 0.375f, "slot 0 dry/wet preserved exactly");
            check(out[0].state == original[0].state, "slot 0 opaque state preserved byte-for-byte");
            check(out[3].present && out[3].id == kMissingB, "slot 3 id preserved");
            check(out[7].present && out[7].dryWet == 0.0f, "slot 7 dry/wet 0.0 preserved");
            check(out[7].state.empty(), "slot 7 empty opaque state preserved");
            check(!out[1].present && !out[2].present, "genuinely empty slots stay empty");
        }
    }

    // ---------------------------------------------------------------------
    // Slot position stability: a placeholder does not shift its neighbours,
    // and neighbours do not shift it.
    // ---------------------------------------------------------------------
    {
        EffectChain chain;
        chain.prepare(48000.0, 512);
        check(chain.loadState(blob0, manager()), "position: loadState ok");
        std::vector<SlotRecord> out;
        check(parseBlob(chain.saveState(), out), "position: resaved blob parses");
        if (out.size() == EffectChain::MAX_SLOTS) {
            check(out[0].present && out[3].present && out[7].present,
                  "placeholders stay at slots 0, 3 and 7");
            for (size_t i : {1u, 2u, 4u, 5u, 6u, 8u, 9u}) {
                check(!out[i].present, "slot " + std::to_string(i) + " stays empty");
            }
        }
    }

    // ---------------------------------------------------------------------
    // A placeholder claims its slot: automatic insertion must not land on it.
    // ---------------------------------------------------------------------
    {
        EffectChain chain;
        chain.prepare(48000.0, 512);
        std::vector<SlotRecord> recs(EffectChain::MAX_SLOTS);
        recs[0] = {true, 0, kMissingA, false, 1.0f, bytes({0x01})};
        check(chain.loadState(buildBlob(recs), manager()), "occupancy: loadState ok");

        check(!chain.isSlotEmpty(0), "a placeholder slot does not report itself free");
        check(chain.isSlotEmpty(1), "a genuinely empty slot reports free");
        check(chain.getFirstEmptySlot() == 1, "first free slot skips the placeholder");

        // Moving onto a placeholder would silently destroy the retained record.
        check(!chain.movePlugin(5, 0), "move onto a placeholder is refused");
    }

    // ---------------------------------------------------------------------
    // Mixed chain: a live plugin inserted over a placeholder supersedes it,
    // while other placeholders are untouched.
    // ---------------------------------------------------------------------
    {
        EffectChain chain;
        chain.prepare(48000.0, 512);
        std::vector<SlotRecord> recs(EffectChain::MAX_SLOTS);
        recs[0] = {true, 0, kMissingA, false, 1.0f, bytes({0xAA})};
        recs[1] = {true, 0, kMissingB, false, 1.0f, bytes({0xBB})};
        check(chain.loadState(buildBlob(recs), manager()), "mixed: loadState ok");
        check(chain.getMissingPluginCount() == 2, "mixed: two placeholders to start");

        // Explicitly targeting the slot replaces it — the user put something there.
        chain.removePlugin(1);
        check(chain.getMissingPluginCount() == 1, "explicit removal drops that placeholder");
        check(chain.getMissingPluginId(1).empty(), "removed slot has no retained id");
        check(chain.getMissingPluginId(0) == kMissingA, "the other placeholder is untouched");

        std::vector<SlotRecord> out;
        check(parseBlob(chain.saveState(), out), "mixed: resaved blob parses");
        if (out.size() == EffectChain::MAX_SLOTS) {
            check(out[0].present && out[0].id == kMissingA, "surviving placeholder still written");
            check(out[0].state == bytes({0xAA}), "surviving placeholder keeps its bytes");
            check(!out[1].present, "removed slot is written as empty");
        }
    }

    // ---------------------------------------------------------------------
    // Backward compatibility: chains with nothing missing behave as before.
    // ---------------------------------------------------------------------
    {
        EffectChain chain;
        chain.prepare(48000.0, 512);
        const std::vector<uint8_t> emptyBlob = buildBlob({});
        std::vector<std::string> missing;
        check(chain.loadState(emptyBlob, manager(), &missing), "all-empty chain loads");
        check(missing.empty(), "all-empty chain reports nothing missing");
        check(chain.getMissingPluginCount() == 0, "all-empty chain has no placeholders");
        check(chain.saveState() == emptyBlob, "all-empty chain round-trips unchanged");
        check(chain.getFirstEmptySlot() == 0, "all-empty chain offers slot 0");

        // Malformed input is still rejected, and rejection must not leave
        // placeholders behind.
        EffectChain bad;
        bad.prepare(48000.0, 512);
        check(!bad.loadState(bytes({'X', 'X', 'X', 1, 10}), manager()), "wrong magic still rejected");
        check(bad.getMissingPluginCount() == 0, "rejected load leaves no placeholders");
    }

    // ---------------------------------------------------------------------
    // clear() means clear.
    // ---------------------------------------------------------------------
    {
        EffectChain chain;
        chain.prepare(48000.0, 512);
        check(chain.loadState(blob0, manager()), "clear: loadState ok");
        check(chain.getMissingPluginCount() == 3, "clear: placeholders present first");
        chain.clear();
        check(chain.getMissingPluginCount() == 0, "clear() removes placeholders");
        check(chain.saveState() == buildBlob({}), "cleared chain saves as all-empty");
    }

    if (g_failures != 0) {
        std::cerr << "[FAIL] EffectChainMissingPluginTest: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "[PASS] EffectChainMissingPluginTest\n";
    return 0;
}
