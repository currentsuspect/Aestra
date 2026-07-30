// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Plugin-instance identity must survive chain reordering (#667).
//
// The defect this exists to prevent: automation addressed a plugin parameter by
// {effectSlot, paramId}, where effectSlot is a POSITION. EffectChain::movePlugin
// and swapPlugins relocate slot contents, and automation curves live on the lane
// rather than on the chain, so nothing updated them. Reordering an effect chain
// silently re-pointed every plugin-parameter curve on that lane at whatever now
// occupied the slot — no parse failure, no warning, no crash, and destructive on
// the next save because the wrong reading became canonical.
//
// The invariant these tests pin:
//
//   Automation addresses the plugin INSTANCE it was drawn for, not the position
//   that instance happened to occupy. Reordering a chain is a rearrangement,
//   never a retarget.
//
// This slice establishes the identity and its behaviour under every in-memory
// slot mutation. Making automation resolve through it, and persisting it across
// save/load, follow — the id is deliberately not yet read by the engine, so
// these tests are about the identity's own contract, not about audio.
//
// Scope note: the tests use a locally defined IPluginInstance so they never
// depend on a plugin being installed or on the scan cache. A test that needed a
// real plugin could pass or fail for reasons unrelated to the invariant.

#include "Plugin/EffectChain.h"

#include "Commands/EffectCommands.h"
#include "Commands/PluginCommands.h"
#include "Core/MixerChannel.h"
#include "Plugin/PluginManager.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <set>
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

/// Minimal effect instance. Only identity matters here, so process() is a copy
/// and everything else is the smallest legal implementation.
class IdentityTestPlugin final : public IPluginInstance {
public:
    explicit IdentityTestPlugin(const std::string& id) {
        m_info.id = id;
        m_info.name = id;
        m_info.vendor = "Aestra Tests";
        m_info.version = "1";
        m_info.category = "Test";
        m_info.format = PluginFormat::Internal;
        m_info.type = PluginType::Effect;
        m_info.numAudioInputs = 2;
        m_info.numAudioOutputs = 2;
    }

    bool initialize(double, uint32_t) override { return true; }
    void shutdown() override {}
    void activate() override { m_active = true; }
    void deactivate() override { m_active = false; }
    bool isActive() const override { return m_active; }

    void process(const float* const* inputs, float** outputs, uint32_t numInputChannels,
                 uint32_t numOutputChannels, uint32_t numFrames, const MidiBuffer* = nullptr,
                 MidiBuffer* = nullptr) override {
        if (inputs == nullptr || outputs == nullptr) {
            return;
        }
        const uint32_t channels = numInputChannels < numOutputChannels ? numInputChannels
                                                                       : numOutputChannels;
        for (uint32_t ch = 0; ch < channels; ++ch) {
            for (uint32_t i = 0; i < numFrames; ++i) {
                outputs[ch][i] = inputs[ch][i];
            }
        }
    }

    std::vector<PluginParameter> getParameters() const override { return {}; }
    uint32_t getParameterCount() const override { return 0; }
    float getParameter(uint32_t) const override { return 0.0f; }
    void setParameter(uint32_t, float) override {}
    std::string getParameterDisplay(uint32_t) const override { return {}; }
    std::vector<uint8_t> saveState() const override { return {}; }
    bool loadState(const std::vector<uint8_t>&) override { return true; }
    bool hasEditor() const override { return false; }
    bool openEditor(void*) override { return false; }
    void closeEditor() override {}
    bool isEditorOpen() const override { return false; }
    std::pair<int, int> getEditorSize() const override { return {0, 0}; }
    bool resizeEditor(int, int) override { return false; }
    const PluginInfo& getInfo() const override { return m_info; }
    uint32_t getLatencySamples() const override { return 0; }
    uint32_t getTailSamples() const override { return 0; }
    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

private:
    bool m_active{false};
    PluginInfo m_info{};
};

PluginInstancePtr makePlugin(const std::string& id) {
    return std::make_shared<IdentityTestPlugin>(id);
}

// --- the identity's own contract --------------------------------------------

void testInsertMintsDistinctNonZeroIds() {
    EffectChain chain;
    chain.insertPlugin(0, makePlugin("a"));
    chain.insertPlugin(1, makePlugin("b"));
    chain.insertPlugin(2, makePlugin("c"));

    const uint64_t a = chain.getSlotInstanceId(0);
    const uint64_t b = chain.getSlotInstanceId(1);
    const uint64_t c = chain.getSlotInstanceId(2);

    check(a != 0 && b != 0 && c != 0, "every occupied slot has a non-zero identity");
    std::set<uint64_t> unique{a, b, c};
    check(unique.size() == 3, "identities are distinct across slots");
}

