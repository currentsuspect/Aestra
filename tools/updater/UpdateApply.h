// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The swap, with a way back. Header-only and dependency-free (standard library plus the OS), because the updater is a
// tiny program that must keep working while everything else is being replaced.
//
//   preflight   the staged copy holds the program; the install folder can be changed
//   backup      the whole install folder is RENAMED aside (one cheap, nearly atomic step; nothing is copied or deleted)
//   install     the staged folder is RENAMED into its place
//   rollback    the two renames undone, so the old version is exactly as it was
//
// A step that fails halfway undoes the steps before it. The one thing that cannot be undone is the user closing
// something the update needed, and the updater never deletes anything it has not replaced.

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>

#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace AestraUpdater {

namespace fs = std::filesystem;

struct Plan {
    fs::path appDir;     ///< the installed copy, e.g. .../Aestra
    fs::path stagedDir;  ///< the verified new copy, a sibling of appDir (same disk, so a rename is possible)
    fs::path backupDir;  ///< where the old copy waits, also a sibling
    std::string exeName; ///< the main program inside, "Aestra" or "Aestra.exe"
};

enum class Outcome {
    Installed,        ///< the new version is in place; the old one is in backupDir
    StagedInvalid,    ///< nothing changed: the staged copy has no program
    NotWritable,      ///< nothing changed: the install location cannot be modified (no elevation is attempted)
    BackupFailed,     ///< nothing changed: the old copy could not be moved aside (files in use?)
    InstallFailed,    ///< the new copy could not be moved in: the old one is back, exactly as it was
    RollbackFailed,   ///< the worst case: the old copy could not be put back; backupDir still holds it
};

struct Result {
    Outcome outcome = Outcome::Installed;
    std::string message;
    bool ok() const { return outcome == Outcome::Installed; }
};

/// The filesystem operations the swap uses, replaceable so tests can make any one of them fail.
struct Ops {
    std::function<bool(const fs::path&, const fs::path&)> rename = [](const fs::path& from, const fs::path& to) {
        std::error_code ec;
        fs::rename(from, to, ec);
        return !ec;
    };
    std::function<void(int ms)> sleep = [](int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };
    int renameAttempts = 20; ///< a file held open by antivirus or an indexer is usually free again within seconds
    int retryDelayMs = 500;
};

inline bool canModify(const fs::path& dir) {
    // Creating and removing a file is the honest test: permission bits lie on some systems.
    std::error_code ec;
    const fs::path probe = dir / ".aestra-write-test";
    {
        std::ofstream out(probe);
        if (!out) return false;
    }
    fs::remove(probe, ec);
    return true;
}

inline bool renameWithRetry(const Ops& ops, const fs::path& from, const fs::path& to) {
    for (int attempt = 0; attempt < ops.renameAttempts; ++attempt) {
        if (ops.rename(from, to)) return true;
        if (attempt + 1 < ops.renameAttempts) ops.sleep(ops.retryDelayMs);
    }
    return false;
}

inline void makeExecutable(const fs::path& file) {
#ifndef _WIN32
    std::error_code ec;
    fs::permissions(file, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
#else
    (void)file;
#endif
}

inline Result applyUpdate(const Plan& plan, const Ops& ops = Ops()) {
    std::error_code ec;
    if (!fs::is_regular_file(plan.stagedDir / plan.exeName, ec)) return {Outcome::StagedInvalid, "the new version has no " + plan.exeName};
    if (!fs::is_directory(plan.appDir, ec)) return {Outcome::NotWritable, "the installed folder is missing"};
    if (!canModify(plan.appDir.parent_path()) || !canModify(plan.appDir)) {
        return {Outcome::NotWritable, "this install location cannot be changed without administrator rights"};
    }
    makeExecutable(plan.stagedDir / plan.exeName);

    fs::remove_all(plan.backupDir, ec); // the backup of an update from before: this one replaces it
    if (!renameWithRetry(ops, plan.appDir, plan.backupDir)) return {Outcome::BackupFailed, "the current version is in use and could not be moved aside"};

    if (!renameWithRetry(ops, plan.stagedDir, plan.appDir)) {
        // Put the old version back. If even that fails the backup folder still holds everything.
        if (!renameWithRetry(ops, plan.backupDir, plan.appDir)) return {Outcome::RollbackFailed, "the new version could not be installed and the old one could not be restored: it is in " + plan.backupDir.string()};
        return {Outcome::InstallFailed, "the new version could not be moved into place; the old one was restored"};
    }
    return {Outcome::Installed, "installed"};
}

/// Undo a finished swap (the new version misbehaved): the new copy is set aside as <appDir>.failed and the old one returns.
inline Result rollbackUpdate(const Plan& plan, const Ops& ops = Ops()) {
    std::error_code ec;
    if (!fs::is_directory(plan.backupDir, ec)) return {Outcome::RollbackFailed, "there is no previous version to go back to"};
    const fs::path failed = plan.appDir.string() + ".failed";
    fs::remove_all(failed, ec);
    if (fs::exists(plan.appDir, ec) && !renameWithRetry(ops, plan.appDir, failed)) return {Outcome::RollbackFailed, "the new version could not be moved aside"};
    if (!renameWithRetry(ops, plan.backupDir, plan.appDir)) {
        // The new version was moved aside but the old one would not come back: restore the new one rather than leave nothing.
        renameWithRetry(ops, failed, plan.appDir);
        return {Outcome::RollbackFailed, "the previous version could not be restored"};
    }
    fs::remove_all(failed, ec);
    return {Outcome::Installed, "rolled back"};
}

#ifndef _WIN32
// A process that has exited but whose parent has not collected it yet (a zombie) is finished as far as its files go.
inline bool isProcessGone(long pid) {
    if (kill(static_cast<pid_t>(pid), 0) != 0) return errno == ESRCH;
    std::ifstream stat("/proc/" + std::to_string(pid) + "/stat"); // Linux; elsewhere the file is absent and the answer stays "running"
    std::string line;
    if (std::getline(stat, line)) {
        const size_t close = line.rfind(')');
        if (close != std::string::npos && close + 2 < line.size()) return line[close + 2] == 'Z';
    }
    return false;
}
#endif

/// Wait until process @p pid has gone (true) or @p timeoutMs passed (false). A pid that is not running counts as gone.
inline bool waitForExit(long pid, int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
#ifdef _WIN32
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!h) return true; // no such process
    const DWORD wait = WaitForSingleObject(h, static_cast<DWORD>(timeoutMs));
    CloseHandle(h);
    return wait == WAIT_OBJECT_0;
#else
    while (std::chrono::steady_clock::now() < deadline) {
        if (isProcessGone(pid)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return isProcessGone(pid);
#endif
}

inline std::string jsonEscape(const std::string& s) {
    std::string out;
    for (const unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) out += ' ';
        else out += static_cast<char>(c);
    }
    return out;
}

/// What happened, for the app to read next time it starts (and tell the user).
inline void writeResultFile(const fs::path& file, const std::string& from, const std::string& to, bool ok, bool rolledBack,
                            const std::string& message) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::trunc);
    out << "{\"from\":\"" << jsonEscape(from) << "\",\"to\":\"" << jsonEscape(to) << "\",\"ok\":" << (ok ? "true" : "false")
        << ",\"rolledBack\":" << (rolledBack ? "true" : "false") << ",\"message\":\"" << jsonEscape(message) << "\"}\n";
}

} // namespace AestraUpdater
