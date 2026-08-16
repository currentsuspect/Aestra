// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// PluginRackBindingLifecycleTest — the inspector rack must follow the
// LAST-bound chain and never hold a binding to a chain that died with its
// channel (three SEGVs in one day: EffectChain::getPlugin from
// refreshRackDisplay on a normal frame update).
//
// Root cause: bindEffectRack() APPENDED bindings, and refreshRackDisplay()
// read the FIRST match — so after switching selection A -> B, the rack kept
// displaying (and later dereferencing) chain A. When channel A was deleted,
// chain A's memory was freed and the next fingerprint-triggered refresh
// crashed. The fix: one binding per rack — a re-bind replaces the previous.

#include "PluginBrowserPanel.h"
#include "PluginUIController.h"
#include "Plugin/EffectChain.h"
#include "Plugin/PluginHost.h"

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
    EffectChain chainA;
    EffectChain chainB;
    chainA.insertPlugin(0, makePlugin("Alpha"));
    chainB.insertPlugin(0, makePlugin("Beta"));

    PluginUIController controller;

    // Initial bind: the rack shows chain A's content.
    controller.bindEffectRack(rack.get(), &chainA);
    controller.refreshRackDisplay(rack.get());
    expect(slotName(*rack, 0) == "Alpha", "first bind shows chain A");

    // Selection switch A -> B: the rack must now show chain B. Regression:
    // the old code appended the binding and read the FIRST match, so the rack
    // kept showing (and dereferencing) chain A — which later crashed when
    // channel A was deleted.
    controller.bindEffectRack(rack.get(), &chainB);
    controller.refreshRackDisplay(rack.get());
    expect(slotName(*rack, 0) == "Beta", "re-bind replaces the previous binding (rack shows chain B)");

    // Cycling back to A must work too — the replace invariant holds both ways.
    controller.bindEffectRack(rack.get(), &chainA);
    controller.refreshRackDisplay(rack.get());
    expect(slotName(*rack, 0) == "Alpha", "cycling back to chain A shows A");

    // Unbind clears the binding; a fresh bind afterwards works (the app uses
    // this when the mixer selection is cleared).
    controller.unbindEffectRack(rack.get());
    controller.bindEffectRack(rack.get(), &chainB);
    controller.refreshRackDisplay(rack.get());
    expect(slotName(*rack, 0) == "Beta", "bind after unbind shows chain B");

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "plugin rack binding lifecycle passed\n";
    return 0;
}
