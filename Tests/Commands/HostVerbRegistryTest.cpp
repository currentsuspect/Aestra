// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The seam that lets Muse operate Aestra rather than only its audio engine.
// Covers the registry's contract (naming, typed args, thread affinity,
// enumeration) and its dispatch through MuseService — including that a
// headless session refuses UI-affine verbs instead of running host code on a
// thread that does not own the state it touches.

#include "Commands/HostVerbRegistry.h"
#include "Commands/MuseService.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"

#include "AestraJSON.h"

#include <iostream>
#include <memory>
#include <string>

using Aestra::JSON;
using Aestra::Audio::AudioEngine;
using Aestra::Audio::FlagType;
using Aestra::Audio::HostThreadAffinity;
using Aestra::Audio::HostVerbArg;
using Aestra::Audio::HostVerbDomain;
using Aestra::Audio::HostVerbRegistry;
using Aestra::Audio::HostVerbResult;
using Aestra::Audio::HostVerbSpec;
using Aestra::Audio::MuseService;
using Aestra::Audio::TrackManager;

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

HostVerbSpec spec(const std::string& name, HostVerbDomain domain,
                  HostThreadAffinity affinity = HostThreadAffinity::Any) {
    HostVerbSpec s;
    s.name = name;
    s.domain = domain;
    s.affinity = affinity;
    s.description = "test capability";
    return s;
}

JSON args1(const std::string& key, const JSON& value) {
    JSON a = JSON::object();
    a.set(key, value);
    return a;
}

JSON call(MuseService& service, const std::string& request) {
    return JSON::parse(service.handleRequest(request));
}

std::string status(JSON& response) {
    return response.has("status") ? response["status"].asString() : "<missing>";
}

} // namespace

