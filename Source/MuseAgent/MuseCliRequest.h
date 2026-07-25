// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Argv -> Muse request, split out of main() so it can be tested without a
// socket. Everything here is pure: strings in, JSON out, no IO.

#include "AestraJSON.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace Aestra {
namespace MuseAgent {

struct CliInvocation {
    std::string host = "127.0.0.1";
    std::string portText;    // empty = fall back to $AESTRA_MUSE_PORT
    std::string verb;
    JSON args = JSON::object();
    bool haveArgs = false;
    bool argsFromJson = false; // --args-json was used; per-flag args then conflict
    bool raw = false;
    bool compact = false;
    bool resultOnly = false;
    bool helpRequested = false;
};

// A bare token is a value; `--name` starts the next flag. This is what lets
// `--muted` mean true without inventing a separate boolean syntax.
inline bool museCliIsFlag(const std::string& token) {
    return token.size() > 2 && token[0] == '-' && token[1] == '-';
}

// Type by shape, not by schema: the CLI does not carry a copy of the grammar,
// and guessing from a stale one would be worse than letting the service
// validate. Numbers must consume the whole token, so "1.5.2" and "12abc" stay
// strings rather than silently becoming 1.5 and 12.
inline JSON museCliValueFromToken(const std::string& token) {
    if (token == "true") return JSON(true);
    if (token == "false") return JSON(false);

    if (!token.empty()) {
        char* end = nullptr;
        const double parsed = std::strtod(token.c_str(), &end);
        if (end != nullptr && *end == '\0' && end != token.c_str()) {
            return JSON(parsed);
        }
    }
    return JSON(token);
}

/**
 * @brief Parse argv (excluding argv[0]) into an invocation.
 * @return false with outError set when the command line cannot be honoured.
 *
 * Options are options only until the verb is named; after that a --flag belongs
 * to the verb, so `muse set_steps --pattern 4` is unambiguous. The three
 * presence-only output options are the exception — they are recognised after
 * the verb too, because that is where anyone actually types them, and having no
 * value means there is no arity ambiguity to resolve.
 */
inline bool parseMuseCli(const std::vector<std::string>& tokens, CliInvocation& out,
                         std::string& outError) {
    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::string& arg = tokens[i];

        if (arg == "-h" || arg == "--help") {
            out.helpRequested = true;
            return true;
        }

        if (out.verb.empty() && museCliIsFlag(arg)) {
            const bool wantsValue = (arg == "--port" || arg == "--host" || arg == "--args-json");
            std::string value;
            if (wantsValue) {
                if (i + 1 >= tokens.size()) {
                    outError = arg + " needs a value";
                    return false;
                }
                value = tokens[++i];
            }
            if (arg == "--port") { out.portText = value; continue; }
            if (arg == "--host") { out.host = value; continue; }
            if (arg == "--args-json") {
                bool consumedAll = false;
                JSON parsed = JSON::parseStrict(value, consumedAll);
                if (!consumedAll || !parsed.isObject()) {
                    outError = "--args-json must be a single JSON object";
                    return false;
                }
                if (out.haveArgs) {
                    outError = "--args-json and per-flag args are mutually exclusive";
                    return false;
                }
                out.args = parsed;
                out.haveArgs = true;
                out.argsFromJson = true;
                continue;
            }
            if (arg == "--raw") { out.raw = true; continue; }
            if (arg == "--compact") { out.compact = true; continue; }
            if (arg == "--result") { out.resultOnly = true; continue; }
            outError = "unknown option: " + arg;
            return false;
        }

        if (out.verb.empty()) {
            out.verb = arg;
            continue;
        }

        if (!museCliIsFlag(arg)) {
            outError = "unexpected value '" + arg + "' — flags take the form --name value";
            return false;
        }

        const std::string name = arg.substr(2);
        if (name == "result") { out.resultOnly = true; continue; }
        if (name == "raw") { out.raw = true; continue; }
        if (name == "compact") { out.compact = true; continue; }

        // --args-json is whole-object; mixing it with per-flag args would leave
        // the precedence between them unstated, so refuse instead of guessing.
        if (out.argsFromJson) {
            outError = "--args-json and per-flag args are mutually exclusive";
            return false;
        }

        if (i + 1 < tokens.size() && !museCliIsFlag(tokens[i + 1])) {
            out.args.set(name, museCliValueFromToken(tokens[++i]));
        } else {
            out.args.set(name, JSON(true)); // presence-as-true
        }
        out.haveArgs = true;
    }

    if (out.verb.empty() && !out.helpRequested) {
        outError = "no verb given";
        return false;
    }
    return true;
}

/**
 * @brief Frame an invocation as a Muse request envelope.
 *
 * JSON::set silently no-ops on a non-object, so the envelope and args must be
 * built with JSON::object() — a default-constructed JSON is Null, and every
 * field written to it would vanish without a diagnostic.
 */
inline JSON buildMuseRequest(const CliInvocation& invocation) {
    JSON request = JSON::object();
    request.set("id", JSON(1.0));
    request.set("verb", JSON(invocation.verb));
    if (invocation.haveArgs) {
        request.set("args", invocation.args);
    }
    return request;
}

} // namespace MuseAgent
} // namespace Aestra
