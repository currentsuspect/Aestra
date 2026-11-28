// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// SandboxHelperPriorityTest — the helper's audio thread runs at real-time priority where the system allows it.
//
// The app runs its own sandbox worker at SCHED_FIFO so it is never held back by ordinary work. The helper's audio thread, which
// does the plugin's processing on the other side of that hand-over, ran at normal priority and lost to everything else on a busy
// machine, including the plugin's own GUI threads: a Vital editor open with the CPU busy made 10% of blocks late. Where the user
// may use real-time priority (RLIMIT_RTPRIO > 0) at least one helper thread must be SCHED_FIFO or SCHED_RR. Elsewhere there
// is nothing to check, and the test skips.

#include <csignal>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
// Field 41 of /proc/<pid>/task/<tid>/stat is the scheduling policy (0 other, 1 FIFO, 2 RR). The command name sits in
// parentheses and may contain spaces, so count from the closing parenthesis.
int policyOf(const std::string& statPath) {
    std::ifstream in(statPath);
    std::string line;
    std::getline(in, line);
    const size_t close = line.rfind(')');
    if (close == std::string::npos) return -1;
    std::istringstream rest(line.substr(close + 2));
    std::string field;
    for (int i = 3; i <= 41; ++i) {
        if (!(rest >> field)) return -1;
    }
    return std::atoi(field.c_str());
}
} // namespace

int main() {
    rlimit rt{};
    if (getrlimit(RLIMIT_RTPRIO, &rt) != 0 || rt.rlim_cur == 0) {
        std::cout << "real-time priority is not allowed here (RLIMIT_RTPRIO is 0); skipping.\n";
        return 77;
    }

    int in[2], out[2];
    if (pipe(in) != 0 || pipe(out) != 0) return 1;
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

    // PING returns only once the helper is up and its threads exist.
    const std::string ping = "PING\n";
    if (write(in[1], ping.data(), ping.size()) < 0) return 1;
    std::string reply;
    char ch;
    while (read(out[0], &ch, 1) == 1 && ch != '\n') reply += ch;

    int realtime = 0, threads = 0;
    const std::string taskDir = "/proc/" + std::to_string(pid) + "/task";
    if (DIR* dir = opendir(taskDir.c_str())) {
        while (const dirent* entry = readdir(dir)) {
            if (entry->d_name[0] == '.') continue;
            ++threads;
            const int policy = policyOf(taskDir + "/" + entry->d_name + "/stat");
            if (policy == 1 || policy == 2) ++realtime;
        }
        closedir(dir);
    }

    const std::string exitCommand = "EXIT\n";
    if (write(in[1], exitCommand.data(), exitCommand.size()) < 0) return 1;
    close(in[1]);
    close(out[0]);
    int status = 0;
    waitpid(pid, &status, 0);

    std::cout << "helper answered \"" << reply << "\" with " << threads << " threads, " << realtime << " at real-time priority\n";
    if (threads < 2) {
        std::cout << "FAIL: expected the main thread and the audio thread\n";
        return 1;
    }
    if (realtime < 1) {
        std::cout << "FAIL: no helper thread runs at real-time priority although the user may use it\n";
        return 1;
    }
    std::cout << "PASS: the helper's audio thread runs at real-time priority\n";
    return 0;
}
