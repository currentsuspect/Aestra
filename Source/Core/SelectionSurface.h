// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// A surface that shows a selection (Selection System Spec, section 2.3). The model computes what changed; the
// surface says where each id is; repaintRects() turns the one into the other, so selecting row 3 repaints row 3 and
// the row that was selected, nothing else. Pure: no window, no renderer, so a table test can drive it.

#include "NUITypes.h"
#include "SelectionModel.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace Aestra {

class ISelectionSurface {
public:
    virtual ~ISelectionSurface() = default;

    /// Which surface this is. A surface only ever answers for its own ids.
    virtual SelectionSurface surface() const = 0;

    /// Where the item with this stable domain id is drawn, in the surface's own window coordinates; nothing when it
    /// is not on screen (scrolled out, folded, closed), so nothing needs repainting for it.
    virtual std::optional<AestraUI::NUIRect> boundsOf(uint64_t id) const = 0;

    /// The surface's own side effects of a change (the Arsenal showing the selected unit's pattern in the piano roll).
    /// Repainting is not one of them: the framework does that from repaintRects().
    virtual void onSelectionChanged(const SelectionChange&) {}
};

/// The rectangles of `surface` that `change` made stale: one per affected id this surface owns and has on screen.
inline std::vector<AestraUI::NUIRect> repaintRects(const ISelectionSurface& surface, const SelectionChange& change) {
    std::vector<AestraUI::NUIRect> rects;
    for (const SelectionKey& key : SelectionModel::affectedKeys(change)) {
        if (key.surface != surface.surface()) continue;
        if (const auto r = surface.boundsOf(key.id)) rects.push_back(*r);
    }
    return rects;
}

} // namespace Aestra
