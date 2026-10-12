// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Models/UnitManager.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace Aestra {

/**
 * @brief Where the Arsenal's rows and group headers sit (V81-AR1, FD-27 point 2): "a grouped rack".
 *
 * Units sit under their UnitGroup (Drums, Synth, Audio, then anything else), each group under a header
 * with its count, in the order the units were made inside a group. The rule and the positions are pure so
 * the panel's layout, scrolling and drag-and-drop all read one answer instead of each assuming uniform rows.
 */
struct ArsenalRackItem {
    enum class Kind { Header, Row };
    Kind kind{Kind::Row};
    Audio::UnitGroup group{Audio::UnitGroup::Unknown};
    size_t rowIndex{0}; ///< index into the panel's row list (Row only)
    int count{0};       ///< rows in the group (Header only)
    float y{0.0f};      ///< top, relative to the rack's start
    float h{0.0f};
    float extraH{0.0f}; ///< an open row's lanes, directly under it (Row only)
};

struct ArsenalRackLayout {
    static constexpr float kHeaderH = 30.0f;
    static constexpr float kRowH = 44.0f;
    static constexpr float kRowGap = 4.0f;

    std::vector<ArsenalRackItem> items;
    std::vector<size_t> displayOrder; ///< display slot -> row index
    float contentHeight{0.0f};

    static const char* groupLabel(Audio::UnitGroup g) {
        switch (g) {
        case Audio::UnitGroup::Drums: return "Drums";
        case Audio::UnitGroup::Synth: return "Synth";
        case Audio::UnitGroup::Audio: return "Audio";
        default: return "Other";
        }
    }

    /// @p groups: each row's group, in the order the panel holds them (the units' own order).
    /// @p extras: height an open row adds under itself (parallel to @p groups; empty or 0 = closed).
    static ArsenalRackLayout compute(const std::vector<Audio::UnitGroup>& groups, const std::vector<float>& extras = {}) {
        ArsenalRackLayout out;
        static constexpr Audio::UnitGroup order[] = {Audio::UnitGroup::Drums, Audio::UnitGroup::Synth,
                                                     Audio::UnitGroup::Audio, Audio::UnitGroup::Unknown};
        float y = 0.0f;
        for (const auto g : order) {
            int count = 0;
            for (const auto& rg : groups) {
                // Anything that is not one of the three named groups lands under "Other".
                const bool named = rg == Audio::UnitGroup::Drums || rg == Audio::UnitGroup::Synth || rg == Audio::UnitGroup::Audio;
                if (rg == g || (g == Audio::UnitGroup::Unknown && !named)) ++count;
            }
            if (count == 0) continue;
            ArsenalRackItem header;
            header.kind = ArsenalRackItem::Kind::Header;
            header.group = g;
            header.count = count;
            header.y = y;
            header.h = kHeaderH;
            out.items.push_back(header);
            y += kHeaderH;
            for (size_t i = 0; i < groups.size(); ++i) {
                const auto rg = groups[i];
                const bool named = rg == Audio::UnitGroup::Drums || rg == Audio::UnitGroup::Synth || rg == Audio::UnitGroup::Audio;
                if (!(rg == g || (g == Audio::UnitGroup::Unknown && !named))) continue;
                ArsenalRackItem row;
                row.kind = ArsenalRackItem::Kind::Row;
                row.group = g;
                row.rowIndex = i;
                row.y = y;
                row.h = kRowH;
                row.extraH = i < extras.size() ? extras[i] : 0.0f;
                out.items.push_back(row);
                out.displayOrder.push_back(i);
                y += kRowH + row.extraH + kRowGap;
            }
        }
        out.contentHeight = y;
        return out;
    }

    /// Top of a row, or 0 when there is no such row.
    float rowY(size_t rowIndex) const {
        for (const auto& item : items)
            if (item.kind == ArsenalRackItem::Kind::Row && item.rowIndex == rowIndex) return item.y;
        return 0.0f;
    }

    /**
     * @brief Which display slot a drop at @p y (relative to the rack's start) lands before: the number of rows
     *        whose middle is above it. The panel maps this to a unit index with insertIndexForSlot().
     */
    size_t dropSlotForY(float y) const {
        size_t slot = 0;
        for (const auto& item : items) {
            if (item.kind != ArsenalRackItem::Kind::Row) continue;
            if (item.y + item.h * 0.5f < y) ++slot;
        }
        return slot;
    }

    /// The unit-order index a drop at display slot @p slot inserts before (the row count when it is the end).
    size_t insertIndexForSlot(size_t slot, size_t rowCount) const {
        return slot < displayOrder.size() ? displayOrder[slot] : rowCount;
    }
};

} // namespace Aestra
