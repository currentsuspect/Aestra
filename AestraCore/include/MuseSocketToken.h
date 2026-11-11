// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// FD-64 / #1266: the Muse socket is a loopback TCP listener, and loopback restricts the network, not the user. So the
// server writes a random token to a file only the current user can read, and every client presents it as its first
// line. Header-only so the server (AestraAudio) and the clients (`muse`, `muse-agent`) agree on the file's name and
// place without sharing a library.
//
//   Linux    $XDG_RUNTIME_DIR/aestra/muse-<port>.token (already 0700), else $XDG_STATE_HOME/aestra/,
//            else ~/.local/state/aestra/
//   Windows  %LOCALAPPDATA%\Aestra\muse-<port>.token   (the per-user folder is the boundary)
//   macOS    ~/Library/Application Support/Aestra/muse-<port>.token
//
// Wire format: the first line a client sends is  {"auth":"<token>"}  and the server answers
// {"status":"ok","result":{"authenticated":true}}. Anything else first gets {"status":"unauthorized",...}.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Aestra {
namespace MuseToken {

constexpr size_t kTokenBytes = 32;               ///< 256 bits
constexpr size_t kTokenHexLength = kTokenBytes * 2;

inline std::string tokenFileName(uint16_t port) {
    return "muse-" + std::to_string(port) + ".token";
}

/// Folders a token file may live in, best first. The server writes to the first one it can create; a client tries them
/// all, so a changed environment between the two does not strand it. @p getEnv is callable(const char*) -> const char*.
template <class GetEnv> std::vector<std::filesystem::path> candidateDirectories(GetEnv&& getEnv) {
    namespace fs = std::filesystem;
    const auto var = [&](const char* name) -> std::string {
        const char* v = getEnv(name);
        return v && *v ? std::string(v) : std::string();
    };
    std::vector<fs::path> dirs;
#if defined(_WIN32)
    if (const auto base = var("LOCALAPPDATA"); !base.empty()) dirs.push_back(fs::path(base) / "Aestra");
#elif defined(__APPLE__)
    if (const auto home = var("HOME"); !home.empty())
        dirs.push_back(fs::path(home) / "Library" / "Application Support" / "Aestra");
#else
    if (const auto run = var("XDG_RUNTIME_DIR"); !run.empty()) dirs.push_back(fs::path(run) / "aestra");
    if (const auto state = var("XDG_STATE_HOME"); !state.empty()) dirs.push_back(fs::path(state) / "aestra");
    if (const auto home = var("HOME"); !home.empty()) dirs.push_back(fs::path(home) / ".local" / "state" / "aestra");
#endif
    return dirs;
}

inline std::vector<std::filesystem::path> candidateDirectories() {
    return candidateDirectories([](const char* n) { return std::getenv(n); });
}

/// The auth line a client sends first.
inline std::string authLine(const std::string& token) {
    return "{\"auth\":\"" + token + "\"}";
}

/// Equal-length comparison whose time does not depend on where the strings differ.
inline bool constantTimeEquals(const std::string& a, const std::string& b) {
    unsigned char diff = static_cast<unsigned char>(a.size() != b.size());
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

inline bool isWellFormedToken(const std::string& token) {
    if (token.size() != kTokenHexLength) return false;
    for (char c : token) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

/// Read the token for @p port. A non-empty @p overridePath (--token-file) is used alone; otherwise every candidate
/// folder is tried. Returns false with a reason in @p outError.
inline bool loadToken(uint16_t port, const std::string& overridePath, std::string& outToken, std::string& outError) {
    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    if (!overridePath.empty()) {
        files.emplace_back(overridePath);
    } else {
        for (const auto& dir : candidateDirectories()) files.push_back(dir / tokenFileName(port));
    }
    std::string tried;
    for (const auto& file : files) {
        std::ifstream in(file);
        if (!in) {
            tried += (tried.empty() ? "" : ", ") + file.string();
            continue;
        }
        std::string token;
        std::getline(in, token);
        while (!token.empty() && (token.back() == '\r' || token.back() == ' ')) token.pop_back();
        if (!isWellFormedToken(token)) {
            outError = "token file " + file.string() + " does not hold a token";
            return false;
        }
        outToken = token;
        return true;
    }
    outError = "no Muse token file for port " + std::to_string(port) + " (looked at: " +
               (tried.empty() ? std::string("no per-user folder is configured") : tried) +
               "). The app writes it when the socket starts; --token-file <path> names one explicitly.";
    return false;
}

} // namespace MuseToken
} // namespace Aestra
