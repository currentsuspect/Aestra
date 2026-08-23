// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// ClipSource's out-of-line mutation path. setBuffer lives here rather than in
// the header because notifying the owning SourceManager needs the complete
// type, and SourceManager.h already includes ClipSource.h.

#include "Models/ClipSource.h"

#include "Models/SourceManager.h"

namespace Aestra {
namespace Audio {

void ClipSource::setBuffer(std::shared_ptr<AudioBufferData> buffer) {
    const bool wasReady = isValid();
    m_buffer = std::move(buffer);
    m_waveformCache.reset();
    ++m_contentRevision;
    clearFilteredVariant(); // new content invalidates any anti-aliased copy

    // INVARIANT: every not-ready -> ready transition must bump the owning
    // SourceManager's revision. TrackManagerUI change-gates its "install
    // missing waveform caches" sweep on that revision, so a source whose buffer
    // arrives through a path that does not go through SourceManager (e.g. an
    // async decode completing onto an already-deduped source) would otherwise
    // render as a bare centre line until some unrelated source event happened
    // to run the sweep. A null owner means standalone source: nothing to wake.
    if (!wasReady && isValid() && m_owner) {
        m_owner->bumpRevision();
    }
}

} // namespace Audio
} // namespace Aestra
