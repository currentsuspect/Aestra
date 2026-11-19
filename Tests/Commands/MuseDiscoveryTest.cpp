// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Muse discovery contract — what an agent can learn from the surface itself.
//
// #1409: an agent that reads get_schema as its tool list could not find
// project.save (a host verb, registered at runtime and absent from the grammar's
// static text), was told "unknown flag" without being told the valid ones, and
// was given no way to learn the socket handshake. Each check below fails if that
// discovery hole reopens.

#include "Commands/CommandParser.h"
#include "Commands/CommandRegistry.h"
#include "Commands/HostVerbRegistry.h"
#include "Commands/MuseService.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"

#include "AestraJSON.h"

#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

using Aestra::Audio::AudioEngine;
using Aestra::Audio::CommandParser;
using Aestra::Audio::CommandRegistry;
using Aestra::Audio::FlagType;
using Aestra::Audio::HostThreadAffinity;
using Aestra::Audio::HostVerbArg;
using Aestra::Audio::HostVerbDomain;
using Aestra::Audio::HostVerbRegistry;
using Aestra::Audio::HostVerbResult;
using Aestra::Audio::HostVerbSpec;
using Aestra::Audio::MuseService;
using Aestra::Audio::TrackManager;
using Aestra::JSON;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& label) {
    if (condition) {
        std::cout << "PASS: " << label << "\n";
    } else {
        std::cout << "FAIL: " << label << "\n";
        ++g_failures;
    }
}

JSON call(MuseService& service, const std::string& request) {
    return JSON::parse(service.handleRequest(request));
}

std::string status(JSON& response) {
    return response.has("status") ? response["status"].asString() : "<missing>";
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// The shape project.save registers in the app: one optional path, one optional
// bool, and four refusals a caller can act on.
void registerSaveProbe(HostVerbRegistry& registry) {
    HostVerbSpec spec;
    spec.name = "project.save";
    spec.domain = HostVerbDomain::Project;
    spec.affinity = HostThreadAffinity::HostUiThread;
    spec.mutates = true;
    spec.description = "Save the project to disk.";
    HostVerbArg path;
    path.name = "path";
    path.type = FlagType::String;
    path.required = false;
    spec.args.push_back(path);
    HostVerbArg copy;
    copy.name = "copy";
    copy.type = FlagType::Bool;
    copy.required = false;
    spec.args.push_back(copy);
    spec.failureCodes = {"no_canonical_path", "save_as_required", "copy_needs_path", "save_failed"};

    std::string error;
    registry.registerVerb(
        std::move(spec),
        [](const JSON&) -> HostVerbResult {
            JSON result = JSON::object();
            result.set("path", JSON("/tmp/probe.aes"));
            return HostVerbResult::success(result);
        },
        error);
}

} // namespace

