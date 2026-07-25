// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Argv -> Muse request framing for the `muse` CLI. No socket, no service: this
// covers the half that turns a command line into a request, which is where the
// mistakes are silent — JSON::set no-ops on a non-object, so a request built on
// a default-constructed JSON loses every field without a diagnostic and only
// fails much later, at the far end of a socket.

#include "MuseCliRequest.h"

#include <iostream>
#include <string>
#include <vector>

using Aestra::JSON;
using Aestra::MuseAgent::buildMuseRequest;
using Aestra::MuseAgent::CliInvocation;
using Aestra::MuseAgent::parseMuseCli;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    }
}

CliInvocation parseOrDie(const std::vector<std::string>& tokens) {
    CliInvocation invocation;
    std::string error;
    if (!parseMuseCli(tokens, invocation, error)) {
        std::cerr << "FAIL: unexpected parse error: " << error << "\n";
        ++g_failures;
    }
    return invocation;
}

bool parseFails(const std::vector<std::string>& tokens, std::string& outError) {
    CliInvocation invocation;
    return !parseMuseCli(tokens, invocation, outError);
}

} // namespace

int main() {
    // --- the envelope is an object, and carries the args -------------------
    {
        const CliInvocation inv = parseOrDie({"add_unit", "--type", "sampler", "--name", "Kick"});
        check(inv.verb == "add_unit", "verb is read from the first bare token");

        const JSON request = buildMuseRequest(inv);
        check(request.isObject(), "request envelope is a JSON object");
        check(request.has("verb") && request["verb"].asString() == "add_unit",
              "envelope carries the verb");
        check(request.has("args"), "envelope carries args when flags were given");
        check(request["args"]["type"].asString() == "sampler", "string flag survives framing");
        check(request["args"]["name"].asString() == "Kick", "second string flag survives framing");

        // The regression that motivated this test: with a default-constructed
        // JSON, set() silently no-ops and the service sees no args at all.
        const std::string text = request.toString();
        check(text.find("\"sampler\"") != std::string::npos,
              "serialized request actually contains the arg value");
    }

    // --- a verb with no flags sends no args key ----------------------------
    {
        const JSON request = buildMuseRequest(parseOrDie({"get_transport"}));
        check(!request.has("args"), "no args key when the verb took no flags");
    }

    // --- values are typed by shape -----------------------------------------
    {
        const CliInvocation inv =
            parseOrDie({"set_note", "--pitch", "60", "--velocity", "0.5", "--start", "-2.25"});
        const JSON args = buildMuseRequest(inv)["args"];
        check(args["pitch"].isNumber() && args["pitch"].asNumber() == 60.0, "integer becomes a number");
        check(args["velocity"].isNumber() && args["velocity"].asNumber() == 0.5,
              "decimal becomes a number");
        check(args["start"].isNumber() && args["start"].asNumber() == -2.25,
              "negative decimal becomes a number");

        // Integer-valued flags must not serialize as "60.000000" — the grammar's
        // int validators reject that shape.
        const std::string text = args.toString();
        check(text.find("60.0") == std::string::npos, "integers serialize integrally");
        check(text.find("60") != std::string::npos, "the integer is still present");
    }

    // --- partial numbers stay strings rather than silently truncating ------
    {
        const JSON args = buildMuseRequest(parseOrDie(
            {"rename_track", "--name", "12abc", "--pattern", "1.5.2"}))["args"];
        check(args["name"].isString() && args["name"].asString() == "12abc",
              "a token that is only partly numeric stays a string");
        check(args["pattern"].isString() && args["pattern"].asString() == "1.5.2",
              "a malformed number stays a string instead of becoming 1.5");
    }

    // --- booleans, both spellings ------------------------------------------
    {
        const JSON args = buildMuseRequest(parseOrDie(
            {"mute_track", "--track", "0", "--state", "true", "--other", "false"}))["args"];
        check(args["state"].isBool() && args["state"].asBool(), "'true' becomes a boolean");
        check(args["other"].isBool() && !args["other"].asBool(), "'false' becomes a boolean");
    }
    {
        // A flag with nothing after it, and a flag followed by another flag,
        // both mean presence-as-true.
        const JSON args =
            buildMuseRequest(parseOrDie({"mute_track", "--state", "--track", "0"}))["args"];
        check(args["state"].isBool() && args["state"].asBool(),
              "a flag followed by another flag is true");
        check(args["track"].isNumber(), "the following flag still takes its own value");
    }
    {
        const JSON args = buildMuseRequest(parseOrDie({"mute_track", "--state"}))["args"];
        check(args["state"].isBool() && args["state"].asBool(), "a trailing flag is true");
    }

    // --- output options are accepted on both sides of the verb -------------
    {
        const CliInvocation before = parseOrDie({"--result", "--compact", "list_units"});
        check(before.resultOnly && before.compact, "output options parse before the verb");

        const CliInvocation after = parseOrDie({"list_units", "--result", "--compact"});
        check(after.resultOnly && after.compact, "output options parse after the verb too");
        check(!buildMuseRequest(after).has("args"),
              "output options after the verb are not sent as verb args");
    }

    // --- connection options are pre-verb only ------------------------------
    {
        const CliInvocation inv = parseOrDie({"--port", "41952", "--host", "10.0.0.2", "list_units"});
        check(inv.portText == "41952", "--port is captured");
        check(inv.host == "10.0.0.2", "--host is captured");

        // After the verb, --port belongs to the verb; nothing else would let a
        // verb legitimately own a flag named port.
        const CliInvocation owned = parseOrDie({"some_verb", "--port", "4"});
        check(owned.portText.empty(), "--port after the verb is not the CLI's port");
        check(buildMuseRequest(owned)["args"]["port"].asNumber() == 4.0,
              "--port after the verb is a verb arg");
    }

    // --- --args-json for shapes flat flags cannot express ------------------
    {
        const CliInvocation inv = parseOrDie(
            {"--args-json", R"({"commands":[{"verb":"add_track","args":{"name":"Drums"}}]})",
             "batch"});
        const JSON request = buildMuseRequest(inv);
        check(request["args"]["commands"].isArray(), "--args-json preserves nested arrays");
        check(request["args"]["commands"][static_cast<size_t>(0)]["args"]["name"].asString() ==
                  "Drums",
              "--args-json preserves nested objects");
    }

    // --- refusals ----------------------------------------------------------
    {
        std::string error;
        check(parseFails({}, error), "no arguments is an error");
        check(parseFails({"--nonsense", "list_units"}, error), "unknown pre-verb option is refused");
        check(parseFails({"--port"}, error), "an option missing its value is refused");
        check(parseFails({"list_units", "stray"}, error),
              "a bare value after the verb is refused rather than guessed at");
        check(parseFails({"--args-json", "[1,2]", "batch"}, error),
              "--args-json must be an object, not an array");
        check(parseFails({"--args-json", "{\"a\":1} trailing", "batch"}, error),
              "--args-json rejects trailing garbage");
        check(parseFails({"--args-json", "{\"a\":1}", "batch", "--b", "2"}, error),
              "--args-json and per-flag args cannot be combined");
    }

    // --- help short-circuits, and needs no verb ----------------------------
    {
        CliInvocation invocation;
        std::string error;
        check(parseMuseCli({"--help"}, invocation, error) && invocation.helpRequested,
              "--help is accepted with no verb");
        CliInvocation shortForm;
        check(parseMuseCli({"-h"}, shortForm, error) && shortForm.helpRequested,
              "-h is accepted with no verb");
    }

    if (g_failures != 0) {
        std::cerr << g_failures << " assertion(s) failed\n";
        return 1;
    }
    std::cout << "[PASS] muse CLI request framing\n";
    return 0;
}
