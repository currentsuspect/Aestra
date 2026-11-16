// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "EmberControls.h"
#include "NUIThemeSystem.h"
#include "../Graphics/NUIDepth.h"

#include "NUIRenderer.h"

#include <algorithm>
#include <cmath>

namespace AestraUI {
namespace EditorKit {

bool isBypassed(const Aestra::Audio::IPluginInstance& plugin, uint32_t bypassParam) {
    return plugin.getParameter(bypassParam) > 0.5f;
}

// ---------------------------------------------------------------------------------------------------------------
// Knob board
// ---------------------------------------------------------------------------------------------------------------

void EmberKnobBoard::clear() {
    m_cells.clear();
    m_headers.clear();
}

void EmberKnobBoard::place(const Spec& spec, const NUIPoint& topLeft) {
    Cell c;
    c.spec = spec;
    const float w = spec.width > 0.0f ? spec.width : emberKnobCellWidth(spec.size);
    c.rect = NUIRect(topLeft.x, topLeft.y, w, emberKnobCellHeight(spec.size));
    m_cells.push_back(c);
}

void EmberKnobBoard::addHeader(const NUIRect& span, const char* label) { m_headers.push_back({span, label}); }

void EmberKnobBoard::layoutRow(const NUIPoint& origin, float width, const std::vector<Group>& groups) {
    constexpr float kGroupGap = 16.0f;
    float sum = 0.0f;
    int count = 0;
    for (const auto& g : groups)
        for (const auto& k : g.knobs) {
            sum += k.width > 0.0f ? k.width : emberKnobCellWidth(k.size);
            ++count;
        }
    const float gap = std::max(4.0f, (width - sum - kGroupGap * static_cast<float>(groups.size() - 1)) /
                                         static_cast<float>(std::max(1, count - 1)));
    float x = origin.x;
    bool first = true;
    for (const auto& g : groups) {
        if (!first) x += kGroupGap; // on top of the knob gap already added after the last knob
        first = false;
        const float groupStart = x;
        float groupEnd = x;
        for (const auto& k : g.knobs) {
            place(k, {x, origin.y + 20.0f});
            groupEnd = x + (k.width > 0.0f ? k.width : emberKnobCellWidth(k.size));
            x = groupEnd + gap;
        }
        m_headers.push_back({NUIRect(groupStart, origin.y, groupEnd - groupStart, 12.0f), g.label});
    }
}

void EmberKnobBoard::setValueText(uint32_t param, const std::string& text) {
    for (auto& c : m_cells)
        if (c.spec.param == param) c.valueText = text;
}

void EmberKnobBoard::setEnabled(uint32_t param, bool enabled) {
    for (auto& c : m_cells)
        if (c.spec.param == param) c.enabled = enabled;
}

NUIRect EmberKnobBoard::rectOf(uint32_t param) const {
    for (const auto& c : m_cells)
        if (c.spec.param == param) return c.rect;
    return {};
}

void EmberKnobBoard::draw(NUIRenderer& renderer, const Aestra::Audio::IPluginInstance& plugin, const NUIPoint* mouse,
                          int dragParam, bool bypassed) const {
    for (const auto& h : m_headers) drawSectionHeader(renderer, h.span.x, h.span.y, h.span.width, h.label);
    for (const auto& c : m_cells) {
        if (c.spec.param == kNoParam) continue;
        ControlState state = ControlState::Idle;
        if (!c.enabled) state = ControlState::Disabled;
        else if (dragParam == static_cast<int>(c.spec.param)) state = ControlState::Drag;
        else if (mouse && c.rect.contains(*mouse)) state = ControlState::Hover;
        drawEmberKnob(renderer, {c.rect.x, c.rect.y}, c.spec.size, plugin.getParameter(c.spec.param), c.spec.label,
                      c.valueText.empty() ? plugin.getParameterDisplay(c.spec.param) : c.valueText, state, c.spec.bipolar,
                      bypassed);
    }
}

const std::vector<std::string>& noteDivisionLabels() {
    static const std::vector<std::string> kLabels = {"1/1", "1/2", "1/4",  "1/16", "1/8",  "1/32", "1/2D",
                                                     "1/4D", "1/8D", "1/16D", "1/4T", "1/8T", "1/16T"};
    return kLabels;
}

const std::vector<int>& noteDivisionMenuOrder() {
    static const std::vector<int> kOrder = {0, 1, 6, 2, 10, 7, 4, 11, 8, 3, 12, 9, 5};
    return kOrder;
}

// ---------------------------------------------------------------------------------------------------------------
// Segments
// ---------------------------------------------------------------------------------------------------------------

int EmberSegments::index(const Aestra::Audio::IPluginInstance& plugin) const {
    const int n = static_cast<int>(labels.size());
    if (n < 2) return 0;
    return static_cast<int>(std::lround(std::clamp(plugin.getParameter(param), 0.0f, 1.0f) * static_cast<float>(n - 1)));
}

int EmberSegments::hit(const NUIPoint& p) const {
    const int n = static_cast<int>(labels.size());
    if (n <= 0 || !rect.contains(p)) return -1;
    return std::clamp(static_cast<int>((p.x - rect.x) / (rect.width / static_cast<float>(n))), 0, n - 1);
}

bool EmberSegments::onPress(Aestra::Audio::IPluginInstance& plugin, const NUIPoint& p) const {
    const int i = hit(p);
    if (i < 0 || !enabled) return false;
    const int n = static_cast<int>(labels.size());
    plugin.setParameter(param, n > 1 ? static_cast<float>(i) / static_cast<float>(n - 1) : 0.0f);
    return true;
}

void EmberSegments::draw(NUIRenderer& renderer, const Aestra::Audio::IPluginInstance& plugin, const NUIPoint* mouse,
                         bool bypassed) const {
    drawSegmented(renderer, rect, labels.data(), static_cast<int>(labels.size()), index(plugin),
                  (mouse && enabled) ? hit(*mouse) : -1, bypassed, glyph);
}

// ---------------------------------------------------------------------------------------------------------------
// Slider
// ---------------------------------------------------------------------------------------------------------------

NUIRect EmberSliderControl::track() const {
    const auto g = emberSliderGeometry(rect, labelWidth);
    return NUIRect(g.x0 - 4.0f, rect.y, g.x1 - g.x0 + 8.0f, rect.height);
}

bool EmberSliderControl::onMouse(const NUIMouseEvent& event, Aestra::Audio::IPluginInstance& plugin) {
    const auto g = emberSliderGeometry(rect, labelWidth);
    auto toValue = [&](float x) { return std::clamp((x - g.x0) / (g.x1 - g.x0), 0.0f, 1.0f); };
    if (dragging && !event.pressed) {
        if (event.released) dragging = false;
        else plugin.setParameter(param, toValue(event.position.x));
        return true;
    }
    if (event.pressed && event.button == NUIMouseButton::Left && track().contains(event.position)) {
        if (event.doubleClick) {
            for (const auto& p : plugin.getParameters())
                if (p.id == param) plugin.setParameter(param, p.defaultValue);
        } else {
            dragging = true;
            plugin.setParameter(param, toValue(event.position.x));
        }
        return true;
    }
    return false;
}

void EmberSliderControl::draw(NUIRenderer& renderer, const Aestra::Audio::IPluginInstance& plugin,
                              const NUIPoint* mouse, bool bypassed) const {
    const ControlState state = dragging ? ControlState::Drag
                               : (mouse && track().contains(*mouse)) ? ControlState::Hover
                                                                     : ControlState::Idle;
    drawEmberSlider(renderer, rect, labelWidth, label, plugin.getParameter(param), plugin.getParameterDisplay(param),
                    state, bypassed);
}

// ---------------------------------------------------------------------------------------------------------------
// Dropdown
// ---------------------------------------------------------------------------------------------------------------

namespace {
constexpr float kMenuRowH = 22.0f;
}

int EmberDropdown::index(const Aestra::Audio::IPluginInstance& plugin) const {
    const int n = static_cast<int>(labels.size());
    if (n < 2) return 0;
    return static_cast<int>(std::lround(std::clamp(plugin.getParameter(param), 0.0f, 1.0f) * static_cast<float>(n - 1)));
}

NUIRect EmberDropdown::menuRect(const NUIRect& window) const {
    const float h = static_cast<float>(labels.size()) * kMenuRowH + 2.0f;
    float y = rect.bottom() + 2.0f;
    if (y + h > window.bottom() - 4.0f) y = rect.y - h - 2.0f; // flip above
    y = std::clamp(y, window.y + 4.0f, std::max(window.y + 4.0f, window.bottom() - h - 4.0f));
    return NUIRect(rect.x, y, rect.width, h);
}

void EmberDropdown::draw(NUIRenderer& renderer, const Aestra::Audio::IPluginInstance& plugin, const NUIPoint* mouse,
                         bool) const {
    const auto c = emberColors();
    const bool hover = enabled && mouse && rect.contains(*mouse);
    // Closed, a dropdown is a Field (sunk pill, no outline), sinking deeper while its list is open (as NUIDropdown).
    const float rad = Depth::Radius::pill(rect.height);
    Depth::bed(renderer, rect, rad, open ? Depth::Cut::Deep : Depth::Cut::Shallow);
    if (hover && !open) renderer.fillRoundedRect(rect, rad, NUIThemeManager::getInstance().getColor("hover"));
    const int i = index(plugin);
    drawTextLeft(renderer, labels.empty() ? std::string() : labels[static_cast<size_t>(i)], rect.x + 8.0f,
                 rect.y + rect.height * 0.5f, 12.0f, enabled ? c.ink : c.inkMuted);
    const float vx = rect.right() - 12.0f, vy = rect.y + rect.height * 0.5f;
    const NUIColor ink = enabled ? c.inkSoft : c.inkOff;
    renderer.drawLine({vx - 4.0f, vy - 2.0f}, {vx, vy + 2.0f}, 1.5f, ink);
    renderer.drawLine({vx, vy + 2.0f}, {vx + 4.0f, vy - 2.0f}, 1.5f, ink);
}

void EmberDropdown::drawMenu(NUIRenderer& renderer, const Aestra::Audio::IPluginInstance& plugin,
                             const NUIPoint* mouse, const NUIRect& window) const {
    if (!open) return;
    const auto c = emberColors();
    const NUIRect m = menuRect(window);
    // Design Spec, Menu: the one popup card, rows inset and rounded (row geometry is unchanged for hit testing).
    Depth::floatingCard(renderer, m, 10.0f, NUIThemeManager::getInstance().getColor("surfaceTertiary"));
    const int current = index(plugin);
    for (size_t row = 0; row < labels.size(); ++row) {
        const int li = order.empty() ? static_cast<int>(row) : order[row];
        const NUIRect r(m.x + 1.0f, m.y + 1.0f + static_cast<float>(row) * kMenuRowH, m.width - 2.0f, kMenuRowH);
        const bool on = li == current;
        const bool hot = mouse && r.contains(*mouse);
        const NUIRect inset(r.x + 3.0f, r.y, r.width - 6.0f, r.height); // never edge to edge
        if (on) renderer.fillRoundedRect(inset, 6.0f, c.selected);
        else if (hot) renderer.fillRoundedRect(inset, 6.0f, NUIThemeManager::getInstance().getColor("hover"));
        drawTextLeft(renderer, labels[static_cast<size_t>(li)], r.x + 9.0f, r.y + kMenuRowH * 0.5f, 12.0f,
                     (on || hot) ? c.ink : c.inkSoft);
        if (on) {
            const float cx = r.right() - 14.0f, cy = r.y + kMenuRowH * 0.5f;
            renderer.drawLine({cx - 3.0f, cy}, {cx - 1.0f, cy + 3.0f}, 1.5f, c.secondary);
            renderer.drawLine({cx - 1.0f, cy + 3.0f}, {cx + 4.0f, cy - 3.0f}, 1.5f, c.secondary);
        }
    }
}

bool EmberDropdown::onMouse(const NUIMouseEvent& event, Aestra::Audio::IPluginInstance& plugin,
                            const NUIRect& window) {
    const bool press = event.pressed && event.button == NUIMouseButton::Left;
    if (!press) return open && window.contains(event.position); // a move or release over an open menu is ours
    if (open) {
        const NUIRect m = menuRect(window);
        if (m.contains(event.position)) {
            const int row = std::clamp(static_cast<int>((event.position.y - m.y - 1.0f) / kMenuRowH), 0,
                                       static_cast<int>(labels.size()) - 1);
            const int li = order.empty() ? row : order[static_cast<size_t>(row)];
            const int n = static_cast<int>(labels.size());
            plugin.setParameter(param, n > 1 ? static_cast<float>(li) / static_cast<float>(n - 1) : 0.0f);
        }
        open = false;
        return true;
    }
    if (enabled && rect.contains(event.position)) {
        open = true;
        return true;
    }
    return false;
}

} // namespace EditorKit
} // namespace AestraUI
