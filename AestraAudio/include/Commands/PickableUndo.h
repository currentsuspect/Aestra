// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Pickable undo (SPEC-026, FD-82): which later steps depend on an earlier one, read from what each touched.
#pragma once

#include "Commands/ICommand.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace Aestra {
namespace Audio {

// The later steps that depend on step @p index of @p history (oldest first): those that touched what it
// touched, followed through, so a step depending on a dependant is named too. In history order.
inline std::vector<size_t> dependantsOf(const std::vector<std::shared_ptr<ICommand>>& history, size_t index) {
    std::vector<size_t> dependants;
    if (index >= history.size()) return dependants;
    // What has to come undone with it: its own footprint, grown by each dependant found.
    std::vector<Footprint> reached{history[index] ? history[index]->footprint() : Footprint::everything()};
    for (size_t j = index + 1; j < history.size(); ++j) {
        const Footprint later = history[j] ? history[j]->footprint() : Footprint::everything();
        for (const Footprint& f : reached) {
            if (later.overlaps(f)) {
                dependants.push_back(j);
                reached.push_back(later);
                break;
            }
        }
    }
    return dependants;
}

} // namespace Audio
} // namespace Aestra
