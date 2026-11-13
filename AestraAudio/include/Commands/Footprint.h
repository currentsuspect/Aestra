// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// What a step in history touched (SPEC-026 pickable undo, FD-82): which clips, notes, tracks, units or settings,
// and optionally which part of each. Two steps overlap when they touched the same thing.
//
// A command that does not say what it touches counts as touching everything (Footprint::everything()), so it
// is never undone around. Header-only; nothing here runs on the audio thread.
#pragma once

#include "../AestraUUID.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Aestra {
namespace Audio {

enum class Touch : uint8_t { Track, Lane, Clip, Pattern, Note, Unit, MixerChannel, Marker, Automation, Project };

class Footprint {
public:
    // The whole thing, rather than one part of it (a track's height, a clip's gain).
    static constexpr uint32_t kWhole = 0xFFFFFFFFu;

    static Footprint everything() {
        Footprint f;
        f.m_everything = true;
        return f;
    }

    // @p part names one part of the thing (each kind numbers its own parts); kWhole for all of it.
    void add(Touch kind, AestraUUID id, uint32_t part = kWhole) { m_touched.push_back({kind, id, part}); }

    bool isEverything() const { return m_everything; }
    bool empty() const { return !m_everything && m_touched.empty(); }

    bool overlaps(const Footprint& other) const {
        if ((m_everything && !other.empty()) || (other.m_everything && !empty())) return true;
        for (const auto& a : m_touched) {
            for (const auto& b : other.m_touched) {
                if (a.kind == b.kind && a.id == b.id && (a.part == kWhole || b.part == kWhole || a.part == b.part)) {
                    return true;
                }
            }
        }
        return false;
    }

private:
    struct Touched {
        Touch kind;
        AestraUUID id;
        uint32_t part;
    };
    std::vector<Touched> m_touched;
    bool m_everything = false;
};

} // namespace Audio
} // namespace Aestra
