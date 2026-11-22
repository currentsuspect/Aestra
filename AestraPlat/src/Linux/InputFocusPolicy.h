#pragma once

namespace Aestra {

// Whether a left press that reached Aestra's own window should take X input focus back.
//
// An embedded plugin editor (a child X window) can take input focus for itself and nothing returns it, so typing in
// Aestra's text fields goes nowhere. A press on Aestra's own surface is the user saying "here"; if the focus is
// somewhere else, take it back. Two cases must not: the focus is unknown (nothing to compare), and the press landed
// over a mapped child window (the user is clicking the plugin, which must keep the focus it asked for).
inline bool shouldReclaimInputFocus(bool focusOwnerKnown, bool focusOwnerIsAestra, bool pressHitAestra,
                                    bool pressUnderEmbeddedChild) {
    if (!focusOwnerKnown || focusOwnerIsAestra) return false;
    if (!pressHitAestra) return false;
    return !pressUnderEmbeddedChild;
}

} // namespace Aestra
