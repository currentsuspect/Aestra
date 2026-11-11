// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "EmberControls.h"

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
    c.rect = NUIRect(topLeft.x, topLeft.y, emberKnobCellWidth(spec.size), emberKnobCellHeight(spec.size));
    m_cells.push_back(c);
}

void EmberKnobBoard::addHeader(const NUIRect& span, const char* label) { m_headers.push_back({span, label}); }

void EmberKnobBoard::layoutRow(const NUIPoint& origin, float width, const std::vector<Group>& groups) {
    constexpr float kGroupGap = 16.0f;
    float sum = 0.0f;
    int count = 0;
    for (const auto& g : groups)
        for (const auto& k : g.knobs) {
            sum += emberKnobCellWidth(k.size);
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
            groupEnd = x + emberKnobCellWidth(k.size);
            x = groupEnd + gap;
        }
        m_headers.push_back({NUIRect(groupStart, origin.y, groupEnd - groupStart, 12.0f), g.label});
    }
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
        ControlState state = ControlState::Idle;
        if (!c.enabled) state = ControlState::Disabled;
        else if (dragParam == static_cast<int>(c.spec.param)) state = ControlState::Drag;
        else if (mouse && c.rect.contains(*mouse)) state = ControlState::Hover;
        drawEmberKnob(renderer, {c.rect.x, c.rect.y}, c.spec.size, plugin.getParameter(c.spec.param), c.spec.label,
                      plugin.getParameterDisplay(c.spec.param), state, c.spec.bipolar, bypassed);
    }
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
                  (mouse && enabled) ? hit(*mouse) : -1, bypassed);
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

} // namespace EditorKit
} // namespace AestraUI
