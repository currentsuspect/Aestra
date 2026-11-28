// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// GenericPluginEditorHeaderTest — the generic plugin editor has the same two-row header as every other plugin window.
//
// Reported by the founder with RF-480R RVRB: the generic editor's title bar was a mess. It never turned on the two-row header
// (FD-73) that Aestra's editors and the native plugin panel use, so the preset bar was squeezed into the one 32 px row beside
// the title, the close button had no room, and the A/B buttons were cut off. Its parameter rows also started at a fixed
// 32 px, so a taller header would have covered them. Row one is the title, the context and close; row two is the preset bar.

#include "Widgets/GenericPluginEditor.h"

#include <iostream>
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
        m_info.id = "test.generic";
        m_info.name = "RF-480R RVRB";
        m_info.format = PluginFormat::CLAP;
        m_info.type = PluginType::Effect;
        for (uint32_t i = 0; i < 12; ++i) {
            PluginParameter p;
            p.id = 100 + i;
            p.name = "Parameter " + std::to_string(i);
            p.shortName = p.name;
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
    float getParameter(uint32_t) const override { return 0.0f; }
    void setParameter(uint32_t, float) override {}
    std::string getParameterDisplay(uint32_t) const override { return "0.00"; }
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
} // namespace

int main() {
    auto editor = std::make_shared<GenericPluginEditor>(std::make_shared<FakePlugin>());

    check(editor->hasTwoRowHeader(), "the generic editor uses the two-row header, as every other plugin window does");
    check(editor->headerHeight() >= 2.0f * AestraPanelWindow::TITLE_BAR_H, "so its header is two rows tall");
    check(editor->firstParameterTop() >= editor->headerHeight(),
          "the first parameter row starts below the whole header, not under the second row");
    check(editor->getBounds().height > editor->headerHeight() + 100.0f,
          "the window is tall enough to show parameter rows below the header");

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "generic plugin editor header passed\n";
    return 0;
}
