#pragma once

// X input focus inspection and reclaim for Aestra's own window. Linux/X11 only.

#include <X11/Xlib.h>
#include <string>

namespace Aestra::XInputFocus {

enum class Relation { Unknown, NoFocus, PointerRootFocus, Aestra, Container, Descendant, Ancestor, Other };

struct Snapshot {
    Relation relation = Relation::Unknown;
    ::Window window = 0;
    int depth = 0;       ///< levels below Aestra's window (Container is 1)
    std::string name;    ///< the window's WM name when it has one
};

Snapshot query(Display* display, ::Window aestra);
std::string describe(const Snapshot& snapshot);

/// True when the snapshot says focus is held by something that is not Aestra's own window.
inline bool known(const Snapshot& s) { return s.relation != Relation::Unknown; }
inline bool isAestra(const Snapshot& s) { return s.relation == Relation::Aestra; }

/// A mapped child of Aestra's window that covers (x, y) in Aestra's coordinates.
bool childUnder(Display* display, ::Window aestra, int x, int y);

/// Gives Aestra's window X input focus. Does not raise it.
void reclaim(Display* display, ::Window aestra);

} // namespace Aestra::XInputFocus
