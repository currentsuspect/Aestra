// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NativePluginPanel.h"

#include "../Core/NUIThemeSystem.h"
#include "../Graphics/NUIRenderer.h"
#include "../Platform/NUIPlatformBridge.h"

#include <algorithm>
#include <cmath>

namespace AestraUI {

std::shared_ptr<NativePluginPanel> NativePluginPanel::create(
    const std::shared_ptr<Aestra::Audio::IPluginInstance>& instance, NUIPlatformBridge* bridge) {
    if (!instance || !bridge || !instance->ownsEditorWindow() || !instance->hasEditor())
        return nullptr;
    const auto parent = reinterpret_cast<std::uintptr_t>(bridge->getNativeHandle());
    if (!parent)
        return nullptr; // not an X11 window: nothing to embed into
    const std::uintptr_t child = instance->openEditorEmbedded(parent);
    if (!child)
        return nullptr;
    const auto [w, h] = instance->getEditorSize();
    return std::shared_ptr<NativePluginPanel>(
        new NativePluginPanel(instance, bridge, child, std::max(w, 1), std::max(h, 1), instance->canResizeEditor()));
}

NativePluginPanel::NativePluginPanel(const std::shared_ptr<Aestra::Audio::IPluginInstance>& instance,
                                     NUIPlatformBridge* bridge, std::uintptr_t child, int width, int height,
                                     bool resizable)
    : m_instance(instance), m_bridge(bridge), m_child(child), m_viewW(width), m_viewH(height), m_resizable(resizable) {
    const auto& info = instance->getInfo();
    setPanelTitle(info.name.empty() ? info.id : info.name);
    setTwoRowHeader(true);
    setPlatformBridge(bridge);
    const NUISize size = panelSizeFor(width, height);
    setBounds(NUIRect{0.0f, 0.0f, size.width, size.height});
}

NativePluginPanel::~NativePluginPanel() {
    // The child window is the helper's: closing the editor destroys it. Hide it first so it never outlives the panel
    // on screen, even for the moment the helper takes to answer.
    if (m_bridge && m_child)
        m_bridge->placeChildWindow(m_child, 0, 0, 1, 1, false);
    if (auto instance = m_instance.lock())
        instance->closeEditor();
}

NUISize NativePluginPanel::panelSizeFor(int width, int height) const {
    return {static_cast<float>(width), headerHeight() + static_cast<float>(height) + (m_resizable ? kGripBand : 0.0f)};
}

NUIRect NativePluginPanel::viewRect() const {
    const NUIRect content = getContentRect();
    return {content.x, content.y, static_cast<float>(m_viewW), static_cast<float>(m_viewH)};
}

NUIRect NativePluginPanel::gripRect() const {
    const auto b = getBounds();
    return {b.right() - 18.0f, b.bottom() - kGripBand, 18.0f, kGripBand};
}

bool NativePluginPanel::shownInTree() const {
    for (const NUIComponent* c = this; c; c = c->getParent())
        if (!c->isVisible())
            return false;
    return true;
}

void NativePluginPanel::place(bool force) {
    if (!m_bridge || !m_child)
        return;
    // A child window always draws over the app. While the panel's own preset fold or menu is open it would cover
    // the fold, so the plugin steps aside until the fold closes.
    const bool popoverOpen = titleAccessory() && titleAccessory()->wantsKeys();
    const bool visible = shownInTree() && !m_instance.expired() && !popoverOpen;
    const NUIRect view = viewRect();
    if (!force && m_everPlaced && visible == m_placedVisible && view.x == m_placed.x && view.y == m_placed.y &&
        view.width == m_placed.width && view.height == m_placed.height)
        return;
    float scale = m_bridge->getDPIScale();
    if (scale <= 0.0f)
        scale = 1.0f;
    m_bridge->placeChildWindow(m_child, static_cast<int>(std::lround(view.x * scale)),
                               static_cast<int>(std::lround(view.y * scale)),
                               static_cast<int>(std::lround(view.width * scale)),
                               static_cast<int>(std::lround(view.height * scale)), visible);
    m_placed = view;
    m_placedVisible = visible;
    m_everPlaced = true;
}

void NativePluginPanel::onUpdate(double deltaTime) {
    AestraPanelWindow::onUpdate(deltaTime);
    place(false);

    // The plugin can change its own size (a zoom menu, a page with more controls). Ask the window system now and then
    // and follow, unless the user is dragging the grip, whose size wins.
    m_sizePoll += deltaTime;
    if (m_sizePoll < 0.25 || m_resizing || !m_bridge || !m_child)
        return;
    m_sizePoll = 0.0;
    int w = 0, h = 0;
    if (!m_bridge->getChildWindowSize(m_child, w, h)) {
        // The window is gone: the plugin was removed or its helper ended. Nothing left to show.
        if (auto instance = m_instance.lock(); !instance || !instance->isEditorOpen())
            setVisible(false);
        return;
    }
    float scale = m_bridge->getDPIScale();
    if (scale <= 0.0f)
        scale = 1.0f;
    const int lw = static_cast<int>(std::lround(w / scale)), lh = static_cast<int>(std::lround(h / scale));
    if (lw > 1 && lh > 1 && (lw != m_viewW || lh != m_viewH)) {
        m_viewW = lw;
        m_viewH = lh;
        const auto b = getBounds();
        const NUISize size = panelSizeFor(lw, lh);
        setBounds(NUIRect{b.x, b.y, size.width, size.height});
        place(true);
        setDirty();
    }
}

void NativePluginPanel::onRender(NUIRenderer& renderer) {
    AestraPanelWindow::onRender(renderer);
    // The view itself is the plugin's window, drawn over this area by the window system. What shows here is the grip.
    if (!m_resizable)
        return;
    const auto& t = NUIThemeManager::getInstance().getCurrentTheme();
    const NUIRect g = gripRect();
    const NUIColor ink = (m_gripHover || m_resizing) ? t.textPrimary : t.textMuted;
    for (int i = 1; i <= 3; ++i) {
        const float d = 3.0f * static_cast<float>(i);
        renderer.drawLine({g.right() - 3.0f - d, g.bottom() - 3.0f}, {g.right() - 3.0f, g.bottom() - 3.0f - d}, 1.0f,
                          ink);
    }
}

void NativePluginPanel::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    // Behind the plugin's window: the bed, in case the plugin is slow to draw its first frame.
    const auto& t = NUIThemeManager::getInstance().getCurrentTheme();
    renderer.fillRect(contentRect, t.backgroundPrimary);
}