int main() {
    // --- naming: a verb must declare a domain, and match it -----------------
    {
        HostVerbRegistry registry;
        std::string error;
        const auto ok = [&](const std::string& name, HostVerbDomain d) {
            return registry.registerVerb(spec(name, d), [](const JSON&) {
                return HostVerbResult::success();
            }, error);
        };

        check(ok("view.openMixer", HostVerbDomain::View) == HostVerbRegistry::RegisterStatus::Ok,
              "a well-formed capability registers");
        check(registry.has("view.openMixer"), "and is then known by name");

        check(ok("openMixer", HostVerbDomain::View) == HostVerbRegistry::RegisterStatus::NameMalformed,
              "an unnamespaced verb is refused");
        check(error.find("<domain>.<capability>") != std::string::npos,
              "the refusal shows the expected shape");

        check(ok("view.Open Mixer", HostVerbDomain::View) ==
                  HostVerbRegistry::RegisterStatus::NameMalformed,
              "a capability name that is not lowerCamelCase is refused");

        check(ok("settings.openMixer", HostVerbDomain::View) ==
                  HostVerbRegistry::RegisterStatus::DomainMismatch,
              "a prefix that contradicts the declared domain is refused");
        check(error.find("settings") != std::string::npos && error.find("view") != std::string::npos,
              "the mismatch names both sides");

        // The native engine surface is conceptually audio.*; nothing may
        // register a host verb claiming to be engine capability.
        check(ok("audio.setBpm", HostVerbDomain::Settings) ==
                  HostVerbRegistry::RegisterStatus::ReservedDomain,
              "the audio. domain is reserved");

        check(ok("view.openMixer", HostVerbDomain::View) ==
                  HostVerbRegistry::RegisterStatus::Duplicate,
              "re-registering a name is refused, not silently overwritten");

        HostVerbSpec noHandler = spec("view.openBrowser", HostVerbDomain::View);
        check(registry.registerVerb(noHandler, nullptr, error) ==
                  HostVerbRegistry::RegisterStatus::NoHandler,
              "a spec with no handler is refused");
    }

    // --- typed arguments -----------------------------------------------------
    {
        HostVerbRegistry registry;
        std::string error;

        HostVerbSpec s = spec("settings.setBufferSize", HostVerbDomain::Settings);
        s.args.push_back(HostVerbArg{"frames", FlagType::Int, true, 32.0, 8192.0, "block size"});
        s.args.push_back(HostVerbArg{"exclusive", FlagType::Bool, false,
                                     std::numeric_limits<double>::quiet_NaN(),
                                     std::numeric_limits<double>::quiet_NaN(), ""});
        double seen = -1.0;
        registry.registerVerb(s, [&](const JSON& a) {
            seen = a["frames"].asNumber();
            return HostVerbResult::success(args1("applied", JSON(true)));
        }, error);

        HostVerbResult r = registry.invoke("settings.setBufferSize", JSON::object(), true);
        check(!r.ok && r.errorCode == "missing_arg", "a missing required arg is refused by code");

        r = registry.invoke("settings.setBufferSize", args1("frames", JSON("512")), true);
        check(!r.ok && r.errorCode == "invalid_args", "a string where an int is declared is refused");

        r = registry.invoke("settings.setBufferSize", args1("frames", JSON(512.5)), true);
        check(!r.ok && r.errorCode == "invalid_args", "a fractional int is refused, not truncated");

        r = registry.invoke("settings.setBufferSize", args1("frames", JSON(16.0)), true);
        check(!r.ok && r.errorCode == "invalid_args", "a value below the declared minimum is refused");

        r = registry.invoke("settings.setBufferSize", args1("frames", JSON(99999.0)), true);
        check(!r.ok && r.errorCode == "invalid_args", "a value above the declared maximum is refused");

        r = registry.invoke("settings.setBufferSize", args1("framez", JSON(512.0)), true);
        check(!r.ok && r.errorCode == "invalid_args",
              "a misspelled arg is refused rather than dropped and defaulted");

        r = registry.invoke("settings.setBufferSize", args1("frames", JSON(512.0)), true);
        check(r.ok && seen == 512.0, "a valid call reaches the handler with its value");
        check(r.result["applied"].isBool(), "the handler's payload comes back in result");

        r = registry.invoke("settings.nothingHere", JSON::object(), true);
        check(!r.ok && r.errorCode == "unknown_verb", "an unregistered name is refused by code");
    }

    // --- thread affinity is a capability question, not a marshalling hint ----
    {
        HostVerbRegistry registry;
        std::string error;
        bool ran = false;
        registry.registerVerb(spec("view.openMixer", HostVerbDomain::View,
                                   HostThreadAffinity::HostUiThread),
                              [&](const JSON&) {
                                  ran = true;
                                  return HostVerbResult::success();
                              }, error);
        registry.registerVerb(spec("project.describe", HostVerbDomain::Project,
                                   HostThreadAffinity::Any),
                              [](const JSON&) { return HostVerbResult::success(); }, error);

        HostVerbResult r = registry.invoke("view.openMixer", JSON::object(), false);
        check(!r.ok && r.errorCode == "host_unavailable",
              "a UI-affine verb is refused where there is no UI thread");
        check(!ran, "and its handler is never entered");
        check(r.message.find("headless") != std::string::npos,
              "the refusal explains why rather than just failing");

        r = registry.invoke("project.describe", JSON::object(), false);
        check(r.ok, "an affinity-free verb still runs headless");

        r = registry.invoke("view.openMixer", JSON::object(), true);
        check(r.ok && ran, "the same verb runs where the host UI thread exists");
    }

    // --- enumeration: "what can this host do?" -------------------------------
    {
        HostVerbRegistry registry;
        std::string error;
        const auto add = [&](const std::string& name, HostVerbDomain d) {
            registry.registerVerb(spec(name, d), [](const JSON&) {
                return HostVerbResult::success();
            }, error);
        };
        add("view.openMixer", HostVerbDomain::View);
        add("browser.search", HostVerbDomain::Browser);
        add("settings.setAudioDevice", HostVerbDomain::Settings);

        const auto caps = registry.capabilities();
        check(caps.size() == 3, "every registered capability is enumerated");
        check(caps[0].name == "browser.search" && caps[1].name == "settings.setAudioDevice" &&
                  caps[2].name == "view.openMixer",
              "enumeration is ordered, so callers can diff two hosts");
    }

    // --- dispatch through MuseService ---------------------------------------
    {
        auto trackManager = std::make_shared<TrackManager>();
        AudioEngine engine;
        engine.setSampleRate(48000);
        engine.setBufferConfig(4096, 2);
        MuseService service(trackManager.get(), &engine);

        std::string error;
        bool opened = false;
        HostVerbSpec s = spec("view.openMixer", HostVerbDomain::View, HostThreadAffinity::HostUiThread);
        s.mutates = false;
        service.hostVerbs().registerVerb(s, [&](const JSON&) {
            opened = true;
            return HostVerbResult::success(args1("view", JSON("mixer")));
        }, error);

        HostVerbSpec named = spec("browser.search", HostVerbDomain::Browser, HostThreadAffinity::Any);
        named.args.push_back(HostVerbArg{"query", FlagType::String, true,
                                         std::numeric_limits<double>::quiet_NaN(),
                                         std::numeric_limits<double>::quiet_NaN(), "search text"});
        service.hostVerbs().registerVerb(named, [](const JSON& a) {
            return HostVerbResult::success(args1("echo", JSON(a["query"].asString())));
        }, error);

        // Headless by default: the UI-affine verb must refuse, and say why.
        JSON r = call(service, "{\"id\": 1, \"verb\": \"view.openMixer\"}");
        check(status(r) == "execution_error", "headless service refuses a UI-affine host verb");
        check(r["errorCode"].asString() == "host_unavailable",
              "the machine-readable code survives to the wire");
        check(!opened, "the host handler did not run");

        service.setHostUiThreadAvailable(true);
        r = call(service, "{\"id\": 2, \"verb\": \"view.openMixer\"}");
        check(status(r) == "ok" && opened, "the same verb runs once the host declares its UI thread");
        check(r["result"]["view"].asString() == "mixer", "the handler payload reaches the caller");

        r = call(service, "{\"id\": 3, \"verb\": \"browser.search\", \"args\": {\"query\": \"kick\"}}");
        check(status(r) == "ok" && r["result"]["echo"].asString() == "kick",
              "a host verb with typed args round-trips");

        r = call(service, "{\"id\": 4, \"verb\": \"browser.search\"}");
        check(status(r) == "validation_error", "a missing required host arg is a validation error");

        r = call(service, "{\"id\": 5, \"verb\": \"browser.nope\"}");
        check(status(r) == "parse_error",
              "an unregistered namespaced verb is unknown, not a host failure");

        // Enumeration over the wire.
        r = call(service, "{\"id\": 6, \"verb\": \"get_capabilities\"}");
        check(status(r) == "ok", "get_capabilities ok");
        check(r["result"]["hostVerbs"].size() == 2, "both registered capabilities are listed");
        check(r["result"]["hostUiAvailable"].isBool() && r["result"]["hostUiAvailable"].asBool(),
              "the host UI availability is reported so agents need not guess");
        check(r["result"]["hostVerbs"][0]["verb"].asString() == "browser.search",
              "enumeration over the wire keeps its order");
        check(r["result"]["hostVerbs"][0]["args"][0]["name"].asString() == "query" &&
                  r["result"]["hostVerbs"][0]["args"][0]["required"].asBool(),
              "argument schemas are published, so an agent need not hardcode them");
        check(r["result"]["hostVerbs"][1]["requiresHostUi"].asBool(),
              "thread affinity is published too");

        // Built-ins keep working, and are not shadowed by the registry.
        r = call(service, "{\"id\": 7, \"verb\": \"get_transport\"}");
        check(status(r) == "ok", "native verbs still dispatch with host verbs registered");
    }

    // --- a bare service enumerates honestly ----------------------------------
    {
        auto trackManager = std::make_shared<TrackManager>();
        AudioEngine engine;
        MuseService service(trackManager.get(), &engine);
        JSON r = call(service, "{\"id\": 1, \"verb\": \"get_capabilities\"}");
        check(status(r) == "ok" && r["result"]["hostVerbs"].size() == 0,
              "a host that registered nothing reports an empty capability list");
        check(!r["result"]["hostUiAvailable"].asBool(),
              "and does not claim a UI thread it does not have");
    }

    std::cout << (g_failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string(g_failures))
              << std::endl;
    return g_failures == 0 ? 0 : 1;
}