void testUnoccupiedSlotsHaveNoIdentity() {
    // Zero is the reserved "no instance" value. If an empty slot carried a real
    // id, an un-addressed curve would resolve to it.
    EffectChain chain;
    chain.insertPlugin(0, makePlugin("a"));

    check(chain.getSlotInstanceId(1) == 0, "an empty slot has no identity");
    check(chain.getSlotInstanceId(EffectChain::MAX_SLOTS) == 0,
          "an out-of-range slot reports no identity rather than reading past the array");
}

void testMoveCarriesIdentityToTheNewPosition() {
    // The core case. This is what silently retargeted automation.
    EffectChain chain;
    chain.insertPlugin(0, makePlugin("keeper"));
    const uint64_t original = chain.getSlotInstanceId(0);

    check(chain.movePlugin(0, 4), "the move succeeds into a free slot");

    check(chain.getSlotInstanceId(4) == original,
          "the identity travels with the instance to its new position");
    check(chain.getSlotInstanceId(0) == 0, "the vacated slot keeps no identity behind");
    check(chain.findSlotByInstanceId(original) == 4,
          "the instance is found at its new position by identity");
}

void testSwapExchangesIdentitiesWithInstances() {
    EffectChain chain;
    chain.insertPlugin(0, makePlugin("first"));
    chain.insertPlugin(1, makePlugin("second"));
    const uint64_t first = chain.getSlotInstanceId(0);
    const uint64_t second = chain.getSlotInstanceId(1);

    check(chain.swapPlugins(0, 1), "the swap succeeds");

    check(chain.getSlotInstanceId(0) == second && chain.getSlotInstanceId(1) == first,
          "identities exchange along with their instances");
    check(chain.findSlotByInstanceId(first) == 1 && chain.findSlotByInstanceId(second) == 0,
          "each instance is still found by its own identity after the swap");
}

void testReorderDoesNotRetarget() {
    // The defect stated positively, at the level a curve would experience it.
    // Resolve an address before the reorder and after, and require it to name the
    // same plugin both times — which the positional scheme could not do.
    EffectChain chain;
    chain.insertPlugin(0, makePlugin("reverb"));
    chain.insertPlugin(1, makePlugin("compressor"));

    const uint64_t reverbId = chain.getSlotInstanceId(0);
    const std::string before = chain.getPlugin(chain.findSlotByInstanceId(reverbId))->getInfo().id;

    check(chain.swapPlugins(0, 1), "reorder the chain");

    const size_t nowAt = chain.findSlotByInstanceId(reverbId);
    check(nowAt != EffectChain::MAX_SLOTS, "the addressed instance is still resolvable");
    const std::string after = chain.getPlugin(nowAt)->getInfo().id;

    check(before == after && after == "reverb",
          "an address resolved before and after a reorder names the same plugin");
    check(nowAt == 1, "and it moved position, so the test is not passing by nothing happening");
}

void testRemoveRetiresTheIdentity() {
    // A removed instance's id must resolve to nothing, not to whatever occupies
    // the index next. Resolving to a successor is the exact defect.
    EffectChain chain;
    chain.insertPlugin(0, makePlugin("doomed"));
    const uint64_t doomed = chain.getSlotInstanceId(0);

    chain.removePlugin(0);
    check(chain.getSlotInstanceId(0) == 0, "the emptied slot has no identity");
    check(chain.findSlotByInstanceId(doomed) == EffectChain::MAX_SLOTS,
          "a removed instance's identity resolves to nothing");

    chain.insertPlugin(0, makePlugin("successor"));
    check(chain.findSlotByInstanceId(doomed) == EffectChain::MAX_SLOTS,
          "the successor in the same slot does NOT inherit the removed identity");
    check(chain.getSlotInstanceId(0) != doomed,
          "the successor received a fresh identity of its own");
}

void testReplacingInSlotMintsFresh() {
    // insertPlugin over an occupied slot is a replacement, so it is a different
    // instance and must not inherit the previous plugin's automation.
    EffectChain chain;
    chain.insertPlugin(0, makePlugin("old"));
    const uint64_t oldId = chain.getSlotInstanceId(0);

    chain.insertPlugin(0, makePlugin("new"));
    const uint64_t newId = chain.getSlotInstanceId(0);

    check(newId != 0, "the replacement has an identity");
    check(newId != oldId, "the replacement does not inherit the replaced instance's identity");
    check(chain.findSlotByInstanceId(oldId) == EffectChain::MAX_SLOTS,
          "the replaced identity no longer resolves anywhere");
}

