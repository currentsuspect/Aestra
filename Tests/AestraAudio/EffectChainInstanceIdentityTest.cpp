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
#include <limits>
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

// --- v2 serialization (Automation Identity Contract I4/I5/I6/I9) ------------

/// Build a raw chain-state blob by hand so tests can pin version-specific
/// behavior (v1 = no ids, v2 = ids) without depending on saveState's current
/// version. Empty slots are hasPlugin=0; occupied slots carry pluginId +
/// bypass + dryWet + empty plugin state.
std::vector<uint8_t> buildChainBlob(uint8_t version,
                                    const std::vector<std::pair<uint64_t, std::string>>& occupiedSlots) {
    std::vector<uint8_t> state{'N', 'E', 'C', version, static_cast<uint8_t>(EffectChain::MAX_SLOTS)};
    const auto put = [&state](const void* data, size_t n) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        state.insert(state.end(), bytes, bytes + n);
    };
    size_t cursor = 0;
    for (size_t slot = 0; slot < EffectChain::MAX_SLOTS; ++slot) {
        if (cursor < occupiedSlots.size() && occupiedSlots[cursor].first == 0 && false) {
            // unreachable; kept for clarity of the loop shape
        }
        // occupiedSlots entries are indexed by slot position
        if (slot < occupiedSlots.size()) {
            const auto& [instanceId, pluginId] = occupiedSlots[slot];
            state.push_back(1);
            if (version >= 2) {
                put(&instanceId, sizeof(instanceId));
            }
            const uint32_t idLen = static_cast<uint32_t>(pluginId.size());
            put(&idLen, sizeof(idLen));
            state.insert(state.end(), pluginId.begin(), pluginId.end());
            const uint8_t bypass = 0;
            state.push_back(bypass);
            const float dryWet = 1.0f;
            put(&dryWet, sizeof(dryWet));
            const uint32_t stateLen = 0;
            put(&stateLen, sizeof(stateLen));
        } else {
            state.push_back(0);
        }
    }
    return state;
}

void testSaveLoadRoundTripPreservesIdentity() {
    // v2: the identity travels with the instance (Contract I4/I9). A save/load
    // cycle must restore the exact ids — the serialization gap D2 taught us to
    // look for. (v1 minted; v2 must not.)
    EffectChain source;
    source.insertPlugin(0, makePlugin("aestra.test.identity.unregistered.a"));
    source.insertPlugin(3, makePlugin("aestra.test.identity.unregistered.b"));
    const uint64_t idA = source.getSlotInstanceId(0);
    const uint64_t idB = source.getSlotInstanceId(3);
    check(source.saveState()[3] == EffectChain::kStateFormatVersion,
          "saved state carries the current format version (v2)");
    const std::vector<uint8_t> blob = source.saveState();

    EffectChain restored;
    std::vector<std::string> missing;
    check(restored.loadState(blob, PluginManager::getInstance(), &missing),
          "the v2 chain loads");
    check(restored.getSlotInstanceId(0) == idA,
          "round trip preserves the first instance identity exactly");
    check(restored.getSlotInstanceId(3) == idB,
          "round trip preserves the second instance identity exactly");
    check(restored.findSlotByInstanceId(idA) == 0 && restored.findSlotByInstanceId(idB) == 3,
          "restored identities resolve to their slots");
}

void testV1StateStillLoadsAndMints() {
    // v1 payloads predate identity: they load and mint (migration rule I5).
    const std::vector<uint8_t> blob = buildChainBlob(
        1, {{0, "aestra.test.identity.unregistered.a"}, {0, "aestra.test.identity.unregistered.b"}});
    EffectChain restored;
    std::vector<std::string> missing;
    check(restored.loadState(blob, PluginManager::getInstance(), &missing),
          "a v1 chain still loads");
    const uint64_t a = restored.getSlotInstanceId(0);
    const uint64_t b = restored.getSlotInstanceId(1);
    check(a != 0 && b != 0, "v1 occupants mint identities");
    check(a != b, "v1 occupants mint distinct identities");
}

