// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// NUIPointerVisibilityWatchdog — the pointer must never be invisible.
//
// The app hides the OS pointer whenever it draws its own (the overlay arrow, the timeline's tool cursor)
// or has captured it for an infinite drag. Several things can leave it hidden while nothing is drawing:
// a stale tool-cursor claim whose view is not painted (hidden behind a maximised editor), a capture that
// lost its release. Owner reports, 2026-10-11: an invisible pointer over the file browser, over knobs
// while dragging, and in the piano roll after auditioning a one-shot.
//
// Each of those was patched individually; this is the net under all of them. Once per frame it is told
// whether the OS pointer is about to be hidden and whether the app actually drew one; if the pointer has
// been hidden with nothing drawn for kGraceFrames in a row, it says "show the OS pointer" and keeps
// saying so until the situation ends. A capture is a deliberate hide and never counts.

#pragma once

namespace AestraUI {

class NUIPointerVisibilityWatchdog {
public:
    /// About a third of a second at 60 fps: longer than any legitimate one-frame gap between
    /// "claim set" and "claim drawn", short enough that a stuck pointer is noticed at once.
    static constexpr int kGraceFrames = 20;

    /**
     * @param nativeHidden the OS pointer would be hidden this frame
     * @param drawnByApp   the app drew a pointer this (or the last) frame
     * @param captured     the pointer is deliberately hidden by a drag capture
     * @return true when the OS pointer must be shown to rescue an invisible pointer
     */
    bool update(bool nativeHidden, bool drawnByApp, bool captured) {
        if (!nativeHidden || drawnByApp || captured) {
            m_frames = 0;
            m_engaged = false;
            return false;
        }
        if (m_frames < kGraceFrames) {
            ++m_frames;
        }
        if (m_frames >= kGraceFrames) {
            m_engaged = true;
        }
        return m_engaged;
    }

    bool engaged() const { return m_engaged; }
    int hiddenFrames() const { return m_frames; }

private:
    int m_frames = 0;
    bool m_engaged = false;
};

} // namespace AestraUI
