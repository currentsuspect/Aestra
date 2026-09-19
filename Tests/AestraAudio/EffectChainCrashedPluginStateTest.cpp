// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// A crashed plugin must not erase its own last good state on the next save (#931).
//
// Before this test's fix, EffectChain::saveState took the live-plugin branch for
// a crashed occupant and wrote whatever saveState() returned — which for a dead
// out-of-process helper is always empty (every failure path in
// OutOfProcessPluginInstance::saveState returns {}). The first save after a
// crash, manual or autosave, therefore persisted an empty blob over the last
// good state. The DAW survived the crash; the save path cemented it.
//
// The fix: every successful non-empty capture refreshes a per-slot last-good
// cache (keyed by instance id), and a crashed occupant is emitted as a #647
// missing-record carrying the cached blob. No wire-format change — a reload
// with the plugin present restores it with its pre-crash state.
//
// Scope note: the tests use a locally defined crashable IPluginInstance so they
// never depend on a real helper process dying on cue. A test that needed a real
// crash could pass or fail for reasons unrelated to the invariant.

#include "Plugin/EffectChain.h"

#include "Plugin/PluginManager.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
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

std::vector<uint8_t> bytes(std::initializer_list<int> vals) {
    std::vector<uint8_t> v;
    for (int x : vals) v.push_back(static_cast<uint8_t>(x));
    return v;
}

// Minimal effect instance with a crash switch. Healthy it reports its bytes;
// crashed it answers saveState() with empty, exactly like a dead helper.
class CrashableTestPlugin final : public IPluginInstance {
public:
    CrashableTestPlugin(const std::string& id, std::vector<uint8_t> state) : m_state(std::move(state)) {
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

    void crash() { m_crashed = true; }

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
    std::vector<uint8_t> saveState() const override { return m_crashed ? std::vector<uint8_t>{} : m_state; }
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
    bool isCrashed() const override { return m_crashed; }

private:
    bool m_active{false};
    bool m_crashed{false};
    PluginInfo m_info{};
    std::vector<uint8_t> m_state;
};

const char* kCrashId = "com.aestra.tests.crashable";

PluginManager& manager() { return PluginManager::getInstance(); }

} // namespace

int main() {
    const std::vector<uint8_t> goodState = bytes({0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01});

    // ---------------------------------------------------------------------
    // The core invariant: a crash between two saves must not erase the state
    // the first save captured.
    // ---------------------------------------------------------------------
    {
        EffectChain chain;
        chain.prepare(48000.0, 512);
        auto plugin = std::make_shared<CrashableTestPlugin>(kCrashId, goodState);
        check(chain.insertPlugin(0, plugin), "crash: insert ok");
        const uint64_t liveId = chain.getSlotInstanceId(0);

        const std::vector<uint8_t> before = chain.saveState();
        std::vector<SlotRecord> beforeRecs;
        check(parseBlob(before, beforeRecs), "crash: pre-crash blob parses");
        if (beforeRecs.size() == EffectChain::MAX_SLOTS) {
            check(beforeRecs[0].present && beforeRecs[0].state == goodState,
                  "crash: pre-crash save carries the live state");
        }

        plugin->crash();
        const std::vector<uint8_t> after = chain.saveState();
        std::vector<SlotRecord> afterRecs;
        check(parseBlob(after, afterRecs), "crash: post-crash blob parses");
        if (afterRecs.size() == EffectChain::MAX_SLOTS) {
            check(afterRecs[0].present, "crash: crashed slot is still written, not dropped");
            check(afterRecs[0].id == kCrashId, "crash: crashed slot keeps its plugin id");
            check(afterRecs[0].state == goodState,
                  "crash: post-crash save carries the last good state, not empty");
            check(afterRecs[0].instanceId == liveId, "crash: post-crash save keeps the wire identity");
        }
    }

    // ---------------------------------------------------------------------
    // Boundary: a crash with no prior successful save has nothing to
    // preserve. The slot must still be written with its id, never dropped.
    // ---------------------------------------------------------------------
    {
        EffectChain chain;
        chain.prepare(48000.0, 512);
        auto plugin = std::make_shared<CrashableTestPlugin>(kCrashId, goodState);
        check(chain.insertPlugin(2, plugin), "cold: insert ok");
        plugin->crash();

        std::vector<SlotRecord> out;
        check(parseBlob(chain.saveState(), out), "cold: blob parses");
        if (out.size() == EffectChain::MAX_SLOTS) {
            check(out[2].present && out[2].id == kCrashId, "cold: crashed slot keeps its id");
            check(out[2].state.empty(), "cold: nothing cached means an empty blob, honestly written");
        }
    }

    // ---------------------------------------------------------------------
    // Healthy saves are byte-identical with the cache in place: the cache
    // must never perturb normal output.
    // ---------------------------------------------------------------------
    {
        EffectChain chain;
        chain.prepare(48000.0, 512);
        check(chain.insertPlugin(1, std::make_shared<CrashableTestPlugin>(kCrashId, goodState)),
              "healthy: insert ok");
        const std::vector<uint8_t> first = chain.saveState();
        const std::vector<uint8_t> second = chain.saveState();
        check(first == second, "healthy: repeated saves are byte-identical");
    }

    // ---------------------------------------------------------------------
    // The crashed record round-trips through the #647 machinery: a fresh
    // chain that cannot instantiate the id keeps it as a placeholder with
    // the preserved bytes, and re-emits them unchanged.
    // ---------------------------------------------------------------------
    {
        EffectChain chain;
        chain.prepare(48000.0, 512);
        auto plugin = std::make_shared<CrashableTestPlugin>(kCrashId, goodState);
        check(chain.insertPlugin(0, plugin), "reload: insert ok");
        chain.saveState(); // prime the last-good cache
        plugin->crash();
        const std::vector<uint8_t> crashedBlob = chain.saveState();

        EffectChain reloaded;
        reloaded.prepare(48000.0, 512);
        std::vector<std::string> missing;
        check(reloaded.loadState(crashedBlob, manager(), &missing), "reload: loadState ok");
        check(reloaded.getMissingPluginId(0) == kCrashId, "reload: crashed record arrives as a placeholder");
        check(reloaded.saveState() == crashedBlob, "reload: placeholder re-emits the preserved bytes");
    }

    // ---------------------------------------------------------------------
    // Replacement isolation: removing a crashed plugin and inserting a new
    // one must not resurrect the old occupant's cached state.
    // ---------------------------------------------------------------------
    {
        EffectChain chain;
        chain.prepare(48000.0, 512);
        auto doomed = std::make_shared<CrashableTestPlugin>(kCrashId, goodState);
        check(chain.insertPlugin(0, doomed), "replace: insert ok");
        chain.saveState(); // prime the cache, then crash and clear it away
        doomed->crash();
        chain.removePlugin(0);

        const std::vector<uint8_t> freshState = bytes({0x11, 0x22});
        check(chain.insertPlugin(0, std::make_shared<CrashableTestPlugin>(kCrashId, freshState)),
              "replace: reinsert ok");
        std::vector<SlotRecord> out;
        check(parseBlob(chain.saveState(), out), "replace: blob parses");
        if (out.size() == EffectChain::MAX_SLOTS) {
            check(out[0].present && out[0].state == freshState,
                  "replace: new occupant saves its own state, not the cached one");
        }
    }

    if (g_failures != 0) {
        std::cerr << "[FAIL] EffectChainCrashedPluginStateTest: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "[PASS] EffectChainCrashedPluginStateTest\n";
    return 0;
}
