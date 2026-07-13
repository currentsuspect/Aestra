// © 2026 Aestra Studios — All Rights Reserved.
// EditorCloseDeferredTest — regression for the plugin-editor close cycle (#475).
//
// A plugin editor closes itself from inside its own onMouseEvent by calling
// m_popupLayer->removeChild(self). Before the fix, that erased the parent's
// children_ mid-iteration (iterator invalidation) and, with a weak_ptr close
// capture, freed the editor while it was still on the call stack (use-after-
// free). The fix defers removeChild() while an event dispatch is in flight
// (NUIComponent::begin/endEventDispatch) and flushes it when the dispatch
// unwinds. These tests exercise that directly at the NUIComponent level.

#include "NUIComponent.h"
#include "NUITypes.h"

#include <iostream>
#include <memory>
#include <vector>

using namespace AestraUI;

namespace {

// Removes itself from its parent during onMouseEvent, then lets the parent's
// dispatch loop continue to the next sibling (returns false).
class SelfCloser : public NUIComponent {
public:
    bool sawEvent = false;
    bool onMouseEvent(const NUIMouseEvent&) override {
        sawEvent = true;
        if (auto* parent = getParent()) {
            for (const auto& child : parent->getChildren()) {
                if (child.get() == this) {
                    parent->removeChild(child);
                    break;
                }
            }
        }
        return false; // don't consume — force the parent loop to continue past a removed child
    }
};

int g_failures = 0;
void check(bool cond, const char* what) {
    if (!cond) {
        std::cout << "[FAIL] " << what << "\n";
        ++g_failures;
    }
}

// Two children remove themselves during a single dispatch; a plain child stays.
void testSelfRemovalDuringDispatchIsSafe() {
    auto root = std::make_shared<NUIComponent>();
    auto a = std::make_shared<SelfCloser>();
    auto b = std::make_shared<SelfCloser>();
    auto keep = std::make_shared<NUIComponent>();
    root->addChild(a);
    root->addChild(b);
    root->addChild(keep);

    std::weak_ptr<NUIComponent> wa = a, wb = b;
    SelfCloser* ap = a.get();
    SelfCloser* bp = b.get();
    a.reset();
    b.reset(); // only root holds a and b now

    NUIMouseEvent ev;
    ev.pressed = true;
    ev.button = NUIMouseButton::Left;

    NUIComponent::beginEventDispatch();
    root->onMouseEvent(ev); // iterates children; a and b remove themselves mid-loop
    // Removal is deferred: still present, still alive during dispatch.
    check(root->getChildren().size() == 3, "children preserved during dispatch");
    check(!wa.expired() && !wb.expired(), "self-closers alive during dispatch");
    check(ap->sawEvent && bp->sawEvent, "both self-closers received the event");
    NUIComponent::endEventDispatch(); // flush deferred removals

    check(root->getChildren().size() == 1, "self-closers removed after dispatch");
    check(root->getChildren().front() == keep, "the kept child remains");
    check(wa.expired() && wb.expired(), "removed editors freed (no retain cycle)");
}

// removeChild outside any dispatch removes immediately (unchanged behavior).
void testRemoveOutsideDispatchIsImmediate() {
    auto root = std::make_shared<NUIComponent>();
    auto child = std::make_shared<NUIComponent>();
    root->addChild(child);
    std::weak_ptr<NUIComponent> w = child;
    child.reset();

    root->removeChild(root->getChildren().front());
    check(root->getChildren().empty(), "immediate removal outside dispatch");
    check(w.expired(), "child freed immediately outside dispatch");
}

// Nested begin/end: removal only flushes when the outermost dispatch unwinds.
void testNestedDispatchDepth() {
    auto root = std::make_shared<NUIComponent>();
    auto child = std::make_shared<NUIComponent>();
    root->addChild(child);
    auto childRef = root->getChildren().front();

    NUIComponent::beginEventDispatch();
    NUIComponent::beginEventDispatch();
    root->removeChild(childRef);
    check(root->getChildren().size() == 1, "deferred while nested dispatch active");
    NUIComponent::endEventDispatch();
    check(root->getChildren().size() == 1, "still deferred at inner unwind");
    NUIComponent::endEventDispatch();
    check(root->getChildren().empty(), "flushed at outer unwind");
}

} // namespace

int main() {
    testSelfRemovalDuringDispatchIsSafe();
    testRemoveOutsideDispatchIsImmediate();
    testNestedDispatchDepth();

    if (g_failures > 0) {
        std::cout << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "All editor-close deferred-destruction checks passed\n";
    return 0;
}
