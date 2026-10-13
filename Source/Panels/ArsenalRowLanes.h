// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ArsenalRackLayout.h"
#include "ArsenalSoundDrawer.h"
#include "ArsenalSoundSet.h"
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
        return u ? ArsenalStepLanes::heightFor(ArsenalStepLanes::lanesFor(u->type).size()) + soundOffset(u->type) + drawerExtra(unit) : 0.0f;
    }

    /// True when no unit has a sound yet (the kit card's moment): no sample path, and no loaded instrument.
    static bool noSounds(Audio::UnitManager& units) {
        for (const auto id : units.getAllUnitIDs()) {
            const auto* u = units.getUnit(id);
            if (u && (!u->audioClipPath.empty() || (u->type == Audio::UnitType::Instrument && u->plugin))) return false;
        }
        return true;
    }

    /// Called with (unit, path) when a neighbouring sound is chosen; the panel routes it to the gap-free loader.
    void setOnSwap(std::function<void(Audio::UnitID, const std::string&)> cb) { m_onSwap = std::move(cb); }

    /// Sound sets (V81-AR2): the library's collection names, a collection's members, and the choice of a set for a unit.
    void setSoundSetProviders(std::function<std::vector<std::string>()> names, std::function<std::vector<std::string>(const std::string&)> members,
                              std::function<void(Audio::UnitID, const std::string&)> chosen) {
        m_setNames = std::move(names); m_setMembers = std::move(members); m_onSetChosen = std::move(chosen);
    }

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
            const auto col = theme.getColor("textSecondary").withAlpha(open ? 1.0f : 0.8f);
            r.strokeRoundedRect(c, theme.getRadius("xs"), 1.0f, theme.getColor("border").withAlpha(open ? 0.9f : 0.6f));
            const float d = open ? -2.5f : 2.5f; // open points up
            r.drawLine({cx - 4.0f, cy - d}, {cx, cy + d}, 1.5f, col);
            r.drawLine({cx, cy + d}, {cx + 4.0f, cy - d}, 1.5f, col);
            if (open && item.extraH > 0.0f) {
                const auto ctx = contextFor(*tm, pattern, row, item.extraH);
                if (hasSound(*tm, row.getUnitId())) {
                    auto& strip = syncStrip(*tm, row.getUnitId());
                    strip.draw(r, soundRect(row));
                    if (m_drawerUnit == row.getUnitId())
                        ArsenalSoundDrawer::draw(r, drawerRect(row), m_drawer, strip.list(), tm->getUnitManager().getUnit(row.getUnitId())->audioClipPath,
                                                 row.channelColor(), strip.setName(), m_missing[row.getUnitId()]);
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
        if (m_drawerUnit != 0 && e.wheelDelta != 0.0f) {
            for (const auto& item : rack.items)
                if (item.kind == ArsenalRackItem::Kind::Row && rows[item.rowIndex] && rows[item.rowIndex]->getUnitId() == m_drawerUnit &&
                    drawerRect(*rows[item.rowIndex]).contains(e.position)) {
                    ArsenalSoundDrawer::scrollBy(m_drawer, ArsenalSoundDrawer::filtered(m_sounds[m_drawerUnit].list(), m_drawer.filter).size(), e.wheelDelta > 0.0f ? -2 : 2);
                    return true;
                }
        }
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
        if (m_drawerUnit != 0) { // a press anywhere outside the drawer and its strip closes it, keeping the sound it is on
            bool inside = false;
            for (const auto& item : rack.items)
                if (item.kind == ArsenalRackItem::Kind::Row && rows[item.rowIndex] && rows[item.rowIndex]->getUnitId() == m_drawerUnit)
                    inside = drawerRect(*rows[item.rowIndex]).contains(e.position) || soundRect(*rows[item.rowIndex]).contains(e.position);
            if (!inside) { m_drawerUnit = 0; relayout = true; }
        }
        for (const auto& item : rack.items) {
            if (item.kind != ArsenalRackItem::Kind::Row || !rows[item.rowIndex]) continue;
            const auto& row = *rows[item.rowIndex];
            if (caretRect(row).contains(e.position)) {
                const auto id = row.getUnitId();
                if (!m_open.erase(id)) m_open.insert(id);
                relayout = true;
                return true;
            }
            if (m_drawerUnit == row.getUnitId() && item.extraH > 0.0f && drawerRect(row).contains(e.position)) {
                auto& strip = m_sounds[m_drawerUnit];
                const auto idx = ArsenalSoundDrawer::filtered(strip.list(), m_drawer.filter);
                if (ArsenalSoundDrawer::chipRect(drawerRect(row)).contains(e.position)) { // the chip moves to the next sound set (Folder, then each set)
                    std::vector<std::string> choices{std::string()};
                    if (m_setNames) for (const auto& n : m_setNames()) choices.push_back(n);
                    const auto* unit = tm->getUnitManager().getUnit(m_drawerUnit);
                    const auto at = unit ? std::find(choices.begin(), choices.end(), unit->soundSet) : choices.end();
                    const std::string next = choices[(static_cast<size_t>(at == choices.end() ? 0 : at - choices.begin()) + 1) % choices.size()];
                    if (m_onSetChosen) m_onSetChosen(m_drawerUnit, next);
                    m_stale.insert(m_drawerUnit);
                    auto& fresh = syncStrip(*tm, m_drawerUnit);
                    m_drawer = ArsenalSoundDrawer::open(fresh.list(), unit ? unit->audioClipPath : std::string());
                    return true;
                }
                const int hit = ArsenalSoundDrawer::rowAt(m_drawer, idx.size(), drawerRect(row), e.position);
                if (hit >= 0) {
                    m_drawer.selected = hit;
                    if (m_onSwap) m_onSwap(m_drawerUnit, strip.list()[static_cast<size_t>(idx[static_cast<size_t>(hit)])]); // click auditions in the loop
                }
                return true;
            }
            if (item.extraH > 0.0f && m_open.count(row.getUnitId()) && hasSound(*tm, row.getUnitId()) && soundRect(row).contains(e.position)) {
                auto& strip = m_sounds[row.getUnitId()];
                const auto hit = strip.hitTest(soundRect(row), e.position);
                if (hit == ArsenalSoundStrip::Hit::None) { // the name opens (or, again, closes keeping) the drawer
                    if (m_drawerUnit == row.getUnitId()) m_drawerUnit = 0;
                    else if (const auto* unit = tm->getUnitManager().getUnit(row.getUnitId())) {
                        m_stale.insert(row.getUnitId()); // pick up changes made to the library since the row was last drawn
                        m_drawerUnit = row.getUnitId();
                        m_drawer = ArsenalSoundDrawer::open(syncStrip(*tm, row.getUnitId()).list(), unit->audioClipPath);
                    }
                    relayout = true;
                    return true;
                }
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

    /// Keyboard for the open drawer: typing searches, Up/Down audition, Enter keeps, Esc puts the old sound back.
    /// Returns true when the key was the drawer's (so it is not also a shortcut or a delete).
    bool onKey(const AestraUI::NUIKeyEvent& e, Audio::TrackManager* tm, bool& relayout) {
        if (m_drawerUnit == 0 || !tm || !e.pressed) return false;
        auto& strip = m_sounds[m_drawerUnit];
        const auto out = ArsenalSoundDrawer::onKey(m_drawer, strip.list(), e);
        if (out.result == ArsenalSoundDrawer::Result::Ignored) return false;
        const auto* unit = tm->getUnitManager().getUnit(m_drawerUnit);
        const std::string current = unit ? unit->audioClipPath : std::string();
        if (!out.path.empty() && out.path != current && m_onSwap && out.result != ArsenalSoundDrawer::Result::Consumed) m_onSwap(m_drawerUnit, out.path);
        if (out.result == ArsenalSoundDrawer::Result::Keep || out.result == ArsenalSoundDrawer::Result::Revert) { m_drawerUnit = 0; relayout = true; }
        return true;
    }
    bool drawerOpen() const { return m_drawerUnit != 0; }

private:
    /// The unit's strip, listing its sound set when it has one and its folder otherwise.
    ArsenalSoundStrip& syncStrip(Audio::TrackManager& tm, Audio::UnitID id) const {
        auto& strip = m_sounds[id];
        const auto* unit = tm.getUnitManager().getUnit(id);
        if (!unit) return strip;
        if (!unit->soundSet.empty() && m_setMembers) {
            if (strip.setName() != unit->soundSet || !strip.isSet() || m_stale.count(id)) {
                const auto resolved = ArsenalSoundSet::resolve(m_setMembers(unit->soundSet));
                strip.useFolder();
                strip.follow(unit->audioClipPath);
                strip.useList(unit->soundSet, resolved.files);
                m_missing[id] = resolved.missing.size();
                m_stale.erase(id);
            }
        } else if (strip.isSet() || m_stale.count(id)) {
            strip.useFolder();
            m_missing[id] = 0;
            m_stale.erase(id);
        }
        strip.follow(unit->audioClipPath);
        return strip;
    }
    float drawerExtra(Audio::UnitID unit) const { return m_drawerUnit == unit ? ArsenalSoundDrawer::kHeight + ArsenalStepLanes::kLaneGap : 0.0f; }
    static AestraUI::NUIRect drawerRect(const AestraUI::UnitRow& row) {
        const auto s = soundRect(row);
        return {s.x, s.bottom() + 2.0f, s.width, ArsenalSoundDrawer::kHeight};
    }
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
    /// The open-lanes key: a 20 px key after S at the end of the row's head, centred on the row.
    static AestraUI::NUIRect caretRect(const AestraUI::UnitRow& row) {
        const auto b = row.getBounds();
        return {b.x + row.headRight() - 8.0f - 20.0f, b.y + (b.height - 20.0f) * 0.5f, 20.0f, 20.0f};
    }
    static const ArsenalRackItem* itemFor(const ArsenalRackLayout& rack, size_t rowIndex) {
        for (const auto& i : rack.items)
            if (i.kind == ArsenalRackItem::Kind::Row && i.rowIndex == rowIndex) return &i;
        return nullptr;
    }
    ArsenalStepLanes::Context contextFor(Audio::TrackManager& tm, Audio::PatternID pattern, const AestraUI::UnitRow& row, float extraH) const {
        ArsenalStepLanes::Context c;
        const auto b = row.getBounds();
        c.tm = &tm;
        c.pattern = pattern;
        c.unit = row.getUnitId();
        if (const auto* u = tm.getUnitManager().getUnit(c.unit)) c.type = u->type;
        c.root = tm.getUnitManager().getUnitRootMidiNote(c.unit);
        c.steps = row.getStepCount();
        const float off = soundOffset(c.type) + drawerExtra(c.unit);
        c.area = {b.x, b.bottom() + 2.0f + off, b.width, std::max(0.0f, extraH - 2.0f - off)};
        c.strip = row.stepStrip();
        c.stepW = row.stepPadWidth();
        c.groupGap = AestraUI::UnitRow::stepGroupGap();
        c.scrollX = row.getScrollX();
        c.accent = row.channelColor();
        return c;
    }

    std::set<Audio::UnitID> m_open;
    Audio::UnitID m_drawerUnit{0};            ///< The unit whose sound drawer is open (one at a time).
    ArsenalSoundDrawer::State m_drawer;
    mutable std::map<Audio::UnitID, ArsenalSoundStrip> m_sounds;
    mutable std::map<Audio::UnitID, size_t> m_missing;
    mutable std::set<Audio::UnitID> m_stale;
    std::function<std::vector<std::string>()> m_setNames;
    std::function<std::vector<std::string>(const std::string&)> m_setMembers;
    std::function<void(Audio::UnitID, const std::string&)> m_onSetChosen;
    std::function<void(Audio::UnitID, const std::string&)> m_onSwap;
    mutable ArsenalStepLanes m_lanes;
    size_t m_dragRow{0};
};

} // namespace Aestra