void testV2MissingIdMintsFresh() {
    // A v2 payload with id 0 on an occupied slot is corrupt: mint with a
    // diagnostic rather than leaving the slot unaddressable (Contract I5).
    const std::vector<uint8_t> blob =
        buildChainBlob(2, {{0, "aestra.test.identity.unregistered.a"}});
    EffectChain restored;
    std::vector<std::string> missing;
    check(restored.loadState(blob, PluginManager::getInstance(), &missing),
          "a v2 chain with a missing id still loads");
    check(restored.getSlotInstanceId(0) != 0, "the corrupt slot mints a fresh identity");
}

void testV2DuplicateIdMintsFresh() {
    // Two slots sharing one id is corrupt: the duplicate mints so lookups can
    // never be ambiguous (Contract I5).
    const std::vector<uint8_t> blob = buildChainBlob(
        2, {{42, "aestra.test.identity.unregistered.a"}, {42, "aestra.test.identity.unregistered.b"}});
    EffectChain restored;
    std::vector<std::string> missing;
    check(restored.loadState(blob, PluginManager::getInstance(), &missing),
          "a v2 chain with a duplicate id still loads");
    check(restored.getSlotInstanceId(0) != restored.getSlotInstanceId(1),
          "the duplicate slot mints a distinct identity");
    check(restored.getSlotInstanceId(0) == 42, "the first occupant keeps its id");
}

void testPlaceholderIdentitySurvivesRoundTrip() {
    // A placeholder is an occupant with an identity (Contract I6). Saving a
    // chain with a missing-plugin placeholder and reloading must restore the
    // SAME id, so automation addressed to the failed plugin survives.
    const std::vector<uint8_t> blob = buildChainBlob(
        2, {{77, "aestra.test.identity.does.not.exist"}});
    EffectChain first;
    std::vector<std::string> missing;
    check(first.loadState(blob, PluginManager::getInstance(), &missing),
          "the placeholder chain loads");
    check(first.getSlotInstanceId(0) == 77, "the placeholder restored its wire id");

    const std::vector<uint8_t> resaved = first.saveState();
    EffectChain second;
    std::vector<std::string> missing2;
    check(second.loadState(resaved, PluginManager::getInstance(), &missing2),
          "the resaved placeholder chain loads");
    check(second.getSlotInstanceId(0) == 77,
          "placeholder identity survives a full save/load cycle");
}