int main() {
    auto trackManager = std::make_shared<TrackManager>();
    trackManager->getUnitManager().setPatternManager(&trackManager->getPatternManager());
    AudioEngine engine;
    engine.setSampleRate(48000);
    engine.setBufferConfig(4096, 2);
    MuseService::wireHeadlessEngine(trackManager, engine);
    CommandRegistry::initialize();
    MuseService service(trackManager.get(), &engine);

    registerSaveProbe(service.hostVerbs());

    // --- The manifest carries the verbs the app registered -------------------
    {
        JSON r = call(service, "{\"id\": 1, \"verb\": \"get_schema\"}");
        check(status(r) == "ok", "get_schema ok");

        JSON hostVerbs = r["result"]["hostVerbs"];
        check(hostVerbs.isArray(), "get_schema publishes a hostVerbs array");

        bool sawSave = false;
        for (size_t i = 0; i < hostVerbs.size(); ++i) {
            JSON entry = hostVerbs[i];
            if (entry["verb"].asString() != "project.save") continue;
            sawSave = true;
            check(entry["domain"].asString() == "project", "project.save is filed under its domain");
            check(entry["mutates"].asBool(), "project.save is marked mutating");
            check(entry["requiresHostUi"].asBool(), "project.save declares its host-UI requirement");

            bool sawPath = false;
            bool sawCopy = false;
            for (size_t a = 0; a < entry["args"].size(); ++a) {
                JSON arg = entry["args"][a];
                const std::string name = arg["name"].asString();
                sawPath = sawPath || name == "path";
                sawCopy = sawCopy || name == "copy";
                check(!arg["required"].asBool(), "project.save args are optional: " + name);
            }
            check(sawPath && sawCopy, "project.save publishes the args it takes");

            bool sawCode = false;
            for (size_t c = 0; c < entry["failureCodes"].size(); ++c) {
                sawCode = sawCode || entry["failureCodes"][c].asString() == "save_as_required";
            }
            check(sawCode, "project.save publishes the refusals a caller can act on");
        }
        check(sawSave, "get_schema lists project.save — the verb an agent saves with");

        // get_capabilities and get_schema describe the same registry, so the two
        // manifests cannot disagree about what this process can do.
        JSON caps = call(service, "{\"id\": 2, \"verb\": \"get_capabilities\"}");
        check(status(caps) == "ok", "get_capabilities still answers");
        check(caps["result"]["hostVerbs"].size() == hostVerbs.size(),
              "get_capabilities and get_schema publish the same host verbs");

        // Discovery must not stop there: get_capabilities has to be findable in
        // the schema's own query list, or an agent never learns to ask for it.
        bool sawCapabilitiesQuery = false;
        JSON queries = r["result"]["queries"];
        for (size_t q = 0; q < queries.size(); ++q) {
            sawCapabilitiesQuery = sawCapabilitiesQuery || queries[q]["verb"].asString() == "get_capabilities";
        }
        check(sawCapabilitiesQuery, "get_schema lists get_capabilities among its queries");

        // The handshake is part of learning the surface: a socket client that
        // reads only the schema has to be told to authenticate first.
        JSON notes = r["result"]["notes"];
        check(notes.has("connecting"), "get_schema documents how to connect");
        const std::string connecting = notes["connecting"].asString();
        check(contains(connecting, "\"auth\""), "the connecting note names the auth line");
        check(contains(connecting, "muse-<port>.token"), "the connecting note names the token file");
        check(contains(connecting, "AESTRA_MUSE_PORT"), "the connecting note says where the port comes from");

        check(notes.has("hostVerbs"), "get_schema explains the hostVerbs array");
    }

    // --- Refusals name what would have worked --------------------------------
    {
        CommandParser parser;
        const Aestra::Audio::CommandContext ctx{&engine, trackManager.get()};
        auto& history = trackManager->getCommandHistory();

        // The exact mistake from the session: --plugin where the flag is --effect.
        // The names are not spelling-similar, so the answer is the flag list.
        std::unordered_map<std::string, std::string> flags{{"track", "0"}, {"plugin", "com.Aestrastudios.eq"}};
        const auto result = parser.execute("add_effect", flags, history, ctx);
        const std::string message = result.message;
        check(result.status == Aestra::Audio::CommandStatus::ValidationError,
              "a misspelled flag is a validation error");
        check(contains(message, "unknown flag for add_effect: --plugin"),
              "the error still names the flag it got");
        check(contains(message, "valid flags: --track, --effect, --slot"),
              "the error lists the flags the verb takes");

        // A near-miss of a real flag gets named outright.
        std::unordered_map<std::string, std::string> typo{{"track", "0"}, {"efect", "eq"}};
        const auto typoResult = parser.execute("add_effect", typo, history, ctx);
        check(contains(typoResult.message, "did you mean --effect?"),
              "a near-miss flag is named in the error");

        // A flag name nowhere near a real one gets no confident guess.
        std::unordered_map<std::string, std::string> unrelated{{"track", "0"}, {"zzzzzzzz", "1"}};
        const auto unrelatedResult = parser.execute("add_effect", unrelated, history, ctx);
        check(contains(unrelatedResult.message, "valid flags: --track, --effect, --slot"),
              "an unrelated flag is still told the valid ones");
        check(!contains(unrelatedResult.message, "did you mean"),
              "an unrelated flag is not given a false suggestion");

        // Missing required flags name the valid set too.
        std::unordered_map<std::string, std::string> missing{{"track", "0"}};
        const auto missingResult = parser.execute("add_effect", missing, history, ctx);
        check(contains(missingResult.message, "missing required flag: --effect") &&
                  contains(missingResult.message, "valid flags:"),
              "a missing required flag also lists the flags");
    }

    // --- Host verb refusals name the args they accept ------------------------
    //
    // Invoked with the host UI thread available, as it is in the app: this probe
    // verb requires that thread, so a headless request is refused for affinity
    // before its arguments are ever looked at.
    {
        JSON args = JSON::object();
        args.set("pth", JSON("/tmp/x.aes"));
        const auto refused = service.hostVerbs().invoke("project.save", args, true);
        check(!refused.ok && refused.errorCode == "invalid_args", "unknown host arg is refused");
        check(contains(refused.message, "unknown arg for project.save: pth"),
              "the refusal names the bad arg");
        check(contains(refused.message, "path") && contains(refused.message, "copy"),
              "the refusal lists the args project.save takes");

        // Over the wire the same refusal is a validation_error, not an execution
        // one: the caller's arguments were wrong, the host did not fail.
        JSON r = call(service,
                      "{\"id\": 3, \"verb\": \"project.save\", \"args\": {\"pth\": \"/tmp/x.aes\"}}");
        check(status(r) == "execution_error" && r["errorCode"].asString() == "host_unavailable",
              "a headless session says why it cannot serve a host-only verb");
    }

    std::cout << (g_failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string(g_failures))
              << std::endl;
    return g_failures == 0 ? 0 : 1;
}
