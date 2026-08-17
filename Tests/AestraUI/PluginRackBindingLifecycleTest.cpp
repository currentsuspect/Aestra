// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// PluginRackBindingLifecycleTest — the inspector rack must resolve its chain
// FRESH from the channel's stable identity, never from a cached pointer.
//
// The pointer-based binding crashed five times in one day (2026-08-16,
// 12:58/16:10/19:28 + 22:04/22:19): refreshRackDisplay() dereferenced an
// EffectChain that died with its channel — first because re-binds appended
// stale bindings, then (after the replace fix) because the frame's
// fingerprint refresh can fire before the selection sync re-binds, and the
// VM's channel pointer itself can go stale. The binding now stores the
// channel ID and resolves the chain at refresh/callback time; a deleted
// channel resolves to null and the rack clears to Empty.

#include "PluginBrowserPanel.h"
#include "PluginUIController.h"
#include "Plugin/EffectChain.h"
#include "Plugin/PluginHost.h"
#include "Models/TrackManager.h"

#include <iostream>
#include <memory>
#include <string>

using namespace AestraUI;
using namespace Aestra::Audio;

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        ++g_failures;
    }
}

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
    void activate() override {}
    void deactivate() override {}
    bool isActive() const override { return true; }

    void process(const float* const* inputs, float** outputs, uint32_t, uint32_t, uint32_t,
                 const MidiBuffer* = nullptr, MidiBuffer* = nullptr) override {
        if (inputs == nullptr || outputs == nullptr) {
            return;
        }
        outputs[0][0] = inputs[0][0];
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
    PluginInfo m_info{};
};

PluginInstancePtr makePlugin(const std::string& id) {
    return std::make_shared<IdentityTestPlugin>(id);
}

std::string slotName(EffectChainRack& rack, int slot) {
    return rack.getSlot(slot).name;
}

} // namespace

int main() {
    auto rack = std::make_shared<EffectChainRack>();
    TrackManager trackManager;
    MixerChannel* channelA = trackManager.addChannel("Alpha Channel");
    MixerChannel* channelB = trackManager.addChannel("Beta Channel");
    if (!channelA || !channelB) {
        std::cerr << "failed to add channels\n";
        return 1;
    }
    channelA->getEffectChain().insertPlugin(0, makePlugin("Alpha"));
    channelB->getEffectChain().insertPlugin(0, makePlugin("Beta"));
    const uint32_t idA = channelA->getChannelId();
    const uint32_t idB = channelB->getChannelId();

    PluginUIController controller;

    // Initial bind by stable id: the rack shows channel A's content.
    controller.bindEffectRack(rack.get(), &trackManager, idA);
    controller.refreshRackDisplay(rack.get());
    expect(slotName(*rack, 0) == "Alpha", "first bind shows chain A");

    // Selection switch A -> B: re-bind replaces; rack now shows B.
    controller.bindEffectRack(rack.get(), &trackManager, idB);
    controller.refreshRackDisplay(rack.get());
    expect(slotName(*rack, 0) == "Beta", "re-bind replaces the previous binding");

    // Cycling back to A works.
    controller.bindEffectRack(rack.get(), &trackManager, idA);
    controller.refreshRackDisplay(rack.get());
    expect(slotName(*rack, 0) == "Alpha", "cycling back to chain A shows A");

    // THE REGRESSION: channel A dies (deleted mid-session). The rack's next
    // refresh must resolve null and clear to Empty — never dereference the
    // freed chain. The pointer-based binding crashed here (5 SEGVs, #790).
    trackManager.removeChannelById(idA);
    controller.refreshRackDisplay(rack.get());
    expect(slotName(*rack, 0) == "Empty", "deleted channel clears the rack (no stale chain)");

    // Re-bind to the surviving channel still works after the deletion.
    controller.bindEffectRack(rack.get(), &trackManager, idB);
    controller.refreshRackDisplay(rack.get());
    expect(slotName(*rack, 0) == "Beta", "bind after deletion shows chain B");

    // Master (id 0) resolves through getMasterChannel.
    trackManager.getMasterChannel()->getEffectChain().insertPlugin(0, makePlugin("Master"));
    controller.bindEffectRack(rack.get(), &trackManager, 0);
    controller.refreshRackDisplay(rack.get());
    expect(slotName(*rack, 0) == "Master", "master channel binds and resolves");

    // Unbind clears the binding; a fresh bind afterwards works.
    controller.unbindEffectRack(rack.get());
    controller.bindEffectRack(rack.get(), &trackManager, idB);
    controller.refreshRackDisplay(rack.get());
    expect(slotName(*rack, 0) == "Beta", "bind after unbind shows chain B");

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "plugin rack binding lifecycle passed\n";
    return 0;
}
