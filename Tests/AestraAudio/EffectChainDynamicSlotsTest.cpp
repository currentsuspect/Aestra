// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// EffectChainDynamicSlotsTest — V8-S3
//
// The insert chain is no longer a fixed ten. It has ten slots from birth (so
// every existing project is unchanged), grows on demand up to kMaxSlots, and the
// four things the plan says it must not break stay unbroken:
//   - RT-safety: the audio thread reads an immutable snapshot whose size is the
//     chain's size at publication; growing the chain never mutates a snapshot
//     the render thread already holds.
//   - slot-index addressing: indices are stable; growth never renumbers.
//   - automation identity: an instance id resolves to a slot past the tenth.
//   - project format: a chain of ten or fewer slots saves byte-for-byte as it
//     always did (v2, count 10); only a grown chain writes v3, which an older
//     build refuses cleanly.

#include "Plugin/BuiltInPlugins.h"
#include "Plugin/EffectChain.h"
#include "Plugin/PluginManager.h"
#include "Plugin/SamplerPlugin.h"

#include <cstdint>
#include <iostream>
#include <memory>
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

PluginInstancePtr makeSampler() {
    auto plugin = std::make_shared<Plugins::SamplerPlugin>();
    plugin->initialize(48000.0, 512);
    return plugin;
}

} // namespace

