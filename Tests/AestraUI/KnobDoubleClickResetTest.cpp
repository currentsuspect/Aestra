// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// KnobDoubleClickResetTest — a double-click on a knob of one of Aestra's plugins puts it back to its default.
//
// Reported by the founder: no double-click to reset on any of Aestra's plugin knobs. Nine editors (Rumble, LFO, Limit,
// Transient, Sat, Delay, Filter, OTT, Drift) share AestraPanelWindow::handleKnobDrag, and it looked only at single presses
// and drags: the mouse event carries a doubleClick flag that nothing read. A double-click on a knob now sets the parameter
// to the plugin's default and takes no drag capture; anything else behaves as before.

#include "Widgets/AestraPanelWindow.h"
#include "Plugin/PluginHost.h"

#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace Aestra::Audio;
using namespace AestraUI;

namespace {
int g_failures = 0;

void check(bool condition, const std::string& label) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << label << std::endl;
    if (!condition) {
        ++g_failures;
    }
}

class FakePlugin final : public IPluginInstance {
public:
    FakePlugin() {
        m_info.id = "test.knobs";
        m_info.name = "Knobs";
        m_info.format = PluginFormat::Internal;
        m_info.type = PluginType::Effect;
        for (const auto& [id, def] : std::vector<std::pair<uint32_t, float>>{{100, 0.25f}, {101, 0.8f}}) {
            PluginParameter p;
            p.id = id;
            p.name = "P" + std::to_string(id);
            p.defaultValue = def;
            m_params.push_back(p);
        }
    }
    bool initialize(double, uint32_t) override { return true; }
    void shutdown() override {}
    void activate() override {}
    void deactivate() override {}
    bool isActive() const override { return true; }
    void process(const float* const*, float**, uint32_t, uint32_t, uint32_t, const MidiBuffer* = nullptr,
                 MidiBuffer* = nullptr) override {}
    std::vector<PluginParameter> getParameters() const override { return m_params; }
    uint32_t getParameterCount() const override { return static_cast<uint32_t>(m_params.size()); }
    float getParameter(uint32_t) const override { return 0.9f; }
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
    std::vector<PluginParameter> m_params;
};

// An editor with two knobs, as the real ones have, driving handleKnobDrag the way they do.
class TestPanel final : public AestraPanelWindow {
public:
    TestPanel() { setBounds(NUIRect{0, 0, 400, 300}); }
    void drawContent(NUIRenderer&, const NUIRect&) override {}

    std::map<uint32_t, float> values{{100, 0.9f}, {101, 0.9f}};
    int sets = 0;

    std::vector<KnobTarget> targets{{NUIRect{20, 20, 40, 40}, 100}, {NUIRect{100, 20, 40, 40}, 101}};

    bool mouse(const NUIMouseEvent& e) {
        return handleKnobDrag(
            e, targets.data(), targets.size(), 150.0f, [&](uint32_t id) { return values[id]; },
            [&](uint32_t id, float v) {
                values[id] = v;
                ++sets;
            });
    }
    bool dragging() const { return knobDragParam() >= 0; }
};

NUIMouseEvent press(float x, float y, bool doubleClick) {
    NUIMouseEvent e;
    e.type = NUIMouseEventType::Down;
    e.position = {x, y};
    e.button = NUIMouseButton::Left;
    e.pressed = true;
    e.doubleClick = doubleClick;
    return e;
}
} // namespace

int main() {
    auto plugin = std::make_shared<FakePlugin>();

    {
        TestPanel panel;
        panel.setKnobDefaultsFrom(plugin);
        check(panel.mouse(press(40, 40, true)), "a double-click on a knob is consumed");
        check(panel.values[100] == 0.25f, "and sets that knob to the plugin's default");
        check(!panel.dragging(), "and does not start a drag");
        check(panel.mouse(press(120, 40, true)) && panel.values[101] == 0.8f, "each knob goes to its own default");
    }

    {
        TestPanel panel;
        panel.setKnobDefaultsFrom(plugin);
        check(!panel.mouse(press(300, 200, true)), "a double-click that is not on a knob is not consumed");
        check(panel.sets == 0, "and changes nothing");
    }

    {
        TestPanel panel;
        panel.setKnobDefaultsFrom(plugin);
        check(panel.mouse(press(40, 40, false)), "a single press on a knob is still consumed");
        check(panel.dragging(), "and still starts the drag");
        check(panel.sets == 0 && panel.values[100] == 0.9f, "without changing the value");
    }

    {
        // Nothing to reset to: the press goes through as it always did.
        TestPanel panel;
        check(panel.mouse(press(40, 40, true)), "without a plugin to ask, a double-click press is still consumed");
        check(panel.values[100] == 0.9f, "but the value is left alone");
    }

    {
        // A parameter the plugin has no default for keeps its value.
        TestPanel panel;
        panel.setKnobDefaultsFrom(plugin);
        panel.targets = {{NUIRect{20, 20, 40, 40}, 999}};
        panel.values[999] = 0.5f;
        check(panel.mouse(press(40, 40, true)) && panel.values[999] == 0.5f,
              "a knob whose parameter has no known default is not reset");
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "knob double-click reset passed\n";
    return 0;
}
