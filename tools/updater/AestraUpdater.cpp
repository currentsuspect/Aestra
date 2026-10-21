// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// AestraUpdater: replaces the installed Aestra with a verified staged copy after Aestra has closed, starts the new one,
// and goes back to the old one if the new one will not even start. The app copies this program to a temporary folder
// before running it, so it is never one of the files being replaced.
//
//   AestraUpdater --pid N --app-dir D --staged S --backup B --exe Aestra[.exe] --result R.json
//                 [--from 0.8.1] [--to 0.8.2] [--watch-seconds 20] [--wait-seconds 60]
//
// Exit codes: 0 updated; 2 not updated, the old version was started again; 3 the old version never closed (nothing changed);
// 4 updated, but the new version closed with an error right away, so the old one was restored and started.

#include "UpdateApply.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace {

using namespace AestraUpdater;

#ifdef _WIN32
struct Child {
    HANDLE process = nullptr;
};

bool launch(const fs::path& dir, const std::string& exe, Child& child) {
    std::wstring cmd = L"\"" + (dir / exe).wstring() + L"\"";
    std::wstring cwd = dir.wstring();
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, cwd.c_str(), &si, &pi)) return false;
    CloseHandle(pi.hThread);
    child.process = pi.hProcess;
    return true;
}

// true = it ended (exitCode set) within the window; false = still running after it
bool watch(Child& child, int seconds, int& exitCode) {
    const DWORD wait = WaitForSingleObject(child.process, static_cast<DWORD>(seconds) * 1000);
    if (wait != WAIT_OBJECT_0) return false;
    DWORD code = 0;
    GetExitCodeProcess(child.process, &code);
    exitCode = static_cast<int>(code);
    return true;
}

void release(Child& child) {
    if (child.process) CloseHandle(child.process);
    child.process = nullptr;
}
#else
struct Child {
    pid_t pid = -1;
};

bool launch(const fs::path& dir, const std::string& exe, Child& child) {
    const std::string program = (dir / exe).string();
    const std::string cwd = dir.string();
    const pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        if (chdir(cwd.c_str()) != 0) _exit(127);
        execl(program.c_str(), program.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    child.pid = pid;
    return true;
}

bool watch(Child& child, int seconds, int& exitCode) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        int status = 0;
        const pid_t done = waitpid(child.pid, &status, WNOHANG);
        if (done == child.pid) {
            exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 1);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

void release(Child&) {}
#endif

std::map<std::string, std::string> parseArgs(int argc, char** argv) {
    std::map<std::string, std::string> args;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (std::strncmp(argv[i], "--", 2) == 0) args[argv[i] + 2] = argv[i + 1];
    }
    return args;
}

} // namespace

int main(int argc, char** argv) {
    auto args = parseArgs(argc, argv);
    for (const char* required : {"app-dir", "staged", "backup", "exe", "result"}) {
        if (!args.count(required)) {
            std::cerr << "AestraUpdater: missing --" << required << "\n";
            return 64;
        }
    }
    Plan plan;
    plan.appDir = args["app-dir"];
    plan.stagedDir = args["staged"];
    plan.backupDir = args["backup"];
    plan.exeName = args["exe"];
    const fs::path resultFile = args["result"];
    const std::string from = args.count("from") ? args["from"] : "", to = args.count("to") ? args["to"] : "";
    const int watchSeconds = args.count("watch-seconds") ? std::max(0, std::atoi(args["watch-seconds"].c_str())) : 20;
    const int waitSeconds = args.count("wait-seconds") ? std::max(1, std::atoi(args["wait-seconds"].c_str())) : 60;
    const long pid = args.count("pid") ? std::atol(args["pid"].c_str()) : 0;

    // 1. Aestra must be gone: its files are the ones about to move.
    if (pid > 0 && !waitForExit(pid, waitSeconds * 1000)) {
        writeResultFile(resultFile, from, to, false, false, "Aestra did not close in time, so nothing was changed");
        return 3;
    }

    // 2. The swap.
    const Result swap = applyUpdate(plan);
    if (!swap.ok()) {
        const bool oldIsBack = swap.outcome != Outcome::RollbackFailed;
        writeResultFile(resultFile, from, to, false, swap.outcome == Outcome::InstallFailed, swap.message);
        if (oldIsBack) {
            Child old;
            launch(plan.appDir, plan.exeName, old); // the version they had keeps working
            release(old);
        }
        return 2;
    }

    // 3. Say it worked BEFORE the new version starts, so the new version can announce "updated" the moment it opens (the
    //    watch below can take 20 s). If it then fails to start, the note is overwritten with the failure.
    writeResultFile(resultFile, from, to, true, false, "updated");

    // 4. Start the new version and watch it start.
    Child fresh;
    int exitCode = 0;
    const bool started = launch(plan.appDir, plan.exeName, fresh);
    const bool endedEarly = started && watchSeconds > 0 && watch(fresh, watchSeconds, exitCode);
    if (!started || (endedEarly && exitCode != 0)) {
        release(fresh);
        const Result back = rollbackUpdate(plan);
        const std::string why = !started ? "the new version could not be started"
                                         : "the new version closed with an error right after starting";
        writeResultFile(resultFile, from, to, false, back.ok(), why + (back.ok() ? "; the previous version was restored" : "; the previous version could not be restored: " + back.message));
        if (back.ok()) {
            Child old;
            launch(plan.appDir, plan.exeName, old);
            release(old);
        }
        return 4;
    }
    release(fresh);
    return 0;
}
