// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Plugin/PluginHostStatus.h"

#include <cstdlib>
#include <mutex>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace Aestra {
namespace Audio {

namespace {
std::mutex g_lastFailureMutex;
std::string g_lastFailure;

// The helper's own refusal codes, in words (AestraPluginHostMain.cpp).
std::string reasonForHelperReply(std::string reply) {
    while (!reply.empty() && (reply.back() == '\r' || reply.back() == '\n')) {
        reply.pop_back();
    }
    if (reply.rfind("ERR ", 0) == 0) {
        reply.erase(0, 4);
    }
    if (reply == "vst3-unsupported-in-this-build") {
        return "this build has no VST3 support";
    }
    if (reply == "vst3-load-failed") {
        return "the VST3 plugin failed to load";
    }
    if (reply.empty()) {
        return "the helper refused the plugin";
    }
    return "helper said: " + reply;
}
} // namespace

std::string defaultExternalPluginHostPath() {
    if (const char* env = std::getenv("AESTRA_PLUGIN_HOST_PATH")) {
        if (*env != '\0') {
            return env;
        }
    }
#ifdef _WIN32
    char exePath[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return (std::filesystem::path(exePath).parent_path() / "AestraPluginHost.exe").string();
    }
#else
    char exePath[4096] = {};
    const ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len > 0) {
        exePath[len] = '\0';
        return (std::filesystem::path(exePath).parent_path() / "AestraPluginHost").string();
    }
#endif
    return "AestraPluginHost";
}

PluginHostCheck externalPluginHostStatus(const std::filesystem::path& path) {
    PluginHostCheck check;
    check.path = path.empty() ? defaultExternalPluginHostPath() : path.string();

    std::error_code ec;
    const auto status = std::filesystem::status(check.path, ec);
    if (ec || !std::filesystem::is_regular_file(status)) {
        check.status = PluginHostStatus::Missing;
        check.message = "Plugin helper not found at " + check.path +
                        ": VST3 and CLAP plugins cannot load (set AESTRA_PLUGIN_HOST_PATH or build AestraPluginHost)";
        return check;
    }
#ifndef _WIN32
    using std::filesystem::perms;
    const auto exec = perms::owner_exec | perms::group_exec | perms::others_exec;
    if ((status.permissions() & exec) == perms::none) {
        check.status = PluginHostStatus::NotExecutable;
        check.message = "Plugin helper at " + check.path + " is not executable: VST3 and CLAP plugins cannot load";
        return check;
    }
#endif
    check.status = PluginHostStatus::Ok;
    check.message = "Plugin helper found at " + check.path;
    return check;
}

PluginHostLoadError describePluginHostLoadFailure(PluginHostLoadFailure kind, const std::string& pluginName,
                                                  const std::string& helperPath, bool helperExists,
                                                  const std::string& reply) {
    PluginHostLoadError error;
    switch (kind) {
    case PluginHostLoadFailure::HelperMissing:
        error.reason = "plugin helper missing";
        break;
    case PluginHostLoadFailure::StartFailed:
        error.reason = "plugin helper would not start";
        break;
    case PluginHostLoadFailure::HelperExited:
        error.reason = "plugin helper stopped while loading it";
        break;
    case PluginHostLoadFailure::Timeout:
        error.reason = "plugin helper timed out";
        break;
    case PluginHostLoadFailure::HelperReplied:
        error.reason = reasonForHelperReply(reply);
        break;
    }
    error.log = "[PluginHost] Could not load plugin '" + pluginName + "': " + error.reason + " (helper: " + helperPath +
                ", " + (helperExists ? "file exists" : "file missing");
    if (kind == PluginHostLoadFailure::HelperReplied) {
        error.log += ", reply: " + (reply.empty() ? std::string("<empty>") : reply);
    }
    error.log += ")";
    return error;
}

void recordPluginHostLoadFailure(const std::string& reason) {
    std::lock_guard<std::mutex> lock(g_lastFailureMutex);
    g_lastFailure = reason;
}

std::string lastPluginHostLoadFailure() {
    std::lock_guard<std::mutex> lock(g_lastFailureMutex);
    return g_lastFailure;
}

} // namespace Audio
} // namespace Aestra
