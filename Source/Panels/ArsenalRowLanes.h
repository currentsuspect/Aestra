// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ArsenalRackLayout.h"
#include "ArsenalSoundStrip.h"
#include "ArsenalStepLanes.h"
#include "../AestraUI/Widgets/UnitRow.h"

#include <memory>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace Aestra {

/**
 * @brief Which Arsenal rows are open, and the glue between them and the lanes (V81-AR1, FD-27 point 4): the small
 * caret on each row, the extra height an open row adds to the rack, and the pointer routing for the lanes.
 *
 * The panel owns one of these and calls it from layout, render and pointer handling, so the lane rules and the
 * widget wiring stay out of the (size-pinned) panel file.
 */
class ArsenalRowLanes {
public:
    using RowPtr = std::shared_ptr<AestraUI::UnitRow>;

    /// Height an open row adds under itself for its unit's lanes (0 when closed or the unit is unknown).
    float extraFor(Audio::TrackManager& tm, Audio::UnitID unit) const {
        if (!m_open.count(unit)) return 0.0f;
        const auto* u = tm.getUnitManager().getUnit(unit);
        return u ? ArsenalStepLanes::heightFor(ArsenalStepLanes::lanesFor(u->type).size()) + soundOffset(u->type) : 0.0f;
    }

    /// Called with (unit, path) when a neighbouring sound is chosen; the panel routes it to the gap-free loader.
    void setOnSwap(std::function<void(Audio::UnitID, const std::string&)> cb) { m_onSwap = std::move(cb); }

    /// Carets on every visible row and the lanes under the open ones.
    void draw(AestraUI::NUIRenderer& r, const ArsenalRackLayout& rack, const std::vector<RowPtr>& rows, Audio::TrackManager* tm,
              Audio::PatternID pattern, const AestraUI::NUIRect& viewport) const {
        if (!tm || viewport.height <= 0.0f) return;
        auto& theme = AestraUI::NUIThemeManager::getInstance();
        r.setClipRect(viewport);
        for (const auto& item : rack.items) {
            if (item.kind != ArsenalRackItem::Kind::Row || !rows[item.rowIndex]) continue;
            const auto& row = *rows[item.rowIndex];
            const bool open = m_open.count(row.getUnitId()) > 0;
            const auto c = caretRect(row);
            const float cx = c.x + c.width * 0.5f, cy = c.y + c.height * 0.5f;
            const auto col = theme.getColor("textSecondary").withAlpha(open ? 0.95f : 0.55f);
            const float d = open ? -2.5f : 2.5f; // open points up
            r.drawLine({cx - 4.0f, cy - d}, {cx, cy + d}, 1.5f, col);
            r.drawLine({cx, cy + d}, {cx + 4.0f, cy - d}, 1.5f, col);
            if (open && item.extraH > 0.0f) {
                const auto ctx = contextFor(*tm, pattern, row, item.extraH);
                if (hasSound(*tm, row.getUnitId())) {
                    auto& strip = m_sounds[row.getUnitId()];
                    strip.follow(tm->getUnitManager().getUnit(row.getUnitId())->audioClipPath);
                    strip.draw(r, soundRect(row));
                }
                m_lanes.draw(r, ctx);
            }
        }
        r.clearClipRect();
    }

