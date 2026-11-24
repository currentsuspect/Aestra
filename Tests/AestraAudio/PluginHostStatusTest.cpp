// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The out-of-process plugin helper's availability check and the wording of a failed plugin load.
//
// Founder report: dragging Surge XT into Arsenal did nothing. The log said only "Helper failed to load plugin"; the
// build had no AestraPluginHost beside the app. These pin the pieces that now say so, with no helper and no plugin.

#include "Plugin/PluginHostStatus.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifndef _WIN32
#include <sys/stat.h>
#endif

using namespace Aestra::Audio;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok) ++g_failures;
}
bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

void testStatus() {
    const fs::path dir = fs::temp_directory_path() / "aestra_plugin_host_status_test";
    fs::remove_all(dir);
    fs::create_directories(dir);

    const auto missing = externalPluginHostStatus(dir / "AestraPluginHost");
    check(missing.status == PluginHostStatus::Missing, "missing file reports Missing");
    check(contains(missing.message, (dir / "AestraPluginHost").string()), "Missing message names the path");
    check(contains(missing.message, "VST3") && contains(missing.message, "CLAP"), "Missing message says what breaks");

    const auto asDirectory = externalPluginHostStatus(dir);
    check(asDirectory.status == PluginHostStatus::Missing, "a directory is not a helper");

    const fs::path helper = dir / "AestraPluginHost";
    std::ofstream(helper) << "#!/bin/sh\n";
#ifndef _WIN32
    chmod(helper.c_str(), 0644);
    const auto plain = externalPluginHostStatus(helper);
    check(plain.status == PluginHostStatus::NotExecutable, "file without the execute bit reports NotExecutable");
    check(contains(plain.message, "not executable"), "NotExecutable message says so");
    chmod(helper.c_str(), 0755);
#endif
    const auto ok = externalPluginHostStatus(helper);
    check(ok.status == PluginHostStatus::Ok, "executable file reports Ok");
    check(ok.path == helper.string(), "Ok carries the resolved path");

    fs::remove_all(dir);
}

void testMessages() {
    const std::string path = "/opt/aestra/AestraPluginHost";

    auto missing = describePluginHostLoadFailure(PluginHostLoadFailure::HelperMissing, "Surge XT", path, false);
    check(missing.reason == "plugin helper missing", "missing: short reason");
    check(contains(missing.log, "Surge XT") && contains(missing.log, path) && contains(missing.log, "file missing"),
          "missing: log names plugin, path, and that the file is missing");

    auto start = describePluginHostLoadFailure(PluginHostLoadFailure::StartFailed, "Surge XT", path, true);
    check(contains(start.reason, "would not start"), "start failed: short reason");
    check(contains(start.log, "file exists"), "start failed: log says the file exists");

    auto exited = describePluginHostLoadFailure(PluginHostLoadFailure::HelperExited, "Surge XT", path, true);
    check(contains(exited.reason, "stopped"), "exited: short reason");

    auto timeout = describePluginHostLoadFailure(PluginHostLoadFailure::Timeout, "Surge XT", path, true);
    check(contains(timeout.reason, "timed out"), "timeout: short reason");

    auto vst3 = describePluginHostLoadFailure(PluginHostLoadFailure::HelperReplied, "Surge XT", path, true,
                                              "ERR vst3-unsupported-in-this-build\r\n");
    check(contains(vst3.reason, "no VST3 support"), "reply: vst3-unsupported-in-this-build is put in words");
    check(contains(vst3.log, "ERR vst3-unsupported-in-this-build"), "reply: the helper's own text is in the log");

    auto failed = describePluginHostLoadFailure(PluginHostLoadFailure::HelperReplied, "Surge XT", path, true,
                                                "ERR vst3-load-failed");
    check(contains(failed.reason, "failed to load"), "reply: vst3-load-failed is put in words");

    auto odd = describePluginHostLoadFailure(PluginHostLoadFailure::HelperReplied, "Surge XT", path, true,
                                             "ERR something-new");
    check(contains(odd.reason, "something-new"), "reply: an unknown reason is passed through, not swallowed");

    check(missing.reason != start.reason && start.reason != exited.reason && exited.reason != timeout.reason,
          "each failure class reads differently");
}

void testLastFailure() {
    recordPluginHostLoadFailure("plugin helper missing");
    check(lastPluginHostLoadFailure() == "plugin helper missing", "the last failure is kept for the caller");
    recordPluginHostLoadFailure({});
    check(lastPluginHostLoadFailure().empty(), "a success clears it");
}

} // namespace

int main() {
    std::cout << "PluginHostStatusTest\n";
    testStatus();
    testMessages();
    testLastFailure();
    return g_failures == 0 ? 0 : 1;
}
