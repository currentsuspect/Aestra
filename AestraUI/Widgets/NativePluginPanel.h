// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraPanelWindow.h"
#include "PluginHost.h"

#include <cstdint>
#include <memory>

namespace AestraUI {

class NUIPlatformBridge;

/**
 * @brief A third-party plugin's own GUI, inside an Aestra plugin panel.
 *
 * The plugin's window belongs to the sandbox helper, made as a child of the app's window (EDITOR EMBED, X11). This
 * panel gives it Aestra's header (name, where it sits, presets, close), keeps the child window glued to the content
 * area while the panel is dragged, hides it with the panel, and, when the plugin accepts other sizes, resizes it from
 * the grip in the panel's bottom-right corner. The plugin may also resize itself; the panel follows.
 *
 * Moving and showing the child is done on the app's own X connection (NUIPlatformBridge::placeChildWindow), so a
 * drag costs no round trip to the helper. Where the platform cannot embed (Wayland without XWayland, Windows for
 * now), create() returns nullptr and the plugin opens in its own window instead.
 */
class NativePluginPanel : public AestraPanelWindow {
public:
    /// Opens the plugin's editor embedded in the app's window. nullptr when it cannot be embedded.
    static std::shared_ptr<NativePluginPanel> create(const std::shared_ptr<Aestra::Audio::IPluginInstance>& instance,
                                                     NUIPlatformBridge* bridge);
    ~NativePluginPanel() override;

    void onUpdate(double deltaTime) override;
    void drawContent(NUIRenderer& renderer, const NUIRect& contentRect) override;
    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    std::shared_ptr<Aestra::Audio::IPluginInstance> instance() const { return m_instance.lock(); }
    /// The panel size that holds a plugin view of @p width × @p height.
    NUISize panelSizeFor(int width, int height) const;

    static constexpr float kGripBand = 12.0f; ///< the strip under a resizable plugin that holds the grip
    static constexpr int kMinWidth = 240;
    static constexpr int kMinHeight = 140;

private:
    NativePluginPanel(const std::shared_ptr<Aestra::Audio::IPluginInstance>& instance, NUIPlatformBridge* bridge,
                      std::uintptr_t child, int width, int height, bool resizable);

    /// Where the plugin's view sits, in window coordinates (the content area, less the grip band).
    NUIRect viewRect() const;
    bool shownInTree() const;
    void place(bool force);
    NUIRect gripRect() const;

    std::weak_ptr<Aestra::Audio::IPluginInstance> m_instance;
    NUIPlatformBridge* m_bridge = nullptr;
    std::uintptr_t m_child = 0;
    int m_viewW = 0;
    int m_viewH = 0;
    bool m_resizable = false;

    // What was last sent to the window system, so an unchanged frame costs nothing.
    NUIRect m_placed;
    bool m_placedVisible = false;
    bool m_everPlaced = false;

    double m_sizePoll = 0.0;
    bool m_resizing = false;
    NUIPoint m_resizeStart;
    int m_resizeStartW = 0;
    int m_resizeStartH = 0;
    bool m_gripHover = false;
};

} // namespace AestraUI
