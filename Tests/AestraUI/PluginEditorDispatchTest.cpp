// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// PluginEditorDispatchTest — a sandboxed plugin's editor must be its OWN window.
//
// Before this, openPluginEditor() only ever built a widget editor, so every VST3/CLAP
// plugin fell through the registry to GenericPluginEditor and the user got a grid of
// sliders where the plugin's real GUI should be. The host side was already complete:
// OutOfProcessPluginInstance::openEditor() asks the helper process to create the window,
// attach the plugin's view and pump it (HelperEditorHost.h), and the helper keeps serving
// audio commands while that window is open.
//
// The dispatch is the whole fix, so this test pins it: the host is asked exactly once,
// activating the same plugin again raises rather than duplicating, and the window is
// closed when the controller goes away.

#include "PluginUIController.h"
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

class SandboxedTestPlugin final : public IPluginInstance {
public:
    explicit SandboxedTestPlugin(std::string id) {
        m_info.id = std::move(id);
        m_info.name = "Sandboxed Test";
        m_info.vendor = "Aestra Tests";
        m_info.version = "1";
        m_info.category = "Test";
        m_info.format = PluginFormat::CLAP;
        m_info.type = PluginType::Effect;
        m_info.numAudioInputs = 2;
        m_info.numAudioOutputs = 2;
    }

    bool initialize(double, uint32_t) override { return true; }
    void shutdown() override {}
    void activate() override {}
    void deactivate() override {}
    bool isActive() const override { return true; }
    void process(const float* const*, float**, uint32_t, uint32_t, uint32_t, const MidiBuffer* = nullptr,
                 MidiBuffer* = nullptr) override {}
    std::vector<PluginParameter> getParameters() const override { return {}; }
    uint32_t getParameterCount() const override { return 0; }
    float getParameter(uint32_t) const override { return 0.0f; }
    void setParameter(uint32_t, float) override {}
    std::string getParameterDisplay(uint32_t) const override { return {}; }
    std::vector<uint8_t> saveState() const override { return {}; }
    bool loadState(const std::vector<uint8_t>&) override { return true; }

    bool hasEditor() const override { return true; }
    bool ownsEditorWindow() const override { return true; }
    bool openEditor(void*) override {
        ++openCalls;
        editorOpen = true;
        return true;
    }
    bool raiseEditor() override {
        ++raiseCalls;
        // A host whose window the user closed with the plugin's own close button
        // cannot raise it: it reports false so the app opens a new one.
        return raiseSucceeds;
    }
    void closeEditor() override {
        if (!editorOpen) return;
        editorOpen = false;
        ++closeCalls;
    }
    bool isEditorOpen() const override { return editorOpen; }
    std::pair<int, int> getEditorSize() const override { return {920, 600}; }
    bool resizeEditor(int, int) override { return false; }

    const PluginInfo& getInfo() const override { return m_info; }
    uint32_t getLatencySamples() const override { return 0; }
    uint32_t getTailSamples() const override { return 0; }
    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

    int openCalls = 0;
    int raiseCalls = 0;
    int closeCalls = 0;
    bool editorOpen = false;
    bool raiseSucceeds = true;

private:
    PluginInfo m_info{};
};

} // namespace

int main() {
    auto plugin = std::make_shared<SandboxedTestPlugin>("com.aestra.test.sandboxed");

    {
        PluginUIController controller;

        // The plugin's own GUI, not the generic parameter editor.
        controller.openPluginEditor(plugin);
        expect(plugin->openCalls == 1, "a sandboxed plugin's editor is opened through the host");
        expect(plugin->isEditorOpen(), "the host is left with the editor open");

        // Activating the same plugin again must not stack a second copy of the GUI.
        controller.openPluginEditor(plugin);
        expect(plugin->openCalls == 1, "activating an open editor does not reopen it");
        expect(plugin->raiseCalls == 1, "activating an open editor raises the existing window");

        // The user closes the plugin's own window. The next Edit has to reopen it:
        // raising a window the host no longer has would show nothing at all.
        plugin->raiseSucceeds = false;
        controller.openPluginEditor(plugin);
        expect(plugin->openCalls == 2, "a closed window is opened again, not raised");
        expect(plugin->isEditorOpen(), "the reopened editor is left open");
    }

    expect(plugin->closeCalls == 2, "each opened editor is closed when the controller goes away");
    expect(!plugin->isEditorOpen(), "no window outlives the controller");

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "plugin editor dispatch passed\n";
    return 0;
}
