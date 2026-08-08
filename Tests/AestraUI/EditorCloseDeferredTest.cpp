// © 2026 Aestra Studios — All Rights Reserved.
// EditorCloseDeferredTest — regression for the plugin-editor close cycle (#475).
//
// A plugin editor closes itself from inside its own onMouseEvent by calling
// m_popupLayer->removeChild(self). Before the fix, that erased the parent's
// children_ mid-iteration (iterator invalidation) and, with a weak_ptr close
// capture, freed the editor while it was still on the call stack (use-after-
// free). The fix defers hierarchy mutations while an event dispatch is in flight
// and flushes them when the dispatch unwinds. These tests exercise both the
// canonical dispatch entry point used by the platform bridge and nested guards.

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
    size_t childCountAfterRemoval = 0;
    bool onMouseEvent(const NUIMouseEvent&) override {
        sawEvent = true;
        if (auto* parent = getParent()) {
            for (const auto& child : parent->getChildren()) {
                if (child.get() == this) {
                    parent->removeChild(child);
                    break;
                }
            }
            childCountAfterRemoval = parent->getChildren().size();
        }
        return false; // don't consume — force the parent loop to continue past a removed child
    }
};

// Mirrors TrackManagerUI::refreshTracks(): remove every existing row and add a
// replacement generation from inside the row's mouse callback.
class HierarchyRebuilder : public NUIComponent {
public:
    std::shared_ptr<NUIComponent> replacementA = std::make_shared<NUIComponent>();
    std::shared_ptr<NUIComponent> replacementB = std::make_shared<NUIComponent>();
    size_t childCountDuringCallback = 0;

    bool onMouseEvent(const NUIMouseEvent&) override {
        auto* parent = getParent();
        if (!parent) {
            return false;
        }

        for (const auto& child : parent->getChildren()) {
            parent->removeChild(child);
        }
        parent->addChild(replacementA);
        parent->addChild(replacementB);
        childCountDuringCallback = parent->getChildren().size();
        return true;
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

    NUIMouseEvent ev;
    ev.pressed = true;
    ev.button = NUIMouseButton::Left;

    NUIComponent::dispatchMouseEvent(root.get(), ev); // both children remove themselves mid-traversal

    check(a->childCountAfterRemoval == 3 && b->childCountAfterRemoval == 3,
          "canonical dispatch defers removal during traversal");
    check(a->sawEvent && b->sawEvent, "both self-closers received the event");

    check(root->getChildren().size() == 1, "self-closers removed after dispatch");
    check(root->getChildren().front() == keep, "the kept child remains");
    a.reset();
    b.reset();
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

// Popup creation from a release callback must not mutate the parent's child
// vector until the dispatch loop has finished iterating it.
void testAdditionDuringDispatchIsDeferred() {
    auto root = std::make_shared<NUIComponent>();
    auto existing = std::make_shared<NUIComponent>();
    auto popup = std::make_shared<NUIComponent>();
    root->addChild(existing);

    NUIComponent::beginEventDispatch();
    root->addChild(popup);
    check(root->getChildren().size() == 1, "addition preserved outside active child vector during dispatch");
    check(popup->getParent() == nullptr, "deferred popup remains detached during dispatch");
    NUIComponent::endEventDispatch();

    check(root->getChildren().size() == 2, "addition flushed after dispatch");
    check(root->getChildren().back() == popup, "deferred popup keeps frontmost insertion order");
    check(popup->getParent() == root.get(), "deferred popup parent assigned after dispatch");
}

void testHierarchyRebuildDuringCanonicalDispatchIsSafe() {
    auto root = std::make_shared<NUIComponent>();
    auto oldRow = std::make_shared<NUIComponent>();
    auto rebuilder = std::make_shared<HierarchyRebuilder>();
    root->addChild(oldRow);
    root->addChild(rebuilder); // frontmost: handles the event first

    std::weak_ptr<NUIComponent> oldRowWeak = oldRow;
    std::weak_ptr<NUIComponent> rebuilderWeak = rebuilder;

    NUIMouseEvent ev;
    ev.pressed = true;
    ev.button = NUIMouseButton::Left;
    check(NUIComponent::dispatchMouseEvent(root.get(), ev), "hierarchy rebuild event handled");

    check(rebuilder->childCountDuringCallback == 2, "old hierarchy preserved until callback returns");
    check(root->getChildren().size() == 2, "replacement hierarchy installed after dispatch");
    check(root->getChildren()[0] == rebuilder->replacementA && root->getChildren()[1] == rebuilder->replacementB,
          "replacement hierarchy keeps insertion order");

    oldRow.reset();
    rebuilder.reset();
    check(oldRowWeak.expired() && rebuilderWeak.expired(), "old hierarchy released after guarded rebuild");
}

} // namespace

int main() {
    testSelfRemovalDuringDispatchIsSafe();
    testRemoveOutsideDispatchIsImmediate();
    testNestedDispatchDepth();
    testAdditionDuringDispatchIsDeferred();
    testHierarchyRebuildDuringCanonicalDispatchIsSafe();

    if (g_failures > 0) {
        std::cout << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "All editor-close deferred-destruction checks passed\n";
    return 0;
}