bool NativePluginPanel::onMouseEvent(const NUIMouseEvent& event) {
    if (m_resizable) {
        const bool overGrip = gripRect().contains(event.position);
        if (overGrip != m_gripHover) {
            m_gripHover = overGrip;
            setDirty();
        }
        if (event.pressed && event.button == NUIMouseButton::Left && overGrip) {
            m_resizing = true;
            m_resizeStart = event.position;
            m_resizeStartW = m_viewW;
            m_resizeStartH = m_viewH;
            return true;
        }
        if (m_resizing) {
            if (event.released && event.button == NUIMouseButton::Left) {
                m_resizing = false;
                return true;
            }
            const int w = std::max(kMinWidth, m_resizeStartW + static_cast<int>(event.position.x - m_resizeStart.x));
            const int h = std::max(kMinHeight, m_resizeStartH + static_cast<int>(event.position.y - m_resizeStart.y));
            if (w != m_viewW || h != m_viewH) {
                m_viewW = w;
                m_viewH = h;
                const auto b = getBounds();
                const NUISize size = panelSizeFor(w, h);
                setBounds(NUIRect{b.x, b.y, size.width, size.height});
                place(true); // the helper sees the new size and tells the plugin
                setDirty();
            }
            return true;
        }
    }
    if (AestraPanelWindow::onMouseEvent(event))
        return true;
    return consumeInsideBounds(event);
}

} // namespace AestraUI
