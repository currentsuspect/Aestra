// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PluginHost.h"

#include <cstdint>

namespace Aestra {
namespace Audio {

/// The events of @p in whose offset is inside [sliceStart, sliceStart + sliceFrames), with their offsets made relative to the slice
/// and their note identities kept, appended to @p out. Used to cut a large offline render block into plugin-sized slices.
inline void splitMidiForSlice(const MidiBuffer& in, uint32_t sliceStart, uint32_t sliceFrames, MidiBuffer& out) {
    for (size_t i = 0; i < in.getEventCount(); ++i) {
        const MidiBuffer::Event& event = in.getEvent(i);
        if (event.sampleOffset >= sliceStart && event.sampleOffset - sliceStart < sliceFrames) {
            out.addEvent(event.sampleOffset - sliceStart, event.data, event.size, event.noteId);
        }
    }
}

} // namespace Audio
} // namespace Aestra