int main() {
    auto& manager = PluginManager::getInstance();
    manager.initialize();

    std::cout << "[growth]\n";
    {
        EffectChain chain;
        check(chain.slotCount() == EffectChain::kInitialSlots, "a new chain has the initial ten slots");
        check(EffectChain::kInitialSlots == 10, "the initial count is the fixed ten that shipped");

        check(chain.insertPlugin(EffectChain::kInitialSlots, makeSampler()), "inserting at index 10 succeeds");
        check(chain.slotCount() == 11, "...and grows the chain to eleven");
        check(chain.getPlugin(10) != nullptr, "the plugin is at the index it was given");

        check(chain.insertPlugin(EffectChain::kMaxSlots - 1, makeSampler()), "inserting at the last allowed index succeeds");
        check(chain.slotCount() == EffectChain::kMaxSlots, "...and grows to the ceiling");

        const size_t before = chain.slotCount();
        check(!chain.insertPlugin(EffectChain::kMaxSlots, makeSampler()), "an index past the ceiling is refused");
        check(chain.slotCount() == before, "a refused insert leaves the chain unchanged");
        check(chain.getPlugin(EffectChain::kMaxSlots) == nullptr, "reading past the end is null, not undefined");
    }

    std::cout << "[next free slot]\n";
    {
        EffectChain chain;
        for (size_t i = 0; i < EffectChain::kInitialSlots; ++i) {
            chain.insertPlugin(i, makeSampler());
        }
        check(chain.getFirstEmptySlot() == EffectChain::kInitialSlots,
              "ten occupied slots: the next free slot is the eleventh, which insertPlugin grows into");
        check(chain.insertPlugin(chain.getFirstEmptySlot(), makeSampler()), "...and inserting there works");
        check(chain.getFirstEmptySlot() == 11, "the next free slot follows");

        EffectChain full;
        for (size_t i = 0; i < EffectChain::kMaxSlots; ++i) {
            full.insertPlugin(i, makeSampler());
        }
        check(full.getFirstEmptySlot() == EffectChain::kNoSlot, "a chain full at the ceiling reports no free slot");
    }

    std::cout << "[indices are stable]\n";
    {
        EffectChain chain;
        chain.insertPlugin(2, makeSampler());
        const uint64_t id = chain.getSlotInstanceId(2);
        chain.insertPlugin(14, makeSampler());
        check(chain.getSlotInstanceId(2) == id, "growing the chain never renumbers an existing slot");
        check(chain.findSlotByInstanceId(id) == 2, "an identity still resolves to its original index");
        check(chain.movePlugin(2, 12), "a plugin can be moved into an index past the old end");
        check(chain.findSlotByInstanceId(id) == 12, "the identity travels with it");
        check(chain.getPlugin(2) == nullptr, "the source is vacated");
    }

    std::cout << "[RT snapshot]\n";
    {
        EffectChain chain;
        chain.prepare(48000.0, 512);
        chain.insertPlugin(0, makeSampler());
        auto early = chain.getSnapshot();
        check(early && early->slotCount() == EffectChain::kInitialSlots, "the snapshot starts at ten slots");

        chain.insertPlugin(12, makeSampler());
        const uint64_t id = chain.getSlotInstanceId(12);
        auto later = chain.getSnapshot();
        check(later && later->slotCount() == 13, "growth publishes a snapshot sized to the new chain");
        check(early->slotCount() == EffectChain::kInitialSlots,
              "the snapshot the render thread already holds is not mutated by growth");
        check(later->findSlotByInstanceId(id) == 12, "automation identity resolves to a slot past the tenth");
        check(later->slot(12).plugin != nullptr && later->slot(11).isEmpty(), "slot contents are where they were put");
        check(later->getActiveSlotCount() == 2, "the active count sees both plugins");
        check(early->findSlotByInstanceId(id) == EffectChain::kNoSlot, "the older snapshot knows nothing of the new instance");
    }

    std::cout << "[project format]\n";
    {
        // A chain that never grew: exactly the bytes it always wrote.
        EffectChain plain;
        plain.insertPlugin(0, makeSampler());
        const auto plainState = plain.saveState();
        check(plainState[3] == EffectChain::kStateFormatVersion && plainState[3] == 2, "a ten-slot chain is still saved as v2");
        check(plainState[4] == EffectChain::kInitialSlots, "...with a slot count of ten");

        // Grown then emptied: trailing empty slots are not carried into the file.
        EffectChain shrunk;
        shrunk.insertPlugin(0, makeSampler());
        shrunk.insertPlugin(15, makeSampler());
        shrunk.removePlugin(15);
        const auto shrunkState = shrunk.saveState();
        check(shrunkState[3] == 2 && shrunkState[4] == EffectChain::kInitialSlots,
              "a chain that grew and was emptied again saves exactly like one that never grew");
        check(shrunkState.size() == plainState.size(), "...byte for byte the same size");

        // Grown and used: v3 with the count it needs, round-trips with identity.
        EffectChain big;
        big.insertPlugin(0, makeSampler());
        big.insertPlugin(12, makeSampler());
        const uint64_t bigId = big.getSlotInstanceId(12);
        const auto bigState = big.saveState();
        check(bigState[3] == EffectChain::kStateFormatVersionLong, "a chain past ten is saved as v3");
        check(bigState[4] == 13, "...with just the slots it needs (up to the last occupied)");

        EffectChain loaded;
        check(loaded.loadState(bigState, manager), "a v3 chain loads");
        check(loaded.slotCount() == 13, "the loaded chain has its saved size");
        check(loaded.getPlugin(0) != nullptr && loaded.getPlugin(12) != nullptr, "both plugins are back at their slots");
        check(loaded.getSlotInstanceId(12) == bigId, "the identity survives the round trip");

        // Loading a smaller state into a bigger chain yields exactly what was saved.
        check(loaded.loadState(plainState, manager), "a v2 chain loads into a chain that had grown");
        check(loaded.slotCount() == EffectChain::kInitialSlots, "the chain is trimmed back to ten");
        check(loaded.getPlugin(12) == nullptr, "nothing from the larger chain is left behind");
        check(loaded.getActiveSlotCount() == 1, "only what was saved is present");

        // Counts that were never valid stay invalid.
        auto bad = bigState;
        bad[4] = static_cast<uint8_t>(EffectChain::kMaxSlots + 1);
        EffectChain probe;
        check(!probe.loadState(bad, manager), "a v3 count past the ceiling is refused");
        bad = bigState;
        bad[4] = 5;
        check(!probe.loadState(bad, manager), "a v3 count below the initial ten is refused");
        auto v2odd = plainState;
        v2odd[4] = 11;
        check(!probe.loadState(v2odd, manager), "a v2 payload with a count other than ten is still refused, as before");
        auto future = bigState;
        future[3] = static_cast<uint8_t>(EffectChain::kStateFormatVersionLong + 1);
        check(!probe.loadState(future, manager), "an unknown newer version is refused cleanly");
    }

    std::cout << (gFailures == 0 ? "[PASS] EffectChainDynamicSlotsTest\n" : "[FAIL] EffectChainDynamicSlotsTest\n");
    return gFailures == 0 ? 0 : 1;
}
