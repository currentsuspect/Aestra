// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// #889: where Aestra writes its log. One file per run, in the platform's per-user state folder, so a crash report can
// carry the run that crashed and nobody has to know where the app was launched from:
//
//   Linux    $XDG_STATE_HOME/aestra/, else ~/.local/state/aestra/
//   Windows  %LOCALAPPDATA%\Aestra\logs\
//   macOS    ~/Library/Logs/Aestra/
//
// When that folder cannot be created the working directory is used, and the caller says so. Old runs are pruned.

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

namespace Aestra {
namespace RunLog {

struct Location {
    std::filesystem::path directory;
    bool fellBack = false; ///< the state folder could not be created; this is the working directory
};

/// @param getEnv  callable(const char*) -> const char* (nullptr when unset); std::getenv in the app.
template <class GetEnv> Location resolveDirectory(GetEnv&& getEnv) {
    namespace fs = std::filesystem;
    const auto var = [&](const char* name) -> std::string {
        const char* v = getEnv(name);
        return v && *v ? std::string(v) : std::string();
    };
    fs::path dir;
#if defined(_WIN32)
    if (const auto base = var("LOCALAPPDATA"); !base.empty()) dir = fs::path(base) / "Aestra" / "logs";
#elif defined(__APPLE__)
    if (const auto home = var("HOME"); !home.empty()) dir = fs::path(home) / "Library" / "Logs" / "Aestra";
#else
    if (const auto state = var("XDG_STATE_HOME"); !state.empty()) dir = fs::path(state) / "aestra";
    else if (const auto home = var("HOME"); !home.empty()) dir = fs::path(home) / ".local" / "state" / "aestra";
#endif
    std::error_code ec;
    if (!dir.empty()) {
        fs::create_directories(dir, ec);
        if (!ec && fs::is_directory(dir, ec)) return {dir, false};
    }
    return {fs::current_path(ec), true};
}

/// "aestra-YYYYMMDD-HHMMSS.log" for a run that started at @p start (local time).
inline std::string fileNameFor(const std::tm& start) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "aestra-%04d%02d%02d-%02d%02d%02d.log", start.tm_year + 1900, start.tm_mon + 1,
                  start.tm_mday, start.tm_hour, start.tm_min, start.tm_sec);
    return buf;
}

/// Keeps the newest @p keep run logs in @p dir (by name, which sorts by start time); other files are never touched.
inline void pruneOldRuns(const std::filesystem::path& dir, size_t keep) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<fs::path> runs;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("aestra-", 0) == 0 && entry.path().extension() == ".log") runs.push_back(entry.path());
    }
    if (runs.size() <= keep) return;
    std::sort(runs.begin(), runs.end());
    for (size_t i = 0; i + keep < runs.size(); ++i) fs::remove(runs[i], ec);
}

/// The log file for this process: resolved once, so every Log::init in the run writes to the same file.
inline const std::filesystem::path& path(bool* fellBack = nullptr) {
    static bool fallback = false;
    static const std::filesystem::path resolved = [] {
        const Location where = resolveDirectory([](const char* n) { return std::getenv(n); });
        fallback = where.fellBack;
        const std::time_t now = std::time(nullptr);
        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        pruneOldRuns(where.directory, 20);
        return where.directory / fileNameFor(local);
    }();
    if (fellBack) *fellBack = fallback;
    return resolved;
}

} // namespace RunLog
} // namespace Aestra
