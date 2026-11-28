// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// SandboxHelperExitTest — the sandbox helper exits promptly when it is told to, however its input arrives.
//
// The helper has a pipe thread that reads commands and hands most of them to the main thread. EXIT used to `return` from
// main() while the pipe thread was blocked waiting for EXIT to finish, so main()'s locals (the hand-over's mutex and
// condition variable) were destroyed under a waiting thread and the process never exited. ClapScanAllTest hit it in CI: it
// sends "SCANALL ...\nEXIT\n" in one write, closes stdin and waits for the helper to end.

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include <sys/wait.h>
#include <unistd.h>

namespace {
int g_failures = 0;

void check(bool condition, const std::string& label) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << label << std::endl;
    if (!condition) {
        ++g_failures;
    }
}

struct Outcome {
    bool exited = false;
    int status = -1;
    std::string firstLine;
};

// Starts the helper, writes @p input in one go, closes stdin, and waits up to @p limitMs for it to end.
Outcome run(const std::string& input, int limitMs) {
    Outcome result;
    int in[2], out[2];
    if (pipe(in) != 0 || pipe(out) != 0) return result;
    const pid_t pid = fork();
    if (pid == 0) {
        dup2(in[0], STDIN_FILENO);
        dup2(out[1], STDOUT_FILENO);
        close(in[0]); close(in[1]); close(out[0]); close(out[1]);
        execl(HELPER_PATH, HELPER_PATH, "--stdio", static_cast<char*>(nullptr));
        _exit(127);
    }
    close(in[0]);
    close(out[1]);
    if (write(in[1], input.data(), input.size()) < 0) return result;
    close(in[1]);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(limitMs);
    int status = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        if (waitpid(pid, &status, WNOHANG) == pid) {
            result.exited = true;
            result.status = status;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!result.exited) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
    }
    char ch;
    while (read(out[0], &ch, 1) == 1 && ch != '\n') result.firstLine += ch;
    close(out[0]);
    return result;
}
} // namespace

int main() {
    // Pipelined commands then end of input, as ClapScanAllTest sends them. Several runs, because it is a race.
    int hung = 0, badStatus = 0;
    for (int i = 0; i < 12; ++i) {
        const Outcome o = run("PING\nEXIT\n", 3000);
        if (!o.exited) ++hung;
        else if (!WIFEXITED(o.status) || WEXITSTATUS(o.status) != 0) ++badStatus;
    }
    check(hung == 0, "EXIT sent right behind another command, then end of input, always lets the helper exit (hung: " +
                         std::to_string(hung) + " of 12)");
    check(badStatus == 0, "and it exits with status 0");

    const Outcome alone = run("EXIT\n", 3000);
    check(alone.exited && alone.firstLine == "OK EXIT", "EXIT on its own answers OK EXIT and the helper exits");

    // End of input without EXIT also ends the helper.
    const Outcome eof = run("PING\n", 3000);
    check(eof.exited, "end of input alone ends the helper");

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "sandbox helper exit passed\n";
    return 0;
}