void testClearRetiresEveryIdentity() {
    EffectChain chain;
    chain.insertPlugin(0, makePlugin("a"));
    chain.insertPlugin(1, makePlugin("b"));
    const uint64_t a = chain.getSlotInstanceId(0);
    const uint64_t b = chain.getSlotInstanceId(1);

    chain.clear();

    check(chain.findSlotByInstanceId(a) == EffectChain::MAX_SLOTS &&
              chain.findSlotByInstanceId(b) == EffectChain::MAX_SLOTS,
          "clear() retires every identity in the chain");
}

void testLookupRejectsTheReservedZero() {
    // Every unoccupied slot stores 0. A lookup that matched it would hand the
    // first empty slot to anything not addressing a real instance.
    EffectChain chain;
    chain.insertPlugin(3, makePlugin("only"));

    check(chain.findSlotByInstanceId(0) == EffectChain::MAX_SLOTS,
          "id 0 never resolves, even though empty slots store 0");
}

void testUnknownIdentityDoesNotResolve() {
    EffectChain chain;
    chain.insertPlugin(0, makePlugin("a"));
    const uint64_t real = chain.getSlotInstanceId(0);

    check(chain.findSlotByInstanceId(real + 99999) == EffectChain::MAX_SLOTS,
          "an identity this chain never issued does not resolve");
}

void testIdentitiesAreUniqueAcrossChains() {
    // Ids are minted process-wide, not per chain: a plugin dragged between
    // channel strips keeps its identity, and per-chain counters would collide.
    EffectChain first;
    EffectChain second;
    first.insertPlugin(0, makePlugin("a"));
    second.insertPlugin(0, makePlugin("b"));

    check(first.getSlotInstanceId(0) != second.getSlotInstanceId(0),
          "two chains never mint the same identity for different instances");
    check(second.findSlotByInstanceId(first.getSlotInstanceId(0)) == EffectChain::MAX_SLOTS,
          "one chain's identity does not resolve inside another");
}

void testReserveMintedIdPreventsCollision() {
    // The re-mint guard (#528's pattern). Ids restored from a project were minted
    // in a previous run; without reserving them, this run would hand the same
    // value out again and two slots would share an identity.
    EffectChain chain;
    chain.insertPlugin(0, makePlugin("a"));
    const uint64_t restored = chain.getSlotInstanceId(0) + 5000;

    reserveMintedPluginInstanceId(restored);

    chain.insertPlugin(1, makePlugin("b"));
    check(chain.getSlotInstanceId(1) > restored,
          "ids minted after reserving a restored value exceed it");
}

void testMintNeverReturnsZero() {
    for (int i = 0; i < 1000; ++i) {
        if (mintPluginInstanceId() == 0) {
            check(false, "mintPluginInstanceId must never return the reserved 0");
            return;
        }
    }
}

void testResetPreservesIdentities() {
    // reset() reboots plugins to flush their internal buffers; it does not remove
    // them. The instances are the same, so their identities must be untouched —
    // otherwise a panic/reset would orphan every curve in the project.
    EffectChain chain;
    chain.insertPlugin(0, makePlugin("a"));
    chain.insertPlugin(1, makePlugin("b"));
    const uint64_t a = chain.getSlotInstanceId(0);
    const uint64_t b = chain.getSlotInstanceId(1);

    chain.reset();

    check(chain.getSlotInstanceId(0) == a && chain.getSlotInstanceId(1) == b,
          "reset() reboots plugins without changing their identities");
}

void testSnapshotCarriesIdentity() {
    // The render thread resolves against the snapshot, never the mutable chain,
    // so the identity has to cross that boundary or it is unusable for audio.
    EffectChain chain;
    chain.insertPlugin(0, makePlugin("a"));
    chain.insertPlugin(1, makePlugin("b"));
    const uint64_t b = chain.getSlotInstanceId(1);

    auto snapshot = chain.getSnapshot();
    check(snapshot != nullptr, "a snapshot is published after mutation");
    if (snapshot == nullptr) {
        return;
    }

    check(snapshot->slot(1).instanceId == b, "the snapshot copies each slot's identity");
    check(snapshot->findSlotByInstanceId(b) == 1, "the snapshot resolves by identity");
    check(snapshot->findSlotByInstanceId(0) == EffectChainSnapshot::MAX_SLOTS,
          "the snapshot also refuses the reserved 0");
}

