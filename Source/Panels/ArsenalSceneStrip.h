// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Models/PatternManager.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace Aestra {

/**
 * @brief The Arsenal's pattern scenes (V81-AR1, FD-27 point 1): every MIDI pattern is a chip, one click plays
 * it, and "+" makes the next one.
 *
 * Placement is pure (`place`): chips keep their widths, the active one is kept in view when they overflow, and
 * the wheel scrolls the rest. The panel only owns an instance, feeds it the pattern list and forwards the
 * pointer, so the panel file stays out of this rule.
 */
class ArsenalSceneStrip {
public:
    static constexpr float kHeight = 28.0f;
    static constexpr float kGap = 6.0f;
    static constexpr float kAddW = 28.0f;
    static constexpr float kSwingW = 104.0f; ///< the swing drag zone at the right end

    struct Scene {
        uint64_t id{0};
        std::string name;
    };
    struct Placed {
        size_t index{0};
        float x{0.0f}; ///< relative to the strip's left edge
        float w{0.0f};
    };
    struct Hit {
        enum class Kind { None, Scene, Add, Swing } kind{Kind::None};
        uint64_t id{0};
    };
    /// What a pointer event asks the panel to do (the strip changes swing itself).
    struct Action {
        enum class Kind { None, Select, PatternAdded, SwingEdited } kind{Kind::None};
        uint64_t id{0};
    };

    /// A chip is as wide as its name needs, within sane bounds (no renderer needed, so it is testable).
    static float chipWidth(const std::string& name) {
        return std::clamp(24.0f + 6.4f * static_cast<float>(name.size()), 56.0f, 140.0f);
    }

    /// Chip positions along the strip, scrolled by `offset` (clamped into `outOffset`), the active chip in view.
    static std::vector<Placed> place(const std::vector<float>& widths, int activeIndex, float availW, float offset,
                                     bool followActive, float& outOffset) {
        std::vector<Placed> out;
        float x = 0.0f;
        for (size_t i = 0; i < widths.size(); ++i) {
            out.push_back({i, x, widths[i]});
            x += widths[i] + kGap;
        }
        const float total = std::max(0.0f, x - kGap);
        const float viewW = std::max(0.0f, availW - kAddW - kGap);
        const float maxOff = std::max(0.0f, total - viewW);
        if (followActive && activeIndex >= 0 && activeIndex < static_cast<int>(out.size())) {
            const auto& a = out[static_cast<size_t>(activeIndex)];
            if (a.x < offset) offset = a.x;
            if (a.x + a.w > offset + viewW) offset = a.x + a.w - viewW;
        }
        outOffset = std::clamp(offset, 0.0f, maxOff);
        for (auto& p : out) p.x -= outOffset;
        return out;
    }

    /// "Pattern N" that no existing scene already uses.
    static std::string nextName(const std::vector<Scene>& scenes) {
        for (size_t n = scenes.size() + 1;; ++n) {
            const std::string name = "Pattern " + std::to_string(n);
            if (std::none_of(scenes.begin(), scenes.end(), [&](const Scene& s) { return s.name == name; })) return name;
        }
    }

    /// Re-read the patterns (cheap; called per frame so renames, adds and deletes in the browser just show up).
    void refresh(const Audio::PatternManager& pm, uint64_t activeId, const AestraUI::NUIRect& rect) {
        m_rect = rect;
        m_scenes.clear();
        for (const auto& p : pm.getAllPatterns()) {
            if (p && p->isMidi()) m_scenes.push_back({p->id.value, p->name.empty() ? "Pattern" : p->name});
        }
        std::sort(m_scenes.begin(), m_scenes.end(), [](const Scene& a, const Scene& b) { return a.id < b.id; });
        std::vector<float> widths;
        int active = -1;
        for (size_t i = 0; i < m_scenes.size(); ++i) {
            widths.push_back(chipWidth(m_scenes[i].name));
            if (m_scenes[i].id == activeId) active = static_cast<int>(i);
        }
        m_followActive = m_followActive || activeId != m_lastActive;
        m_lastActive = activeId;
        m_placed = place(widths, active, rect.width - kSwingW - kGap, m_offset, m_followActive, m_offset);
        const auto* ap = pm.getPattern(Audio::PatternID{activeId});
        m_swing = ap ? ap->swing : 0.0f;
        m_followActive = false;
        m_activeId = activeId;
    }

    const std::vector<Scene>& scenes() const { return m_scenes; }
    const std::vector<Placed>& placed() const { return m_placed; }
    AestraUI::NUIRect addRect() const { return {m_rect.right() - kSwingW - kGap - kAddW, m_rect.y, kAddW, m_rect.height}; }
    AestraUI::NUIRect swingRect() const { return {m_rect.right() - kSwingW, m_rect.y, kSwingW, m_rect.height}; }
    float swing() const { return m_swing; }
    /// 50% (straight) to 75% (half a step late), the way drum machines say it.
    static int swingPercent(float swing) { return 50 + static_cast<int>(std::lround(std::clamp(swing, 0.0f, 1.0f) * 25.0f)); }
    AestraUI::NUIRect chipRect(const Placed& p) const { return {m_rect.x + p.x, m_rect.y, p.w, m_rect.height}; }
    void scrollBy(float dx) { m_offset += dx; m_followActive = false; }

