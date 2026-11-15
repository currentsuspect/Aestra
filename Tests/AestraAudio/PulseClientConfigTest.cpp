// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The PulseAudio client configuration Aestra writes so each audio connection gets a 4 MB shared-memory pool instead of
// 64 MB (128 MB resident across two connections). What matters: it keeps the user's and the system's settings, it sets
// the pool size, and a user's own PULSE_CLIENTCONFIG is never overridden.

#include "Drivers/PulseClientConfig.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace Aestra::Audio;

namespace {
int g_failures = 0;
void check(bool ok, const char* name) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++g_failures;
}
bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
} // namespace

int main() {
    const std::string text = buildPulseClientConfig({"/etc/pulse/client.conf", "/home/u/.config/pulse/client.conf"}, 4194304);
    check(has(text, ".include /etc/pulse/client.conf"), "SystemConfigIsIncluded");
    check(has(text, ".include /home/u/.config/pulse/client.conf"), "UserConfigIsIncluded");
    check(has(text, "shm-size-bytes = 4194304"), "PoolSizeIsSet");
    check(text.find("shm-size-bytes") > text.find(".include /home/u"), "OurValueComesAfterTheIncludesSoItWins");
    check(!has(buildPulseClientConfig({}, 4194304), ".include"), "NoIncludesWhenNoFilesExist");
    check(kPulseClientShmBytes == 4u * 1024u * 1024u, "ThePoolIsFourMegabytes");

#ifdef __linux__
    // A user's own PULSE_CLIENTCONFIG is left alone.
    setenv("PULSE_CLIENTCONFIG", "/tmp/users-own.conf", 1);
    setenv("XDG_RUNTIME_DIR", "/tmp", 1);
    limitPulseClientMemory();
    check(std::string(std::getenv("PULSE_CLIENTCONFIG")) == "/tmp/users-own.conf", "ATheUsersOwnClientConfigIsNeverReplaced");

    // Without one, the generated file is written and pointed at.
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "aestra-pulse-test";
    fs::create_directories(dir);
    unsetenv("PULSE_CLIENTCONFIG");
    setenv("XDG_RUNTIME_DIR", dir.string().c_str(), 1);
    limitPulseClientMemory();
    const char* path = std::getenv("PULSE_CLIENTCONFIG");
    check(path != nullptr && fs::exists(path), "TheClientConfigIsWrittenAndPointedAt");
    if (path != nullptr) {
        std::ifstream in(path);
        std::stringstream body;
        body << in.rdbuf();
        check(has(body.str(), "shm-size-bytes = 4194304"), "TheWrittenFileSetsThePool");
    }
    // No runtime directory: nothing is written and nothing breaks.
    unsetenv("PULSE_CLIENTCONFIG");
    unsetenv("XDG_RUNTIME_DIR");
    limitPulseClientMemory();
    check(std::getenv("PULSE_CLIENTCONFIG") == nullptr, "NoRuntimeDirectoryMeansNoChange");
    fs::remove_all(dir);
#endif

    if (g_failures) {
        std::printf("%d PulseClientConfig test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("All PulseClientConfig tests passed\n");
    return 0;
}
