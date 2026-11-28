#include "XInputFocus.h"

#include "XErrorGuard.h"

#include <cstdio>
#include <vector>

namespace Aestra::XInputFocus {

namespace {
// Parents from @p w up to the root, nearest first.
std::vector<::Window> ancestors(Display* display, ::Window w) {
    std::vector<::Window> chain;
    for (int guard = 0; guard < 64 && w; ++guard) {
        ::Window root = 0, parent = 0;
        ::Window* kids = nullptr;
        unsigned count = 0;
        if (!XQueryTree(display, w, &root, &parent, &kids, &count)) break;
        if (kids) XFree(kids);
        if (!parent) break;
        chain.push_back(parent);
        w = parent;
    }
    return chain;
}
} // namespace

Snapshot query(Display* display, ::Window aestra) {
    Snapshot s;
    if (!display || !aestra) return s;
    XErrorGuard guard(display);
    ::Window focus = 0;
    int revert = 0;
    XGetInputFocus(display, &focus, &revert);
    s.window = focus;
    if (focus == None) { s.relation = Relation::NoFocus; return s; }
    if (focus == PointerRoot) { s.relation = Relation::PointerRootFocus; return s; }
    if (focus == aestra) { s.relation = Relation::Aestra; return s; }

    char* name = nullptr;
    if (XFetchName(display, focus, &name) && name) {
        s.name = name;
        XFree(name);
    }
    const auto up = ancestors(display, focus);
    for (size_t i = 0; i < up.size(); ++i) {
        if (up[i] == aestra) {
            s.depth = static_cast<int>(i) + 1;
            s.relation = s.depth == 1 ? Relation::Container : Relation::Descendant;
            return s;
        }
    }
    for (const ::Window a : ancestors(display, aestra)) {
        if (a == focus) { s.relation = Relation::Ancestor; return s; }
    }
    s.relation = Relation::Other;
    return s;
}

std::string describe(const Snapshot& s) {
    const char* what = "unknown";
    switch (s.relation) {
    case Relation::NoFocus: what = "none"; break;
    case Relation::PointerRootFocus: what = "pointer-root"; break;
    case Relation::Aestra: what = "AESTRA"; break;
    case Relation::Container: what = "plugin-container(child of Aestra)"; break;
    case Relation::Descendant: what = "descendant-of-Aestra"; break;
    case Relation::Ancestor: what = "ancestor-of-Aestra(frame)"; break;
    case Relation::Other: what = "OTHER-client-window"; break;
    case Relation::Unknown: break;
    }
    char buf[96];
    std::snprintf(buf, sizeof buf, " win=0x%lx depth=%d", static_cast<unsigned long>(s.window), s.depth);
    std::string out = std::string("xfocus=") + what + buf;
    if (!s.name.empty()) out += " name=\"" + s.name + "\"";
    return out;
}

bool isWithin(Display* display, ::Window focus, ::Window child) {
    if (!display || !focus || !child) return false;
    if (focus == child) return true;
    XErrorGuard guard(display);
    for (const ::Window a : ancestors(display, focus))
        if (a == child) return true;
    return false;
}

bool childUnder(Display* display, ::Window aestra, int x, int y) {
    if (!display || !aestra) return false;
    XErrorGuard guard(display);
    ::Window root = 0, parent = 0;
    ::Window* kids = nullptr;
    unsigned count = 0;
    if (!XQueryTree(display, aestra, &root, &parent, &kids, &count)) return false;
    bool hit = false;
    for (unsigned i = 0; i < count && !hit; ++i) {
        XWindowAttributes a;
        if (!XGetWindowAttributes(display, kids[i], &a) || a.map_state != IsViewable) continue;
        hit = x >= a.x && y >= a.y && x < a.x + a.width && y < a.y + a.height;
    }
    if (kids) XFree(kids);
    return hit;
}

void reclaim(Display* display, ::Window aestra) {
    if (!display || !aestra) return;
    XErrorGuard guard(display); // BadMatch when the window is not viewable
    XSetInputFocus(display, aestra, RevertToParent, CurrentTime);
    XFlush(display);
}

} // namespace Aestra::XInputFocus
