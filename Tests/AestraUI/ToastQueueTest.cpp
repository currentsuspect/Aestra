// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V8-C14 ranked gap 4, "toasts overwrite each other": a message that arrives while another
// is showing waits its turn (ToastQueue.h). Header-only, links nothing.

#include "../../AestraUI/Widgets/ToastQueue.h"

#include <iostream>
#include <string>

namespace {
int g_failures = 0;
void check(bool c, const std::string& what) {
    if (!c) {
        std::cerr << "[FAIL] " << what << '\n';
        ++g_failures;
    }
}
} // namespace

int main() {
    using AestraUI::ToastQueue;
    ToastQueue q;

    auto now = q.offer({"Saved", 2.0}, nullptr);
    check(now && now->text == "Saved", "with nothing showing, a toast shows at once");

    const std::string showing = "Saved";
    check(!q.offer({"Export finished", 3.0}, &showing), "a different toast does not replace the one showing");
    check(q.waiting() == 1, "it waits instead");
    check(!q.offer({"Export finished", 3.0}, &showing), "an exact repeat of the waiting toast");
    check(q.waiting() == 1, "is not queued twice");

    auto same = q.offer({"Saved", 2.0}, &showing);
    check(same && same->text == "Saved", "the same text as the one showing restarts it rather than queueing");

    auto next = q.next();
    check(next && next->text == "Export finished" && next->seconds == 3.0, "the waiting toast comes next, intact");
    check(!q.next(), "then nothing is waiting");

    for (int i = 0; i < 5; ++i) q.offer({"msg " + std::to_string(i), 1.0}, &showing);
    check(q.waiting() == ToastQueue::kMaxQueued, "a burst keeps at most kMaxQueued waiting");
    check(q.next()->text == "msg 2", "and drops the oldest, keeping the newest");

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "ToastQueueTest: all checks passed\n";
    return 0;
}
