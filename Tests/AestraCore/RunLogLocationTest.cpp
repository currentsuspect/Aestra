// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// RunLogLocationTest — #889: logs went to the launcher's working directory, two files per run. They now go to one
// file per run in the platform's per-user state folder, falling back to the working directory (and saying so) only
// when that folder cannot be created.

#include "App/RunLog.h"

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

namespace fs = std::filesystem;

namespace {
int g_failures = 0;
void check(bool cond, const std::string& what) {
    std::cout << (cond ? "PASS: " : "FAIL: ") << what << "\n";
    if (!cond)
        ++g_failures;
}
} // namespace

int main() {
    const fs::path scratch = fs::temp_directory_path() / "aestra_runlog_test";
    std::error_code ec;
    fs::remove_all(scratch, ec);
    fs::create_directories(scratch / "home", ec);

    std::map<std::string, std::string> env;
    const auto getEnv = [&](const char* name) -> const char* {
        auto it = env.find(name);
        return it == env.end() ? nullptr : it->second.c_str();
    };

#if !defined(_WIN32) && !defined(__APPLE__)
    env["HOME"] = (scratch / "home").string();
    auto where = Aestra::RunLog::resolveDirectory(getEnv);
    check(where.directory == scratch / "home" / ".local" / "state" / "aestra" && !where.fellBack,
          "Linux default: ~/.local/state/aestra (" + where.directory.string() + ")");

    env["XDG_STATE_HOME"] = (scratch / "state").string();
    where = Aestra::RunLog::resolveDirectory(getEnv);
    check(where.directory == scratch / "state" / "aestra" && fs::is_directory(where.directory),
          "XDG_STATE_HOME wins, and the folder is created");

    env["XDG_STATE_HOME"] = "/proc/aestra-cannot-create-here";
    where = Aestra::RunLog::resolveDirectory(getEnv);
    check(where.fellBack && where.directory == fs::current_path(),
          "an uncreatable folder falls back to the working directory and says so");
#endif

    const std::string name = Aestra::RunLog::fileNameFor(std::tm{0, 5, 4, 3, 1, 126});
    check(name == "aestra-20260203-040500.log", "one file per run, named by its start time (" + name + ")");

    fs::create_directories(scratch / "logs", ec);
    for (int i = 0; i < 25; ++i) {
        std::ofstream(scratch / "logs" / ("aestra-202601" + std::to_string(10 + i) + "-000000.log")) << "x";
    }
    std::ofstream(scratch / "logs" / "keep-me.txt") << "x";
    Aestra::RunLog::pruneOldRuns(scratch / "logs", 20);
    size_t logs = 0;
    for (const auto& e : fs::directory_iterator(scratch / "logs"))
        if (e.path().extension() == ".log")
            ++logs;
    check(logs == 20, "old runs are pruned to the newest 20 (" + std::to_string(logs) + ")");
    check(fs::exists(scratch / "logs" / "aestra-20260134-000000.log") &&
              !fs::exists(scratch / "logs" / "aestra-20260110-000000.log"),
          "the newest are kept, the oldest go");
    check(fs::exists(scratch / "logs" / "keep-me.txt"), "files that are not run logs are never touched");

    fs::remove_all(scratch, ec);
    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
