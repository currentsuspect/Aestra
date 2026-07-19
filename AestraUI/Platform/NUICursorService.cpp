// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUICursorService.h"

namespace AestraUI {

bool NUICursorService::beginDragCapture(NUICursorRestorePolicy policy, int grabOriginX, int grabOriginY) {
    if (m_inTransition) {
        // A host callback fired during end/cancel tried to start a new capture
        // before the old one reached idle. Refuse: interleaving hide/show/grab
        // sequences would corrupt both captures' host state.
        return false;
    }
    if (m_captured) {
        // A begin while captured means a release event was lost (focus churn,
        // popup, teardown). Recover instead of wedging: restore visibility at
        // the platform's current position, then start the new capture.
        cancelDragCapture();
    }
    m_policy = policy;
    m_grabOriginX = grabOriginX;
    m_grabOriginY = grabOriginY;
    m_captured = true;

    m_host.hostHideCursor();
    // Confine the pointer for the duration of the capture: on native Wayland a
    // warp is a silent no-op once the (hidden) pointer drifts out of the
    // window and loses focus — the grab keeps the release-warp valid.
    m_host.hostSetPointerGrab(true);
    return true;
}

void NUICursorService::endDragCapture(int restoreX, int restoreY) {
    if (!m_captured) {
        return;
    }
    m_captured = false;
    m_inTransition = true;

    const int x = (m_policy == NUICursorRestorePolicy::GrabOrigin) ? m_grabOriginX : restoreX;
    const int y = (m_policy == NUICursorRestorePolicy::GrabOrigin) ? m_grabOriginY : restoreY;

    // Order matters: warp while still hidden so the cursor never renders a
    // frame at the drifted pre-warp position, then unhide, then release the
    // grab (releasing first would let the warp race the un-confinement).
    m_host.hostWarpCursor(x, y);
    m_host.hostShowCursor();
    m_host.hostSetPointerGrab(false);
    m_inTransition = false;
}

void NUICursorService::cancelDragCapture() {
    if (!m_captured) {
        return;
    }
    m_captured = false;
    m_inTransition = true;
    // No warp: a cancel means the warp target may no longer be valid (focus
    // gone, window hidden). Just make the cursor visible wherever it is.
    m_host.hostShowCursor();
    m_host.hostSetPointerGrab(false);
    m_inTransition = false;
}

} // namespace AestraUI
