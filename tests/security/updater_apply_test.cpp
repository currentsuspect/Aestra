// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The swap and its way back (tools/updater/UpdateApply.h): the old version moves aside whole, the new one moves in, and
// any step that fails puts things back exactly as they were. Each filesystem operation can be made to fail on demand.

#include "UpdateApply.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace AestraUpdater;

namespace {

int g_failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cerr << "failed: " << what << "\n";
        ++g_failures;
    }
}

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
void put(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

struct World {
    fs::path root;
    Plan plan;
    World() {
        root = fs::temp_directory_path() / ("aestra-updater-test-" + std::to_string(std::rand()) + "-" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        fs::create_directories(root);
        plan.appDir = root / "Aestra";
        plan.stagedDir = root / "Aestra.new";
        plan.backupDir = root / "Aestra.old";
        plan.exeName = "Aestra";
        put(plan.appDir / "Aestra", "OLD program");
        put(plan.appDir / "AestraAssets" / "a.txt", "old asset");
        put(plan.appDir / "my-own-notes.txt", "a file the user added");
        put(plan.stagedDir / "Aestra", "NEW program");
        put(plan.stagedDir / "AestraAssets" / "a.txt", "new asset");
    }
    ~World() {
        std::error_code ec;
        fs::permissions(root, fs::perms::owner_all, fs::perm_options::replace, ec);
        fs::remove_all(root, ec);
    }
    bool oldIsInPlace() const { return slurp(plan.appDir / "Aestra") == "OLD program" && slurp(plan.appDir / "AestraAssets" / "a.txt") == "old asset" && fs::exists(plan.appDir / "my-own-notes.txt"); }
};

Ops fast() {
    Ops ops;
    ops.sleep = [](int) {};
    return ops;
}

} // namespace

int main() {
    // The good case.
    {
        World w;
        const Result r = applyUpdate(w.plan, fast());
        check(r.ok(), "the swap succeeds: " + r.message);
        check(slurp(w.plan.appDir / "Aestra") == "NEW program" && slurp(w.plan.appDir / "AestraAssets" / "a.txt") == "new asset", "the new version is in place");
        check(slurp(w.plan.backupDir / "Aestra") == "OLD program" && fs::exists(w.plan.backupDir / "my-own-notes.txt"), "the old version, and the user's own file, wait in the backup");
        check(!fs::exists(w.plan.stagedDir), "the staging folder was moved in, not copied");
    }
    // The backup of an earlier update does not get in the way.
    {
        World w;
        put(w.plan.backupDir / "stale.txt", "from two updates ago");
        check(applyUpdate(w.plan, fast()).ok() && !fs::exists(w.plan.backupDir / "stale.txt") && slurp(w.plan.backupDir / "Aestra") == "OLD program", "an older backup is replaced by this one");
    }
    // Nothing changes when the new copy is not a program.
    {
        World w;
        fs::remove(w.plan.stagedDir / "Aestra");
        const Result r = applyUpdate(w.plan, fast());
        check(r.outcome == Outcome::StagedInvalid && w.oldIsInPlace() && fs::exists(w.plan.stagedDir), "a staged copy without the program changes nothing");
    }
    // The install location cannot be written.
    {
        World w;
        bool changed = false;
        std::error_code ec;
        fs::permissions(w.root, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace, ec);
        {
            std::ofstream probe(w.root / "probe");
            changed = !probe; // the permission change took (it does not when run as root)
        }
        if (changed) {
            const Result r = applyUpdate(w.plan, fast());
            check(r.outcome == Outcome::NotWritable, "a read-only install location is refused (no elevation tried)");
            fs::permissions(w.root, fs::perms::owner_all, fs::perm_options::replace, ec);
            check(w.oldIsInPlace(), "and nothing changed");
        }
    }
    // The old copy is busy for a while, then free: retried.
    {
        World w;
        int failures = 3, calls = 0;
        Ops ops = fast();
        const auto real = ops.rename;
        ops.rename = [&](const fs::path& from, const fs::path& to) {
            ++calls;
            if (from == w.plan.appDir && failures-- > 0) return false; // "in use"
            return real(from, to);
        };
        check(applyUpdate(w.plan, ops).ok() && calls >= 5, "a busy folder is retried until it is free");
    }
    // The old copy never frees up: nothing changed.
    {
        World w;
        Ops ops = fast();
        ops.renameAttempts = 4;
        const auto real = ops.rename;
        ops.rename = [&](const fs::path& from, const fs::path& to) { return from == w.plan.appDir ? false : real(from, to); };
        const Result r = applyUpdate(w.plan, ops);
        check(r.outcome == Outcome::BackupFailed && w.oldIsInPlace() && fs::exists(w.plan.stagedDir / "Aestra"), "a folder that stays busy changes nothing");
    }
    // The new copy cannot be moved in: the old one comes back exactly.
    {
        World w;
        Ops ops = fast();
        ops.renameAttempts = 3;
        const auto real = ops.rename;
        ops.rename = [&](const fs::path& from, const fs::path& to) { return from == w.plan.stagedDir ? false : real(from, to); };
        const Result r = applyUpdate(w.plan, ops);
        check(r.outcome == Outcome::InstallFailed, "a failed install says so");
        check(w.oldIsInPlace() && !fs::exists(w.plan.backupDir), "the old version is back exactly as it was");
        check(fs::exists(w.plan.stagedDir / "Aestra"), "and the staged copy is still there to try again");
    }
    // The worst case: the old copy cannot come back either. Nothing is deleted; the backup holds it.
    {
        World w;
        Ops ops = fast();
        ops.renameAttempts = 2;
        const auto real = ops.rename;
        ops.rename = [&](const fs::path& from, const fs::path& to) { return (from == w.plan.stagedDir || from == w.plan.backupDir) ? false : real(from, to); };
        const Result r = applyUpdate(w.plan, ops);
        check(r.outcome == Outcome::RollbackFailed && r.message.find(w.plan.backupDir.string()) != std::string::npos, "the worst case says where the old version is");
        check(slurp(w.plan.backupDir / "Aestra") == "OLD program", "and it is intact there");
    }
    // Going back after a finished swap.
    {
        World w;
        check(applyUpdate(w.plan, fast()).ok(), "(setup) swapped");
        const Result r = rollbackUpdate(w.plan, fast());
        check(r.ok() && w.oldIsInPlace(), "rollback restores the old version exactly");
        check(!fs::exists(w.plan.backupDir) && !fs::exists(w.plan.appDir.string() + ".failed"), "and leaves no litter");
    }
    {
        World w;
        check(rollbackUpdate(w.plan, fast()).outcome == Outcome::RollbackFailed && w.oldIsInPlace(), "rollback with no backup does nothing and says so");
    }
    // Waiting for the app to close.
    {
        check(waitForExit(2147483000L, 200), "a process that is not running counts as closed");
#ifndef _WIN32
        check(!waitForExit(static_cast<long>(getpid()), 150), "a process that is still running is waited for, then reported as running");
#endif
    }
    // The note the app reads next time.
    {
        World w;
        writeResultFile(w.root / "u" / "last.json", "0.8.1", "0.8.2", false, true, "said \"no\"\nnewline");
        const std::string text = slurp(w.root / "u" / "last.json");
        check(text.find("\"ok\":false") != std::string::npos && text.find("\"rolledBack\":true") != std::string::npos && text.find("\\\"no\\\"\\nnewline") != std::string::npos, "the result file is valid and escaped");
    }

    if (g_failures == 0) std::cout << "updater apply: all checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