// ---------------------------------------------------------------------------
// Restoration: load, and the undo/redo round trips (CodeRabbit review, #681)
// ---------------------------------------------------------------------------
//
// Minting on insert is only half the invariant. The other half is that every
// path which puts an occupant into a slot leaves a coherent identity behind —
// including the paths that RESTORE an occupant rather than introduce one.
//
// The load tests deliberately serialize plugins whose ids are not registered
// with PluginManager, so loadState takes the missing-plugin branch (#647). That
// keeps them independent of the plugin registry and scan cache while still
// exercising real restoration: a placeholder is an occupant and must carry an
// identity like any other.

/// Build a serialized chain: slot 0 occupied, everything else empty.
std::vector<uint8_t> serializeChainWithOccupantInSlotZero() {
    EffectChain chain;
    chain.insertPlugin(0, makePlugin("aestra.test.identity.unregistered"));
    return chain.saveState();
}

void testLoadMintsIdentityForRestoredSlots() {
    // Before this, loadState never touched instanceId, so every plugin in a
    // freshly opened project came back with identity 0 — i.e. unaddressable, and
    // indistinguishable from an empty slot to findSlotByInstanceId.
    EffectChain source;
    source.insertPlugin(0, makePlugin("aestra.test.identity.unregistered.a"));
    source.insertPlugin(3, makePlugin("aestra.test.identity.unregistered.b"));
    const std::vector<uint8_t> blob = source.saveState();

    EffectChain restored;
    std::vector<std::string> missing;
    check(restored.loadState(blob, PluginManager::getInstance(), &missing),
          "the serialized chain loads");

    const uint64_t a = restored.getSlotInstanceId(0);
    const uint64_t b = restored.getSlotInstanceId(3);
    check(a != 0, "a restored occupant has an identity");
    check(b != 0, "every restored occupant has one, not just the first");
    check(a != b, "restored occupants get distinct identities");
    check(restored.findSlotByInstanceId(a) == 0, "a restored identity resolves to its slot");
    check(restored.findSlotByInstanceId(b) == 3, "and so does the second");
}

void testLoadClearsIdentityOnSerializedEmptySlots() {
    // The inverse, and the more dangerous half: loading into a chain that was
    // already populated. A slot the blob says is empty must not keep the
    // previous occupant's id, or a lookup for that id resolves to a slot holding
    // nothing — a dangling identity rather than a missing one.
    const std::vector<uint8_t> blob = serializeChainWithOccupantInSlotZero();

    EffectChain reused;
    reused.insertPlugin(0, makePlugin("occupant.0"));
    reused.insertPlugin(2, makePlugin("occupant.2"));
    const uint64_t staleId = reused.getSlotInstanceId(2);
    check(staleId != 0, "the slot about to be emptied really had an identity");

    std::vector<std::string> missing;
    check(reused.loadState(blob, PluginManager::getInstance(), &missing),
          "the blob loads over the populated chain");

    check(reused.getSlotInstanceId(2) == 0,
          "a slot the blob says is empty must not keep its old identity");
    check(reused.findSlotByInstanceId(staleId) == EffectChain::MAX_SLOTS,
          "the retired identity must not resolve to the now-empty slot");
    check(reused.getSlotInstanceId(0) != staleId,
          "nor may it drift onto the slot the blob did fill");
}

void testUndoOfRemoveRestoresTheSameIdentity() {
    // The path that actually ships. MixerViewModel::removeInsert pushes a
    // RemovePluginCommand, and its undo re-inserts the very same instance. Before
    // the preserved-id parameter, insertPlugin minted there, so Ctrl+Z after a
    // remove returned a plugin the user saw as unchanged but which every curve
    // addressed to it no longer matched.
    MixerChannel channel("Identity", 1);
    auto& chain = channel.getEffectChain();
    chain.insertPlugin(1, makePlugin("undo.subject"));
    const uint64_t original = chain.getSlotInstanceId(1);
    check(original != 0, "the plugin has an identity before removal");

    RemovePluginCommand command(channel, 1);
    command.execute();
    check(chain.getSlotInstanceId(1) == 0, "removal retires the identity");

    command.undo();
    check(chain.getSlotInstanceId(1) == original,
          "undoing a removal restores the identity, it does not mint a new one");
    check(chain.findSlotByInstanceId(original) == 1,
          "and the restored identity resolves to the slot again");
}

