// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V82-H2: a plugin's own latency can change after it loads (LSP's limiters only know theirs once they have processed
// a block; others change it with a setting). Slot edits already announce through the chain's latency callback, which
// is what makes the engine re-solve delay compensation. EffectChain::refreshLatency() covers the rest: the UI frame
// calls it, and it announces exactly when the chain's total is not what the owner was last told.

#include "EffectChain.h"
#include "PluginHost.h"

#include <atomic>
#include <iostream>
#include <memory>
#include <string>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;
void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cout << "[FAIL] " << message << '\n';
        ++g_failures;
    }
}

// An effect whose latency the test can change, as a real plugin's can change under it.
class LatencyTestPlugin final : public IPluginInstance {
public:
    explicit LatencyTestPlugin(uint32_t latency) : m_latency(latency) {
        m_info.id = "com.aestra.tests.latency";
        m_info.name = "latency";
        m_info.vendor = "Aestra Tests";
        m_info.version = "1";
        m_info.category = "Test";
        m_info.format = PluginFormat::Internal;
        m_info.type = PluginType::Effect;
        m_info.numAudioInputs = 2;
        m_info.numAudioOutputs = 2;
    }

    void setLatency(uint32_t samples) { m_latency.store(samples, std::memory_order_release); }

    bool initialize(double, uint32_t) override { return true; }
    void shutdown() override {}
    void activate() override { m_active = true; }
    void deactivate() override { m_active = false; }
    bool isActive() const override { return m_active; }
    void process(const float* const*, float**, uint32_t, uint32_t, uint32_t, const MidiBuffer* = nullptr,
                 MidiBuffer* = nullptr) override {}
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
    uint32_t getLatencySamples() const override { return m_latency.load(std::memory_order_acquire); }
    uint32_t getTailSamples() const override { return 0; }
    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

private:
    bool m_active{false};
    std::atomic<uint32_t> m_latency;
    PluginInfo m_info{};
};

} // namespace

int main() {
    EffectChain chain;
    chain.prepare(48000.0, 512);
    int announcements = 0;
    chain.setLatencyChangedCallback([&]() { ++announcements; });

    auto plugin = std::make_shared<LatencyTestPlugin>(0);
    check(chain.insertPlugin(0, plugin), "insert");
    check(announcements == 1, "inserting a plugin announces, as before");

    chain.refreshLatency();
    check(announcements == 1, "nothing changed: refreshing announces nothing");

    plugin->setLatency(240); // the plugin has now processed a block and knows its lookahead
    check(chain.getTotalLatency() == 240, "the chain total follows the plugin");
    chain.refreshLatency();
    check(announcements == 2, "a plugin that now reports 240 samples is announced");
    chain.refreshLatency();
    chain.refreshLatency();
    check(announcements == 2, "and only once, not on every frame");

    plugin->setLatency(480);
    chain.refreshLatency();
    check(announcements == 3, "a further change is announced too");

    // A bypassed slot contributes nothing, so a change inside it is not news.
    chain.setSlotBypassed(0, true);
    check(announcements == 4, "bypassing announces, as before");
    plugin->setLatency(960);
    chain.refreshLatency();
    check(announcements == 4, "a latency change inside a bypassed slot does not announce");
    chain.setSlotBypassed(0, false);
    check(announcements == 5 && chain.getTotalLatency() == 960, "un-bypassing announces and the engine reads the new 960");

    // Removing it announces and the total is zero again.
    chain.removePlugin(0);
    check(announcements == 6 && chain.getTotalLatency() == 0, "removing the plugin announces");
    chain.refreshLatency();
    check(announcements == 6, "and refreshing an empty chain says nothing");

    // No callback registered: refreshing must not crash.
    EffectChain quiet;
    quiet.prepare(48000.0, 512);
    auto other = std::make_shared<LatencyTestPlugin>(10);
    quiet.insertPlugin(0, other);
    other->setLatency(20);
    quiet.refreshLatency();
    check(quiet.getTotalLatency() == 20, "a chain with no listener still refreshes safely");

    if (g_failures == 0) {
        std::cout << "EffectChainLatencyRefreshTest: all passed\n";
        return 0;
    }
    std::cout << "EffectChainLatencyRefreshTest: " << g_failures << " failure(s)\n";
    return 1;
}
