// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// muse — one-shot CLI for Aestra's Muse socket.
//
// MuseRepl speaks JSONL on stdin/stdout and muse-agent hands the surface to a
// model. Neither is what you want when a *person* (or a coding agent) is at a
// terminal driving a session by hand: those need a request framed, a socket
// opened, and a line parsed for every single verb. This is that missing piece —
// one verb per invocation, flags instead of hand-written JSON, the response on
// stdout, and the outcome in the exit status:
//
//   muse list_units
//   muse add_unit --type sampler --name Kick
//   muse set_bpm --value 140
//   muse render_pattern --pattern 1 --file /tmp/loop.wav
//
// It talks to whatever is listening: the running GUI app (started with
// AESTRA_MUSE_PORT set) or a headless `MuseRepl --port N`. Edits against the
// live app land on the main thread through the same command system as user
// edits, so they share the undo history.
//
// Deliberately does NOT link libcurl or any provider code — this is a socket
// client and a JSON formatter, nothing more.

#include "MuseCliRequest.h"
#include "MuseSocketClient.h"

#include "AestraJSON.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using Aestra::JSON;

constexpr int EXIT_OK = 0;
constexpr int EXIT_REQUEST_FAILED = 1; // the service answered, and said no
constexpr int EXIT_USAGE = 2;         // never reached the service

void printUsage() {
    std::cout <<
        "muse — one verb per invocation against a running Aestra (or MuseRepl).\n"
        "\n"
        "Usage:\n"
        "  muse [options] <verb> [--flag value ...]\n"
        "\n"
        "Options:\n"
        "  --port <port>        Muse socket port (default: $AESTRA_MUSE_PORT)\n"
        "  --host <host>        default 127.0.0.1\n"
        "  --args-json <json>   args object verbatim; for verbs flat flags cannot\n"
        "                       express (batch takes an array of commands)\n"
        "  --result             print only the result field, not the envelope\n"
        "  --raw                print the response line exactly as received\n"
        "  --compact            single-line JSON (default is indented)\n"
        "  --timeout <seconds>  give up waiting for a response (default 120,\n"
        "                       0 waits forever). A bounce of a long timeline\n"
        "                       is slow but finite; a stalled peer is not.\n"
        "  -h, --help\n"
        "\n"
        "Flag values are typed by shape: true/false become booleans, numbers\n"
        "become numbers, everything else stays a string. A flag with no value\n"
        "is a boolean true.\n"
        "\n"
        "Exit status: 0 the verb succeeded, 1 the service refused it, 2 it was\n"
        "never asked (bad usage, or nothing listening).\n"
        "\n"
        "Start something to talk to:\n"
        "  AESTRA_MUSE_PORT=41952 ./Aestra      # drive the live app\n"
        "  ./MuseRepl --port 41952              # headless\n"
        "\n"
        "`muse get_schema` lists every verb, its flags, and the semantics that\n"
        "are not visible in the protocol.\n";
}

bool parsePort(const std::string& text, uint16_t& out) {
    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || parsed <= 0 || parsed > 65535) {
        return false;
    }
    out = static_cast<uint16_t>(parsed);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> tokens(argv + 1, argv + argc);

    Aestra::MuseAgent::CliInvocation invocation;
    std::string error;
    if (!Aestra::MuseAgent::parseMuseCli(tokens, invocation, error)) {
        std::cerr << error << "\n\n";
        printUsage();
        return EXIT_USAGE;
    }
    if (invocation.helpRequested) {
        printUsage();
        return EXIT_OK;
    }

    if (invocation.portText.empty()) {
        if (const char* env = std::getenv("AESTRA_MUSE_PORT")) {
            invocation.portText = env;
        }
    }
    if (invocation.portText.empty()) {
        std::cerr << "no port: pass --port, or set AESTRA_MUSE_PORT.\n"
                     "Start the app with AESTRA_MUSE_PORT=<port> to drive the live\n"
                     "session, or run MuseRepl --port <port> for a headless one.\n";
        return EXIT_USAGE;
    }

    uint16_t port = 0;
    if (!parsePort(invocation.portText, port)) {
        std::cerr << "invalid port: " << invocation.portText << "\n";
        return EXIT_USAGE;
    }

    Aestra::MuseAgent::MuseSocketClient client;
    if (!client.connect(invocation.host, port, error)) {
        std::cerr << "cannot reach a Muse socket at " << invocation.host << ":" << port
                  << " — " << error << "\n"
                  << "Is the app running with AESTRA_MUSE_PORT set, or MuseRepl --port?\n";
        return EXIT_USAGE;
    }

    client.setReadTimeoutMs(invocation.timeoutSeconds * 1000);

    Aestra::MuseAgent::MuseSocketClient::Outcome outcome{};
    const std::string line =
        client.request(Aestra::MuseAgent::buildMuseRequest(invocation).toString(), &outcome);

    if (outcome == Aestra::MuseAgent::MuseSocketClient::Outcome::TimedOut) {
        std::cerr << "timed out after " << invocation.timeoutSeconds << "s waiting for "
                  << invocation.verb << " — raise --timeout, or 0 to wait indefinitely\n";
        return EXIT_USAGE;
    }
    if (outcome == Aestra::MuseAgent::MuseSocketClient::Outcome::Disconnected || line.empty()) {
        std::cerr << "no response from " << invocation.host << ":" << port
                  << " (the session may have exited mid-request)\n";
        return EXIT_USAGE;
    }

    // Validate before printing, and identically in both modes. --raw controls
    // the *formatting* of a well-formed envelope; it is not a licence to pass
    // protocol garbage through with a status derived from a lenient parse.
    bool consumedAll = false;
    JSON response = JSON::parseStrict(line, consumedAll);
    if (!consumedAll || !response.isObject() || !response.has("status")) {
        std::cerr << "malformed response (not a single JSON envelope): " << line << "\n";
        return EXIT_USAGE;
    }
    const bool ok = response["status"].asString() == "ok";

    if (invocation.raw) {
        std::cout << line << std::endl;
        return ok ? EXIT_OK : EXIT_REQUEST_FAILED;
    }

    const int indent = invocation.compact ? 0 : 2;
    if (invocation.resultOnly && response.has("result")) {
        std::cout << response["result"].toString(indent) << std::endl;
    } else {
        std::cout << response.toString(indent) << std::endl;
    }

    return ok ? EXIT_OK : EXIT_REQUEST_FAILED;
}
