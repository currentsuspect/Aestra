// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Models/PatternManager.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "NUIDoubleClick.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
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
    static constexpr float kHeight = 44.0f;  ///< a scene card (the mock: 132 x 44)
    static constexpr float kCardW = 132.0f;
    static constexpr float kGap = 8.0f;
    static constexpr float kAddW = 44.0f;
    static constexpr float kSwingW = 166.0f; ///< bounded 0–100% swing slider
    static constexpr float kPlaceW = 156.0f; ///< the "Place in timeline" button at the right end
    static constexpr float kRightW = kSwingW + kGap + kPlaceW;
    static constexpr size_t kThumbBars = 16;

    struct Scene {
        uint64_t id{0};
        std::string name;
        std::vector<float> thumb; ///< kThumbBars bars, 0..1: how busy each sixteenth of the pattern is
    };
    struct Placed {
        size_t index{0};
        float x{0.0f}; ///< relative to the strip's left edge
        float w{0.0f};
    };
    struct Hit {
        enum class Kind { None, Scene, Add, Swing, Place } kind{Kind::None};
        uint64_t id{0};
    };
    /// What a pointer event asks the panel to do (the strip changes swing itself).
    struct Action {
        enum class Kind { None, Select, PatternAdded, SwingEdited, PlaceInTimeline, Menu } kind{Kind::None};
        uint64_t id{0};
        AestraUI::NUIPoint at{};
        AestraUI::NUIModifiers modifiers{AestraUI::NUIModifiers::None};
        float newSwing{0.0f};
        float oldSwing{0.0f};
        std::vector<std::pair<uint64_t, std::optional<float>>> oldUnitSwing;
        bool swingCommit{false}; ///< Menu: where the right-click landed
    };

    /// Every scene card is the same width, so a row of scenes reads as a row of cards, whatever they are called.
    static float chipWidth(const std::string&) { return kCardW; }

    /// How busy each sixteenth of a pattern is: note starts per bucket, scaled so the busiest bucket is full height.
    static std::vector<float> thumbnail(const std::vector<Audio::MidiNote>& notes, double lengthBeats) {
        std::vector<float> bars(kThumbBars, 0.0f);
        if (lengthBeats <= 0.0) return bars;
        for (const auto& n : notes) {
            const size_t b = std::min(kThumbBars - 1, static_cast<size_t>(std::max(0.0, n.startBeat) / lengthBeats * kThumbBars));
            bars[b] += 1.0f;
        }
        const float most = *std::max_element(bars.begin(), bars.end());
        if (most > 0.0f) for (auto& v : bars) if (v > 0.0f) v = std::max(0.2f, v / most);
        return bars;
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

    /// Every unit is born with its own pattern ("Kick Pattern"). Until it holds notes or sits in the timeline it
    /// is plumbing, not an idea, so the strip leaves it out (FD-27: patterns are the person's scenes). A kit of eight
    /// sounds would otherwise bring eight empty cards. Templated on the track manager to keep this header light.
    template <class TrackManagerT>
    static std::vector<uint64_t> quietDefaults(const TrackManagerT& tm) {
        std::vector<uint64_t> placed, quiet;
        tm.getPlaylistModel().withLanes([&](const auto& lanes) {
            for (const auto& lane : lanes)
                for (const auto& c : lane.clips) placed.push_back(c.patternId.value);
            return 0;
        });
        const auto& units = tm.getUnitManager();
        for (const auto id : units.getAllUnitIDs()) {
            const auto* u = units.getUnit(id);
            if (!u || !u->defaultPatternId.isValid()) continue;
            const auto* p = tm.getPatternManager().getPattern(u->defaultPatternId);
            if (!p || !p->isMidi() || !std::get<Audio::MidiPayload>(p->payload).notes.empty()) continue;
            if (std::find(placed.begin(), placed.end(), p->id.value) == placed.end()) quiet.push_back(p->id.value);
        }
        return quiet;
    }

    /// Re-read the patterns (cheap; called per frame so renames, adds and deletes in the browser just show up).
    /// @p hidden: patterns to leave out (see quietDefaults); the active pattern always shows.
    void refresh(const Audio::PatternManager& pm, uint64_t activeId, const AestraUI::NUIRect& rect, const std::vector<uint64_t>& hidden = {}) {
        m_rect = rect;
        m_scenes.clear();
        for (const auto& p : pm.getAllPatterns()) {
            if (p && p->isMidi() && p->id.value != activeId && std::find(hidden.begin(), hidden.end(), p->id.value) != hidden.end()) continue;
            if (p && p->isMidi()) m_scenes.push_back({p->id.value, p->name.empty() ? "Pattern" : p->name, thumbnail(std::get<Audio::MidiPayload>(p->payload).notes, p->lengthBeats)});
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
        m_placed = place(widths, active, rect.width - kRightW - kGap, m_offset, m_followActive, m_offset);
        const auto* ap = pm.getPattern(Audio::PatternID{activeId});
        m_swingMixed = false;
        m_swing = ap ? ap->swing : 0.0f;
        if (ap && !m_swingTargets.empty()) {
            bool first = true;
            float firstValue = 0.0f;
            for (const uint64_t unitId : m_swingTargets) {
                const float value = ap->swingForUnit(unitId);
                if (first) { firstValue = value; first = false; }
                else if (std::abs(value - firstValue) > 0.001f) m_swingMixed = true;
            }
            if (!first) m_swing = firstValue;
        }
        m_followActive = false;
        m_activeId = activeId;
    }

    const std::vector<Scene>& scenes() const { return m_scenes; }
    void setSelectedPatterns(const std::vector<uint64_t>& ids) { m_selectedPatterns = ids; }
    void setSwingTargets(const std::vector<uint64_t>& ids) { m_swingTargets = ids; }
    const std::vector<Placed>& placed() const { return m_placed; }
    /// The "+" slot follows the last card (the mock), and stops at the swing zone when the cards run out of room.
    AestraUI::NUIRect addRect() const {
        const float limit = m_rect.right() - kRightW - kGap - kAddW;
        const float after = m_placed.empty() ? m_rect.x : m_rect.x + m_placed.back().x + m_placed.back().w + kGap;
        return {std::min(limit, after), m_rect.y, kAddW, m_rect.height};
    }
    AestraUI::NUIRect swingRect() const { return {m_rect.right() - kRightW, m_rect.y, kSwingW, m_rect.height}; }
    AestraUI::NUIRect swingTrackRect() const {
        const auto sw = swingRect();
        return {sw.x + 2.0f, sw.y + 15.0f, std::max(1.0f, sw.width - 4.0f), 20.0f};
    }
    AestraUI::NUIRect placeRect() const { return {m_rect.right() - kPlaceW, m_rect.y + (m_rect.height - 34.0f) * 0.5f, kPlaceW, 34.0f}; }
    float swing() const { return m_swing; }
    /// How much swing, 0 to 100% (100% = every odd sixteenth half a step late), as the mock shows it.
    static int swingPercent(float swing) { return static_cast<int>(std::lround(std::clamp(swing, 0.0f, 1.0f) * 100.0f)); }
    /// Where a scene's chip is right now (the inline rename sits exactly on it); empty when it is scrolled out.
    AestraUI::NUIRect chipRectOf(uint64_t id) const {
        for (const auto& p : m_placed) if (m_scenes[p.index].id == id) return chipRect(p);
        return {};
    }
    AestraUI::NUIRect chipRect(const Placed& p) const { return {m_rect.x + p.x, m_rect.y, p.w, m_rect.height}; }
    void scrollBy(float dx) { m_offset += dx; m_followActive = false; }

    Hit hitTest(const AestraUI::NUIPoint& pt) const {
        if (!m_rect.contains(pt)) return {};
        if (placeRect().contains(pt)) return {Hit::Kind::Place, 0};
        if (swingRect().contains(pt)) return {Hit::Kind::Swing, 0};
        if (addRect().contains(pt)) return {Hit::Kind::Add, 0};
        if (pt.x >= addRect().x - kGap) return {};
        for (const auto& p : m_placed) {
            if (chipRect(p).contains(pt)) return {Hit::Kind::Scene, m_scenes[p.index].id};
        }
        return {};
    }

    /// A name that fits its card, cut with an ellipsis instead of mid-letter.
    static std::string fitName(AestraUI::NUIRenderer& renderer, std::string name, float font, float room) {
        if (renderer.measureText(name, font).width <= room) return name;
        while (name.size() > 1 && renderer.measureText(name + "\xE2\x80\xA6", font).width > room) name.pop_back();
        return name + "\xE2\x80\xA6";
    }

    void draw(AestraUI::NUIRenderer& renderer) const {
        if (m_rect.width <= 0.0f) return;
        auto& theme = AestraUI::NUIThemeManager::getInstance();
        const auto& tp = theme.getCurrentTheme();
        const AestraUI::NUIRect view{m_rect.x, m_rect.y, std::max(0.0f, addRect().x - kGap - m_rect.x), m_rect.height};
        renderer.setClipRect(view);
        for (const auto& p : m_placed) {
            const auto r = chipRect(p);
            const float tileRadius = 8.0f; // the board's pattern tile
            const auto& scene = m_scenes[p.index];
            const bool selected = std::find(m_selectedPatterns.begin(), m_selectedPatterns.end(), scene.id) != m_selectedPatterns.end();
            renderer.fillRoundedRect(r, tileRadius, selected ? AestraUI::NUIColor::lerp(theme.getColor("backgroundSecondary"), theme.getColor("accentPrimary"), 0.32f) : theme.getColor("backgroundSecondary"));
            renderer.strokeRoundedRect(r, tileRadius, 1.0f, selected ? theme.getColor("accentPrimary").withAlpha(0.55f) : theme.getColor("borderSubtle").withAlpha(0.7f));
            renderer.setClipRect(AestraUI::intersectRects(view, r));
            renderer.drawText(fitName(renderer, scene.name, tp.fontSizeXS + 1.0f, r.width - 24.0f), {r.x + 12.0f, r.y + 8.0f}, tp.fontSizeXS + 1.0f, theme.getColor("textPrimary").withAlpha(selected ? 1.0f : 0.9f));
            const float tw = r.width - 24.0f, bw = tw / static_cast<float>(kThumbBars), maxH = 8.0f;
            for (size_t i = 0; i < scene.thumb.size(); ++i) {
                const float h = scene.thumb[i] > 0.0f ? std::max(1.0f, maxH * scene.thumb[i]) : 1.0f;
                renderer.fillRect({r.x + 12.0f + static_cast<float>(i) * bw, r.bottom() - 9.0f - h, std::max(1.0f, bw - 1.0f), h},
                                  theme.getColor(selected ? "textSecondary" : "textDisabled").withAlpha(scene.thumb[i] > 0.0f ? 0.9f : 0.35f));
            }
            renderer.setClipRect(view);
        }
        renderer.clearClipRect();
        const auto add = addRect();
        renderer.fillRoundedRect(add, tp.radiusXS, theme.getColor("backgroundPrimary"));
        renderer.strokeRoundedRect(add, tp.radiusXS, 1.0f, theme.getColor("borderSubtle").withAlpha(0.7f));
        renderer.drawTextCentered("+", add, tp.fontSizeM, theme.getColor("textSecondary").withAlpha(0.9f));
        // A bounded slider makes the full 0–100% range visible and the current value easy to place.
        const auto sw = swingRect();
        const auto track = swingTrackRect();
        const float fillWidth = track.width * std::clamp(m_swing, 0.0f, 1.0f);
        renderer.fillRoundedRect(track, track.height * 0.5f, theme.getColor("backgroundPrimary"));
        if (fillWidth > 0.0f) {
            renderer.fillRoundedRect({track.x, track.y, fillWidth, track.height},
                                     std::min(track.height * 0.5f, fillWidth * 0.5f),
                                     theme.getColor("accentPrimary"));
        }
        renderer.strokeRoundedRect(track, track.height * 0.5f, 1.0f, theme.getColor("borderSubtle"));
        renderer.drawTextCentered("SWING", {sw.x, sw.y + 1.0f, sw.width, 12.0f}, tp.fontSizeMicro,
                                  theme.getColor("textMuted"));
        renderer.drawTextCentered(m_swingMixed ? "MIX" : (std::to_string(swingPercent(m_swing)) + "%"),
                                  track, tp.fontSizeXS, theme.getColor("textPrimary"));
        const auto pl = placeRect(); // the one primary action of the header (the mock's purple button)
        renderer.fillRoundedRect(pl, tp.radiusXS, theme.getColor("accentPrimary"));
        const auto buttonText = theme.getContrastColor(theme.getColor("accentPrimary"));
        const float labelFont = tp.fontSizeXS + 1.0f, arrowW = 11.0f, arrowGap = 8.0f;
        const float labelW = renderer.measureText("Place in timeline", labelFont).width;
        const float startX = pl.x + (pl.width - (labelW + arrowGap + arrowW)) * 0.5f, midY = pl.y + pl.height * 0.5f;
        renderer.drawText("Place in timeline", {startX, midY - renderer.getFontMetrics(labelFont).lineHeight * 0.5f}, labelFont, buttonText);
        // The board's trailing arrow: it sends the pattern on to the timeline.
        const float ax = startX + labelW + arrowGap;
        renderer.drawLine({ax, midY}, {ax + arrowW, midY}, 1.5f, buttonText);
        renderer.drawLine({ax + arrowW - 4.0f, midY - 4.0f}, {ax + arrowW, midY}, 1.5f, buttonText);
        renderer.drawLine({ax + arrowW - 4.0f, midY + 4.0f}, {ax + arrowW, midY}, 1.5f, buttonText);
    }

    /// Pointer routing for the strip: chips select, "+" makes the next pattern, the swing zone drags, the wheel scrolls.
    /// Returns true when the strip took the event.
    bool onMouse(const AestraUI::NUIMouseEvent& e, Audio::PatternManager& pm, uint64_t activeId, Action& out) {
        const bool left = e.button == AestraUI::NUIMouseButton::Left;
        if (m_swingDrag) {
            setSwing(pm, activeId, e.position.x);
            out = {Action::Kind::SwingEdited, activeId};
            out.newSwing = m_swing;
            out.oldSwing = m_dragOldSwing;
            out.oldUnitSwing = m_dragOldUnitSwing;
            if (!e.pressed && left) {
                m_swingDrag = false;
                out.swingCommit = true;
            }
            return true;
        }
        if (!m_rect.contains(e.position)) return false;
        if (std::abs(e.wheelDelta) > 0.001f) { scrollBy(-e.wheelDelta * 40.0f); return true; }
        if (e.pressed && e.button == AestraUI::NUIMouseButton::Right) { // a chip's menu (V82-AR1, FD-52)
            if (const Hit h = hitTest(e.position); h.kind == Hit::Kind::Scene) {
                out = {Action::Kind::Menu, h.id, e.position};
                out.modifiers = e.modifiers;
            }
            return true;
        }
        if (!e.pressed || !left) return true;
        const Hit hit = hitTest(e.position);
        if (hit.kind == Hit::Kind::Scene) {
            out = {Action::Kind::Select, hit.id};
            out.modifiers = e.modifiers;
        }
        if (hit.kind == Hit::Kind::Add) out = {Action::Kind::PatternAdded, pm.createMidiPattern(nextName(m_scenes), 8.0, {}).value};
        if (hit.kind == Hit::Kind::Place) out = {Action::Kind::PlaceInTimeline, activeId};
        if (hit.kind == Hit::Kind::Swing) {
            captureSwingState(pm, activeId);
            if (m_swingClickTracker.registerPress() || e.doubleClick) {
                setSwingValue(pm, activeId, 0.0f);
                out = {Action::Kind::SwingEdited, activeId};
                out.newSwing = 0.0f;
                out.oldSwing = m_dragOldSwing;
                out.oldUnitSwing = m_dragOldUnitSwing;
                out.swingCommit = true;
            } else {
                m_swingDrag = true;
                setSwing(pm, activeId, e.position.x);
                out = {Action::Kind::SwingEdited, activeId};
                out.newSwing = m_swing;
                out.oldSwing = m_dragOldSwing;
                out.oldUnitSwing = m_dragOldUnitSwing;
            }
        }
        return true;
    }

private:
    AestraUI::NUIRect m_rect{};
    std::vector<Scene> m_scenes;
    std::vector<Placed> m_placed;
    std::vector<uint64_t> m_selectedPatterns;
    std::vector<uint64_t> m_swingTargets;
    float m_dragOldSwing{0.0f};
    std::vector<std::pair<uint64_t, std::optional<float>>> m_dragOldUnitSwing;
    bool m_swingMixed{false};

    void captureSwingState(Audio::PatternManager& pm, uint64_t id) {
        m_dragOldSwing = 0.0f;
        m_dragOldUnitSwing.clear();
        if (const auto* pattern = pm.getPattern(Audio::PatternID{id})) {
            m_dragOldSwing = pattern->swing;
            m_dragOldUnitSwing.reserve(m_swingTargets.size());
            for (const uint64_t unitId : m_swingTargets)
                m_dragOldUnitSwing.emplace_back(unitId, pattern->unitSwingOverride(unitId));
        }
    }

    void setSwingValue(Audio::PatternManager& pm, uint64_t id, float value) {
        m_swing = std::round(std::clamp(value, 0.0f, 1.0f) * 100.0f) / 100.0f;
        pm.applyPatch(Audio::PatternID{id}, [this](Audio::PatternSource& p) {
            if (m_swingTargets.empty()) p.swing = m_swing;
            else for (const uint64_t unitId : m_swingTargets) p.setUnitSwing(unitId, m_swing);
        });
        m_swingMixed = false;
    }

    void setSwing(Audio::PatternManager& pm, uint64_t id, float x) {
        const auto r = swingTrackRect();
        setSwingValue(pm, id, (x - r.x) / r.width);
    }

    bool m_swingDrag{false};
    AestraUI::NUIDoubleClickTracker m_swingClickTracker;
    float m_swing{0.0f};
    uint64_t m_activeId{0};
    uint64_t m_lastActive{0};
    float m_offset{0.0f};
    bool m_followActive{true};
};

} // namespace Aestra
