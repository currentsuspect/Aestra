// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Toasts stack (at most three, newest at the bottom), every one stays the same time, a fourth pushes the oldest
// off, and a repeat of a showing message restarts it instead of stacking a copy (ToastStack.h). Header-only.

#include "../../AestraUI/Widgets/ToastStack.h"

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
    using namespace AestraUI;
    ToastStack s;
    check(s.empty(), "starts empty");

    s.push("Saved");
    s.push("Exported");
    check(s.entries().size() == 2 && s.entries().back().text == "Exported", "messages stack, the newest last (at the bottom)");

    s.push("Placed");
    s.push("Bounced");
    check(s.entries().size() == kMaxToasts, "never more than kMaxToasts at once");
    check(s.entries().front().text == "Exported" && s.entries().back().text == "Bounced",
          "a fourth pushes the oldest off and keeps the newest");

    s.update(1.0);
    s.push("Exported");
    check(s.entries().size() == kMaxToasts && s.entries().back().text == "Exported" && s.entries().back().age == 0.0,
          "a repeat moves the showing one to the bottom and restarts its time");
    int copies = 0;
    for (const auto& e : s.entries()) copies += e.text == "Exported";
    check(copies == 1, "and never stacks a copy of itself");

    ToastStack t;
    t.push("A");
    t.update(1.0);
    t.push("B");
    check(!t.update(kToastSeconds - 1.0 - 0.01), "nothing leaves early");
    check(t.update(0.02) && t.entries().size() == 1 && t.entries().front().text == "B",
          "every toast leaves after the same time, each from when it arrived");
    t.update(kToastSeconds);
    check(t.empty(), "and the stack empties");

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "ToastStackTest: all checks passed\n";
    return 0;
}
