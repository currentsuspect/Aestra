// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Which pattern the Arsenal (and the piano roll, which follows it) shows when none is on show: a project just
// opened, or the pattern on show was deleted. #999: it was always the lowest id, so a project whose "Pattern 1" was
// empty opened both editors on nothing while the arrangement played another pattern.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Aestra {
namespace ArsenalPatternPick {

struct Candidate {
    uint64_t id;
    size_t noteCount;
};

/// The lowest-id MIDI pattern that has notes; if none has any, the lowest id; 0 when there are none.
inline uint64_t patternToShow(const std::vector<Candidate>& midiPatterns) {
    uint64_t firstAny = 0, firstWithNotes = 0;
    for (const Candidate& c : midiPatterns) {
        if (firstAny == 0 || c.id < firstAny) firstAny = c.id;
        if (c.noteCount > 0 && (firstWithNotes == 0 || c.id < firstWithNotes)) firstWithNotes = c.id;
    }
    return firstWithNotes != 0 ? firstWithNotes : firstAny;
}

} // namespace ArsenalPatternPick
} // namespace Aestra