    /// Pointer routing. Returns true when the event was a caret or a lane gesture; sets @p relayout when a row opened or closed and @p edited when a lane wrote a value.
    bool onMouse(const AestraUI::NUIMouseEvent& e, const ArsenalRackLayout& rack, const std::vector<RowPtr>& rows,
                 Audio::TrackManager* tm, Audio::PatternID pattern, const AestraUI::NUIRect& viewport, bool& relayout, bool& edited) {
        if (!tm) return false;
        if (m_lanes.dragging()) {
            if (const auto* item = itemFor(rack, m_dragRow); item && rows[m_dragRow]) {
                const auto ctx = contextFor(*tm, pattern, *rows[m_dragRow], item->extraH);
                if (!e.pressed && e.button == AestraUI::NUIMouseButton::Left) m_lanes.release(ctx);
                else m_lanes.drag(ctx, e.position);
                edited = m_lanes.takeEdited();
                return true;
            }
            m_lanes.release({});
            return false;
        }
        if (!e.pressed || e.button != AestraUI::NUIMouseButton::Left) return false;
        if (viewport.height > 0.0f && !viewport.contains(e.position)) return false;
        for (const auto& item : rack.items) {
            if (item.kind != ArsenalRackItem::Kind::Row || !rows[item.rowIndex]) continue;
            const auto& row = *rows[item.rowIndex];
            if (caretRect(row).contains(e.position)) {
                const auto id = row.getUnitId();
                if (!m_open.erase(id)) m_open.insert(id);
                relayout = true;
                return true;
            }
            if (item.extraH > 0.0f && m_open.count(row.getUnitId()) && hasSound(*tm, row.getUnitId()) && soundRect(row).contains(e.position)) {
                auto& strip = m_sounds[row.getUnitId()];
                const auto hit = strip.hitTest(soundRect(row), e.position);
                const std::string next = hit == ArsenalSoundStrip::Hit::None ? std::string() : strip.neighbour(hit == ArsenalSoundStrip::Hit::Next ? 1 : -1);
                if (!next.empty() && m_onSwap) m_onSwap(row.getUnitId(), next);
                return true;
            }
            if (item.extraH > 0.0f && m_open.count(row.getUnitId()) &&
                m_lanes.press(contextFor(*tm, pattern, row, item.extraH), e.position)) {
                m_dragRow = item.rowIndex;
                edited = m_lanes.takeEdited();
                return true;
            }
        }
        return false;
    }

private:
    /// Samplers get a sound strip above their lanes.
    static float soundOffset(Audio::UnitType t) {
        return (t == Audio::UnitType::Sampler || t == Audio::UnitType::PitchedSampler) ? ArsenalSoundStrip::kHeight + ArsenalStepLanes::kLaneGap : 0.0f;
    }
    static bool hasSound(Audio::TrackManager& tm, Audio::UnitID id) {
        const auto* u = tm.getUnitManager().getUnit(id);
        return u && soundOffset(u->type) > 0.0f;
    }
    static AestraUI::NUIRect soundRect(const AestraUI::UnitRow& row) {
        const auto b = row.getBounds();
        return {b.x, b.bottom() + 2.0f, b.width, ArsenalSoundStrip::kHeight};
    }
    static AestraUI::NUIRect caretRect(const AestraUI::UnitRow& row) {
        const auto b = row.getBounds();
        return {b.x + 6.0f, b.y + b.height - 16.0f, 22.0f, 14.0f};
    }
    static const ArsenalRackItem* itemFor(const ArsenalRackLayout& rack, size_t rowIndex) {
        for (const auto& i : rack.items)
            if (i.kind == ArsenalRackItem::Kind::Row && i.rowIndex == rowIndex) return &i;
        return nullptr;
    }
    static ArsenalStepLanes::Context contextFor(Audio::TrackManager& tm, Audio::PatternID pattern, const AestraUI::UnitRow& row, float extraH) {
        ArsenalStepLanes::Context c;
        const auto b = row.getBounds();
        c.tm = &tm;
        c.pattern = pattern;
        c.unit = row.getUnitId();
        if (const auto* u = tm.getUnitManager().getUnit(c.unit)) c.type = u->type;
        c.root = tm.getUnitManager().getUnitRootMidiNote(c.unit);
        c.steps = row.getStepCount();
        const float off = soundOffset(c.type);
        c.area = {b.x, b.bottom() + 2.0f + off, b.width, std::max(0.0f, extraH - 2.0f - off)};
        c.strip = row.stepStrip();
        c.stepW = row.stepPadWidth();
        c.groupGap = AestraUI::UnitRow::stepGroupGap();
        c.scrollX = row.getScrollX();
        c.accent = AestraUI::NUIThemeManager::getInstance().getColor("accentPrimary");
        return c;
    }

    std::set<Audio::UnitID> m_open;
    mutable std::map<Audio::UnitID, ArsenalSoundStrip> m_sounds;
    std::function<void(Audio::UnitID, const std::string&)> m_onSwap;
    mutable ArsenalStepLanes m_lanes;
    size_t m_dragRow{0};
};

} // namespace Aestra