void testLoadedIdsAreReservedAgainstFutureMints() {
    // Ids restored from a v2 payload must be reserved so a mint later in the
    // session can never collide with them (I5 + the #528 guard pattern).
    const std::vector<uint8_t> blob = buildChainBlob(
        2, {{9000, "aestra.test.identity.unregistered.a"}});
    EffectChain restored;
    std::vector<std::string> missing;
    check(restored.loadState(blob, PluginManager::getInstance(), &missing),
          "the v2 chain loads");

    restored.insertPlugin(1, makePlugin("aestra.test.identity.unregistered.b"));
    check(restored.getSlotInstanceId(1) > 9000,
          "a mint after loading v2 ids never collides with a restored id");
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

void testPreservedMaxIdCannotPoisonTheCounter() {
    // The boundary CodeRabbit caught on the second pass, and the reason it is
    // dangerous rather than merely odd:
    //
    //   reserve(UINT64_MAX) stored seenId + 1, which wraps the counter to 0.
    //   The next mint's fetch_add then returns 0, the wrap guard fires, and it
    //   hands out 1 — an id that has almost certainly already been minted. Two
    //   live occupants would share an identity, which is exactly the ambiguity
    //   this whole mechanism exists to remove.
    //
    // The codebase already had a policy for this and I had not applied it:
    // SourceManager and PatternManager both refuse to restore UINT64_MAX for the
    // same wrap reason. This pins the same rule here.
    EffectChain chain;
    chain.insertPlugin(0, makePlugin("ordinary"));
    const uint64_t ordinary = chain.getSlotInstanceId(0);
    check(ordinary != 0, "an ordinary identity was minted first");

    // Preserving the boundary value must fall back to a fresh mint rather than
    // installing an id the counter cannot be advanced past.
    chain.insertPlugin(1, makePlugin("boundary"), std::numeric_limits<uint64_t>::max());
    const uint64_t boundary = chain.getSlotInstanceId(1);
    check(boundary != std::numeric_limits<uint64_t>::max(),
          "UINT64_MAX is refused as a preserved identity");
    check(boundary != 0, "the fallback still produces a real identity");
    check(boundary != ordinary, "and it does not collide with the one already minted");

    // The counter must still be healthy afterwards: the failure mode was that
    // every LATER mint got poisoned, so the next two ids are the real assertion.
    chain.insertPlugin(2, makePlugin("after.a"));
    chain.insertPlugin(3, makePlugin("after.b"));
    const uint64_t afterA = chain.getSlotInstanceId(2);
    const uint64_t afterB = chain.getSlotInstanceId(3);
    std::set<uint64_t> unique{ordinary, boundary, afterA, afterB};
    check(unique.size() == 4, "mints after the boundary attempt stay unique");
    check(afterA != 0 && afterB != 0, "and stay non-zero");
}

void testReserveIgnoresTheMaxIdDirectly() {
    // Same rule at the helper's own boundary, so the guarantee does not depend on
    // insertPlugin being the only caller — v2 load will call reserve directly.
    reserveMintedPluginInstanceId(std::numeric_limits<uint64_t>::max());

    EffectChain chain;
    chain.insertPlugin(0, makePlugin("after.reserve.max"));
    const uint64_t first = chain.getSlotInstanceId(0);
    chain.insertPlugin(1, makePlugin("after.reserve.max.2"));
    const uint64_t second = chain.getSlotInstanceId(1);

    check(first != 0 && second != 0, "minting still works after reserving the boundary value");
    check(first != second, "and still produces distinct identities");
}

void testPreservedIdAlreadyLiveInChainFallsBackToMint() {
    // A preserved id that is already occupied elsewhere in the same chain cannot
    // be honoured without putting one identity in two slots. Fall back to a mint:
    // the plugin still belongs in the slot, it just cannot keep a taken name.
    EffectChain chain;
    chain.insertPlugin(0, makePlugin("incumbent"));
    const uint64_t incumbent = chain.getSlotInstanceId(0);

    chain.insertPlugin(1, makePlugin("claimant"), incumbent);

    check(chain.getSlotInstanceId(1) != incumbent,
          "a preserved id already live in the chain is refused");
    check(chain.getSlotInstanceId(1) != 0, "the claimant still gets a real identity");
    check(chain.findSlotByInstanceId(incumbent) == 0,
          "and the incumbent keeps its identity, unambiguously");
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
    testSaveLoadRoundTripPreservesIdentity();
    testV1StateStillLoadsAndMints();
    testV2MissingIdMintsFresh();
    testV2DuplicateIdMintsFresh();
    testPlaceholderIdentitySurvivesRoundTrip();
    testLoadedIdsAreReservedAgainstFutureMints();
    testLoadClearsIdentityOnSerializedEmptySlots();
    testUndoOfRemoveRestoresTheSameIdentity();
    testRedoOfAddKeepsTheOriginalIdentity();
    testUndoOfEffectRemoveRestoresTheSameIdentity();
    testRedoOfEffectAddKeepsTheOriginalIdentity();
    testPreservedIdentityIsReservedAgainstFutureMints();
    testPreservedMaxIdCannotPoisonTheCounter();
    testReserveIgnoresTheMaxIdDirectly();
    testPreservedIdAlreadyLiveInChainFallsBackToMint();
    testPreservedZeroStillMints();

    if (g_failures != 0) {
        std::cerr << "[FAIL] EffectChainInstanceIdentityTest: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "[PASS] EffectChainInstanceIdentityTest\n";
    return 0;
}