void testRedoOfAddKeepsTheOriginalIdentity() {
    // Same defect through AddPluginCommand: undo then redo has to be a round
    // trip. If redo minted, automation drawn before the undo would be orphaned by
    // a redo the user reads as "put it back exactly as it was".
    MixerChannel channel("Identity", 2);
    auto& chain = channel.getEffectChain();

    AddPluginCommand command(channel, 0, makePlugin("redo.subject"));
    command.execute();
    const uint64_t original = chain.getSlotInstanceId(0);
    check(original != 0, "the added plugin has an identity");

    command.undo();
    check(chain.getSlotInstanceId(0) == 0, "undoing the add empties the slot");

    command.redo();
    check(chain.getSlotInstanceId(0) == original,
          "redoing an add restores the original identity");
}

void testUndoOfEffectRemoveRestoresTheSameIdentity() {
    // EffectCommands.h carries a second, independent add/remove pair with the
    // same shape as PluginCommands.h — different constructor, same defect. Both
    // are wired into history, so covering only one would leave the identity
    // guarantee true through one undo path and false through the other.
    TrackManager trackManager;
    MixerChannel channel("Identity", 3);
    auto& chain = channel.getEffectChain();
    chain.insertPlugin(2, makePlugin("effect.undo.subject"));
    const uint64_t original = chain.getSlotInstanceId(2);

    RemoveEffectCommand command(trackManager, channel, 2);
    command.execute();
    command.undo();

    check(chain.getSlotInstanceId(2) == original,
          "undoing RemoveEffectCommand restores the identity too");
}

void testRedoOfEffectAddKeepsTheOriginalIdentity() {
    TrackManager trackManager;
    MixerChannel channel("Identity", 4);
    auto& chain = channel.getEffectChain();

    AddEffectCommand command(trackManager, channel, 0, makePlugin("effect.redo.subject"), "Reverb");
    command.execute();
    const uint64_t original = chain.getSlotInstanceId(0);
    command.undo();
    command.redo();

    check(chain.getSlotInstanceId(0) == original,
          "redoing AddEffectCommand restores the original identity too");
}

void testPreservedIdentityIsReservedAgainstFutureMints() {
    // A preserved id can come from outside this process once v2 persists it. If
    // restoring one did not advance the mint counter, the very next insert could
    // hand out the same value and two slots would share an identity — the exact
    // ambiguity the id exists to remove.
    EffectChain chain;
    const uint64_t farFuture = 9'000'000'000ULL;
    chain.insertPlugin(0, makePlugin("restored"), farFuture);
    check(chain.getSlotInstanceId(0) == farFuture, "the preserved identity is taken as given");

    chain.insertPlugin(1, makePlugin("fresh"));
    check(chain.getSlotInstanceId(1) > farFuture,
          "a later mint must clear a restored identity, not collide with it");
}

void testPreservedZeroStillMints() {
    // 0 is the "no identity" sentinel, so passing it must mean "mint one" rather
    // than "install 0 as the identity" — otherwise every existing caller, which
    // relies on the default, would insert unaddressable plugins.
    EffectChain chain;
    chain.insertPlugin(0, makePlugin("defaulted"), 0);
    check(chain.getSlotInstanceId(0) != 0,
          "passing the reserved 0 mints a real identity instead of storing 0");
}

}  // namespace

int main() {
    testInsertMintsDistinctNonZeroIds();
    testUnoccupiedSlotsHaveNoIdentity();
    testMoveCarriesIdentityToTheNewPosition();
    testSwapExchangesIdentitiesWithInstances();
    testReorderDoesNotRetarget();
    testRemoveRetiresTheIdentity();
    testReplacingInSlotMintsFresh();
    testClearRetiresEveryIdentity();
    testLookupRejectsTheReservedZero();
    testUnknownIdentityDoesNotResolve();
    testIdentitiesAreUniqueAcrossChains();
    testReserveMintedIdPreventsCollision();
    testMintNeverReturnsZero();
    testResetPreservesIdentities();
    testSnapshotCarriesIdentity();
    testLoadMintsIdentityForRestoredSlots();
    testLoadClearsIdentityOnSerializedEmptySlots();
    testUndoOfRemoveRestoresTheSameIdentity();
    testRedoOfAddKeepsTheOriginalIdentity();
    testUndoOfEffectRemoveRestoresTheSameIdentity();
    testRedoOfEffectAddKeepsTheOriginalIdentity();
    testPreservedIdentityIsReservedAgainstFutureMints();
    testPreservedZeroStillMints();

    if (g_failures != 0) {
        std::cerr << "[FAIL] EffectChainInstanceIdentityTest: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "[PASS] EffectChainInstanceIdentityTest\n";
    return 0;
}
