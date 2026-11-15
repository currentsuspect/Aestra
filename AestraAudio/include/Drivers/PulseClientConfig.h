// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Every PulseAudio connection a client opens (pa_context_new) gets its own shared-memory pool, 64 MB by default,
// and on this stack the pages end up resident: two live connections were 128 MB of Aestra's 477 MB at idle
// (measured 15 Nov 2026 with smaps; `strace -k` shows the allocations come from RtAudio's probeDevices() and
// pa_simple_new()). One audio stream needs a few blocks, not 64 MB. This asks libpulse for a 4 MB pool through its
// client configuration, which is the only knob it has. Measured: total PSS 477 -> 373 MB. Turning shared memory
// off (enable-shm = no) was worse (485 MB): the pool then lives in private memory.
//
// PULSE_CLIENTCONFIG replaces the system and user client.conf rather than adding to them, so the generated file
// includes both, then sets the one value. A user who sets PULSE_CLIENTCONFIG themselves keeps their file.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace Aestra {
namespace Audio {

inline constexpr unsigned kPulseClientShmBytes = 4u * 1024u * 1024u;

/// The text of the client configuration: the given existing files included first, then the pool size.
inline std::string buildPulseClientConfig(const std::vector<std::string>& existingConfigs, unsigned shmBytes) {
    std::string text = "# Written by Aestra: a small shared-memory pool per audio connection (the default is 64 MB).\n";
    for (const auto& path : existingConfigs) {
        text += ".include " + path + "\n";
    }
    text += "shm-size-bytes = " + std::to_string(shmBytes) + "\n";
    return text;
}

/// Linux: point libpulse at a client configuration with a small pool, unless the user already chose one. Call before
/// the first PulseAudio connection (RtAudio's first probe). Silent on any failure: the default is only larger.
inline void limitPulseClientMemory() {
#ifdef __linux__
    if (std::getenv("PULSE_CLIENTCONFIG") != nullptr) {
        return;
    }
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (runtime == nullptr || *runtime == '\0') {
        return;
    }
    namespace fs = std::filesystem;
    std::vector<std::string> existing;
    std::error_code ec;
    if (fs::exists("/etc/pulse/client.conf", ec)) {
        existing.push_back("/etc/pulse/client.conf");
    }
    const char* configHome = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    fs::path user = (configHome != nullptr && *configHome != '\0')
                        ? fs::path(configHome) / "pulse" / "client.conf"
                        : (home != nullptr ? fs::path(home) / ".config" / "pulse" / "client.conf" : fs::path());
    if (!user.empty() && fs::exists(user, ec)) {
        existing.push_back(user.string());
    }
    const fs::path file = fs::path(runtime) / "aestra-pulse-client.conf";
    {
        std::ofstream out(file, std::ios::trunc);
        if (!out) {
            return;
        }
        out << buildPulseClientConfig(existing, kPulseClientShmBytes);
        if (!out) {
            return;
        }
    }
    setenv("PULSE_CLIENTCONFIG", file.string().c_str(), 0);
#endif
}

} // namespace Audio
} // namespace Aestra