    Hit hitTest(const AestraUI::NUIPoint& pt) const {
        if (!m_rect.contains(pt)) return {};
        if (swingRect().contains(pt)) return {Hit::Kind::Swing, 0};
        if (addRect().contains(pt)) return {Hit::Kind::Add, 0};
        if (pt.x >= addRect().x - kGap) return {};
        for (const auto& p : m_placed) {
            if (chipRect(p).contains(pt)) return {Hit::Kind::Scene, m_scenes[p.index].id};
        }
        return {};
    }

    void draw(AestraUI::NUIRenderer& renderer) const {
        if (m_rect.width <= 0.0f) return;
        auto& theme = AestraUI::NUIThemeManager::getInstance();
        const auto& tp = theme.getCurrentTheme();
        const auto accent = theme.getColor("accentPrimary");
        const AestraUI::NUIRect view{m_rect.x, m_rect.y, std::max(0.0f, m_rect.width - kSwingW - kGap - kAddW - kGap), m_rect.height};
        renderer.setClipRect(view);
        for (const auto& p : m_placed) {
            const auto r = chipRect(p);
            const bool on = m_scenes[p.index].id == m_activeId;
            renderer.fillRoundedRect(r, tp.radiusXS, on ? accent.withAlpha(0.20f) : theme.getColor("surfaceTertiary").withAlpha(0.85f));
            renderer.strokeRoundedRect(r, tp.radiusXS, 1.0f, on ? accent.withAlpha(0.8f) : theme.getColor("borderSubtle").withAlpha(0.5f));
            renderer.setClipRect(AestraUI::intersectRects(view, r));
            renderer.drawTextCentered(m_scenes[p.index].name, r, tp.fontSizeXS,
                                      on ? accent : theme.getColor("textSecondary").withAlpha(0.9f));
            renderer.setClipRect(view);
        }
        renderer.clearClipRect();
        const auto add = addRect();
        renderer.fillRoundedRect(add, tp.radiusXS, theme.getColor("surfaceTertiary").withAlpha(0.85f));
        renderer.strokeRoundedRect(add, tp.radiusXS, 1.0f, theme.getColor("borderSubtle").withAlpha(0.5f));
        renderer.drawTextCentered("+", add, tp.fontSizeS, theme.getColor("textPrimary").withAlpha(0.9f));
        const auto sw = swingRect();
        renderer.fillRoundedRect(sw, tp.radiusXS, theme.getColor("surfaceTertiary").withAlpha(0.85f));
        renderer.fillRoundedRect({sw.x, sw.y, sw.width * m_swing, sw.height}, tp.radiusXS, accent.withAlpha(0.22f));
        renderer.strokeRoundedRect(sw, tp.radiusXS, 1.0f, theme.getColor("borderSubtle").withAlpha(0.5f));
        renderer.drawTextCentered("SWING " + std::to_string(swingPercent(m_swing)) + "%", sw, tp.fontSizeMicro, theme.getColor("textPrimary").withAlpha(0.9f));
    }

    /// Pointer routing for the strip: chips select, "+" makes the next pattern, the swing zone drags, the wheel scrolls.
    /// Returns true when the strip took the event.
    bool onMouse(const AestraUI::NUIMouseEvent& e, Audio::PatternManager& pm, uint64_t activeId, Action& out) {
        const bool left = e.button == AestraUI::NUIMouseButton::Left;
        if (m_swingDrag) {
            setSwing(pm, activeId, e.position.x);
            out = {Action::Kind::SwingEdited, activeId};
            if (!e.pressed && left) m_swingDrag = false;
            return true;
        }
        if (!m_rect.contains(e.position)) return false;
        if (std::abs(e.wheelDelta) > 0.001f) { scrollBy(-e.wheelDelta * 40.0f); return true; }
        if (!e.pressed || !left) return true;
        const Hit hit = hitTest(e.position);
        if (hit.kind == Hit::Kind::Scene) out = {Action::Kind::Select, hit.id};
        if (hit.kind == Hit::Kind::Add) out = {Action::Kind::PatternAdded, pm.createMidiPattern(nextName(m_scenes), 8.0, {}).value};
        if (hit.kind == Hit::Kind::Swing) { m_swingDrag = true; setSwing(pm, activeId, e.position.x); out = {Action::Kind::SwingEdited, activeId}; }
        return true;
    }

private:
    AestraUI::NUIRect m_rect{};
    std::vector<Scene> m_scenes;
    std::vector<Placed> m_placed;
    void setSwing(Audio::PatternManager& pm, uint64_t id, float x) {
        const auto r = swingRect();
        m_swing = std::clamp((x - r.x) / r.width, 0.0f, 1.0f);
        m_swing = std::round(m_swing * 100.0f) / 100.0f;
        pm.applyPatch(Audio::PatternID{id}, [this](Audio::PatternSource& p) { p.swing = m_swing; });
    }
    bool m_swingDrag{false};
    float m_swing{0.0f};
    uint64_t m_activeId{0};
    uint64_t m_lastActive{0};
    float m_offset{0.0f};
    bool m_followActive{true};
};

} // namespace Aestra
