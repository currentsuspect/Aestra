// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// CursorServiceTest — pins the NUICursorService state machine (cursor
// unification phase 1). A fake host records the exact call sequence so the
// invariants that make the "infinite drag" magic reliable are enforced:
//   * begin: hide THEN grab
//   * end: warp THEN show THEN ungrab (cursor never renders at the drifted
//     pre-warp position; grab released only after the warp landed)
//   * cancel: show + ungrab, NO warp (safe fallback when focus is gone)
//   * GrabOrigin policy restores the stored begin position
//   * begin-while-captured recovers (cancel + fresh begin) instead of wedging

#include "../../AestraUI/Platform/NUICursorService.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace AestraUI;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL (line %d): %s\n", __LINE__, #cond);     \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

struct FakeHost : NUICursorHost {
    std::vector<std::string> calls;
    void hostHideCursor() override { calls.push_back("hide"); }
    void hostShowCursor() override { calls.push_back("show"); }
    void hostWarpCursor(int x, int y) override {
        calls.push_back("warp:" + std::to_string(x) + "," + std::to_string(y));
    }
    void hostSetPointerGrab(bool g) override { calls.push_back(g ? "grab" : "ungrab"); }
};

void testBeginEndOrdering() {
    FakeHost host;
    NUICursorService svc(host);
    CHECK(!svc.isCaptured());

    svc.beginDragCapture(NUICursorRestorePolicy::KnobCenter, 10, 20);
    CHECK(svc.isCaptured());
    CHECK((host.calls == std::vector<std::string>{"hide", "grab"}));

    svc.endDragCapture(100, 200);
    CHECK(!svc.isCaptured());
    CHECK((host.calls ==
           std::vector<std::string>{"hide", "grab", "warp:100,200", "show", "ungrab"}));
    std::puts("  begin/end ordering: ok");
}

void testGrabOriginPolicy() {
    FakeHost host;
    NUICursorService svc(host);
    svc.beginDragCapture(NUICursorRestorePolicy::GrabOrigin, 33, 44);
    svc.endDragCapture(999, 999); // args must be ignored for GrabOrigin
    CHECK((host.calls ==
           std::vector<std::string>{"hide", "grab", "warp:33,44", "show", "ungrab"}));
    std::puts("  grab-origin policy: ok");
}

void testCancelDoesNotWarp() {
    FakeHost host;
    NUICursorService svc(host);
    svc.beginDragCapture(NUICursorRestorePolicy::KnobCenter, 1, 2);
    svc.cancelDragCapture();
    CHECK(!svc.isCaptured());
    CHECK((host.calls == std::vector<std::string>{"hide", "grab", "show", "ungrab"}));

    // end/cancel when idle are no-ops
    svc.endDragCapture(5, 5);
    svc.cancelDragCapture();
    CHECK((host.calls == std::vector<std::string>{"hide", "grab", "show", "ungrab"}));
    std::puts("  cancel semantics: ok");
}

void testBeginWhileCapturedRecovers() {
    FakeHost host;
    NUICursorService svc(host);
    svc.beginDragCapture(NUICursorRestorePolicy::KnobCenter, 1, 1);
    // Lost release event; a second begin must cancel (show+ungrab, no warp)
    // then start cleanly.
    svc.beginDragCapture(NUICursorRestorePolicy::GrabOrigin, 7, 8);
    CHECK(svc.isCaptured());
    CHECK((host.calls == std::vector<std::string>{"hide", "grab",       // first
                                                  "show", "ungrab",     // recovery cancel
                                                  "hide", "grab"}));    // second
    svc.endDragCapture(0, 0);
    CHECK((host.calls.back() == "ungrab"));
    CHECK((host.calls[host.calls.size() - 3] == "warp:7,8")); // GrabOrigin of second begin
    std::puts("  begin-while-captured recovery: ok");
}

} // namespace

int main() {
    std::puts("CursorServiceTest:");
    testBeginEndOrdering();
    testGrabOriginPolicy();
    testCancelDoesNotWarp();
    testBeginWhileCapturedRecovers();
    if (g_failures == 0) {
        std::puts("CursorServiceTest: PASS");
        return 0;
    }
    std::fprintf(stderr, "CursorServiceTest: FAILED (%d checks)\n", g_failures);
    return 1;
}
