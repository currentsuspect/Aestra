// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The records the plugin helper returns for `SCANALL vst3`, and the helper's answers to bad requests. VST3 scanning
// moved out of the app so a scanned plugin is never left mapped in it (40 MB for two Surge XT bundles at idle).
// What matters here: names with the protocol's own separators survive, a malformed record is dropped, and the helper
// refuses what it cannot scan without crashing or loading anything into the caller.

#include "Plugin/PluginScanProtocol.h"

#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <string>

#ifndef HELPER_PATH
#error "HELPER_PATH is set by CMake"
#endif

using namespace Aestra::Audio;

namespace {
int g_failures = 0;
void check(bool ok, const char* name) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++g_failures;
}

// One request to a fresh helper; its first reply line.
std::string ask(const std::string& request) {
    int in[2], out[2];
    if (pipe(in) != 0 || pipe(out) != 0) return {};
    const pid_t pid = fork();
    if (pid == 0) {
        dup2(in[0], STDIN_FILENO);
        dup2(out[1], STDOUT_FILENO);
        close(in[0]); close(in[1]); close(out[0]); close(out[1]);
        execl(HELPER_PATH, HELPER_PATH, "--stdio", static_cast<char*>(nullptr));
        _exit(127);
    }
    close(in[0]); close(out[1]);
    const std::string line = request + "\nEXIT\n";
    if (write(in[1], line.data(), line.size()) < 0) return {};
    close(in[1]);
    std::string reply;
    char ch;
    while (read(out[0], &ch, 1) == 1 && ch != '\n') reply += ch;
    close(out[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    return reply;
}
} // namespace

int main() {
    PluginInfo a;
    a.id = "ABCD,1234;weird";            // the protocol's own separators inside a field
    a.name = "Surge XT, \"Effects\" ; v2";
    a.vendor = "Surge Synth Team";
    a.version = "1.3.4";
    a.category = "Fx|Reverb";
    a.type = PluginType::Effect;
    PluginInfo b;
    b.id = "EF01";
    b.name = "A Synth";
    b.vendor = "V";
    b.version = "2";
    b.category = "Instrument|Synth";
    b.type = PluginType::Instrument;
    b.hasMidiInput = true;

    const std::string wire = ScanProtocol::encodeRecords({a, b});
    check(wire.find(' ') == std::string::npos && wire.find('\n') == std::string::npos, "TheRecordsAreOneLineWithNoSpaces");
    const auto back = ScanProtocol::decodeRecords(wire, "/usr/lib/vst3/X.vst3");
    check(back.size() == 2, "BothRecordsComeBack");
    check(back.size() == 2 && back[0].id == a.id && back[0].name == a.name, "SeparatorsInsideAFieldSurvive");
    check(back.size() == 2 && back[1].type == PluginType::Instrument && back[1].hasMidiInput, "TypeAndMidiInputSurvive");
    check(back.size() == 2 && back[0].format == PluginFormat::VST3 && back[0].path == "/usr/lib/vst3/X.vst3" &&
              back[0].hasEditor && back[0].numAudioOutputs == 2,
          "FormatPathAndScanDefaultsAreSet");

    check(ScanProtocol::decodeRecords("zz,yy", "/x").empty(), "AMalformedRecordIsDropped");
    check(ScanProtocol::decodeRecords("", "/x").empty(), "AnEmptyPayloadIsNoPlugins");
    // An out-of-range type is dropped, not cast into the enum.
    const std::string outOfRange = ScanProtocol::hexEncode("id") + "," + ScanProtocol::hexEncode("n") + "," +
                                   ScanProtocol::hexEncode("v") + "," + ScanProtocol::hexEncode("1") + "," +
                                   ScanProtocol::hexEncode("c") + "," + ScanProtocol::hexEncode("99") + "," +
                                   ScanProtocol::hexEncode("0");
    check(ScanProtocol::decodeRecords(outOfRange, "/x").empty(), "AnOutOfRangePluginTypeIsDropped");

    // The helper itself.
    check(ask("SCANALL vst3 " + ScanProtocol::hexEncode("")).compare(0, 3, "ERR") == 0, "TheHelperRefusesAnEmptyPath");
    const std::string missing = ask("SCANALL vst3 " + ScanProtocol::hexEncode("/no/such/plugin.vst3"));
    check(missing == "OK " || missing.compare(0, 3, "ERR") == 0, "TheHelperAnswersForAPathThatIsNotAPluginWithoutCrashing");
    check(ask("SCANALL au " + ScanProtocol::hexEncode("/x")).compare(0, 3, "ERR") == 0, "TheHelperStillRefusesUnknownFormats");

    if (g_failures) {
        std::printf("%d PluginScanProtocol test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("All PluginScanProtocol tests passed\n");
    return 0;
}
