// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Commands/HostVerbRegistry.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace Aestra {
namespace Audio {
namespace {

// The native engine surface (set_bpm, list_units, …) is conceptually the
// audio.* domain, but those verbs are unprefixed for compatibility — renaming
// 44 of them would break every existing caller and the published manifest.
// Reserving the prefix keeps the conceptual model honest: nothing may register
// a host verb that claims to be engine capability.
constexpr const char* kReservedDomainPrefix = "audio.";

bool isLowerCamelIdentifier(const std::string& text) {
    if (text.empty() || !std::islower(static_cast<unsigned char>(text.front()))) {
        return false;
    }
    return std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return std::isalnum(c) != 0;
    });
}

const char* flagTypeName(FlagType type) {
    switch (type) {
    case FlagType::String: return "string";
    case FlagType::Int:    return "int";
    case FlagType::Float:  return "float";
    case FlagType::Bool:   return "bool";
    }
    return "string";
}

// Validate a JSON value against one typed argument. JSON carries real types,
// so unlike the text-flag path there is nothing to parse — only to check.
bool valueMatches(const HostVerbArg& arg, const JSON& value, std::string& outError) {
    switch (arg.type) {
    case FlagType::Bool:
        if (!value.isBool()) {
            outError = "arg '" + arg.name + "' must be a bool";
            return false;
        }
        return true;
    case FlagType::String:
        if (!value.isString()) {
            outError = "arg '" + arg.name + "' must be a string";
            return false;
        }
        return true;
    case FlagType::Int:
    case FlagType::Float: {
        if (!value.isNumber()) {
            outError = "arg '" + arg.name + "' must be a number";
            return false;
        }
        const double number = value.asNumber();
        if (!std::isfinite(number)) {
            outError = "arg '" + arg.name + "' must be finite";
            return false;
        }
        if (arg.type == FlagType::Int && number != std::floor(number)) {
            outError = "arg '" + arg.name + "' must be a whole number";
            return false;
        }
        if (!std::isnan(arg.minValue) && number < arg.minValue) {
            outError = "arg '" + arg.name + "' is below its minimum of " +
                       std::to_string(arg.minValue);
            return false;
        }
        if (!std::isnan(arg.maxValue) && number > arg.maxValue) {
            outError = "arg '" + arg.name + "' is above its maximum of " +
                       std::to_string(arg.maxValue);
            return false;
        }
        return true;
    }
    }
    outError = "arg '" + arg.name + "' has an unknown type";
    return false;
}

} // namespace

const char* HostVerbRegistry::domainName(HostVerbDomain domain) {
    switch (domain) {
    case HostVerbDomain::Project:  return "project";
    case HostVerbDomain::Settings: return "settings";
    case HostVerbDomain::View:     return "view";
    case HostVerbDomain::Browser:  return "browser";
    case HostVerbDomain::Dialog:   return "dialog";
    }
    return "";
}

std::string HostVerbRegistry::domainPrefixOf(const std::string& name) {
    const size_t dot = name.find('.');
    return dot == std::string::npos ? std::string() : name.substr(0, dot);
}

HostVerbRegistry::RegisterStatus HostVerbRegistry::registerVerb(HostVerbSpec spec,
                                                               HostVerbHandler handler,
                                                               std::string& outError) {
    if (!handler) {
        outError = "no handler for " + spec.name;
        return RegisterStatus::NoHandler;
    }

    if (spec.name.rfind(kReservedDomainPrefix, 0) == 0) {
        outError = "the audio. domain is the native engine surface and cannot be registered into";
        return RegisterStatus::ReservedDomain;
    }

    const size_t dot = spec.name.find('.');
    if (dot == std::string::npos) {
        outError = "host verb '" + spec.name +
                   "' must be named <domain>.<capability>, e.g. settings.setAudioDevice";
        return RegisterStatus::NameMalformed;
    }
    const std::string prefix = spec.name.substr(0, dot);
    const std::string capability = spec.name.substr(dot + 1);
    if (!isLowerCamelIdentifier(capability) || capability.find('.') != std::string::npos) {
        outError = "'" + capability + "' is not a lowerCamelCase capability name";
        return RegisterStatus::NameMalformed;
    }
    if (prefix != domainName(spec.domain)) {
        outError = "'" + spec.name + "' is prefixed '" + prefix + "' but declares domain '" +
                   domainName(spec.domain) + "'";
        return RegisterStatus::DomainMismatch;
    }

    for (const auto& arg : spec.args) {
        if (arg.name.empty()) {
            outError = "'" + spec.name + "' declares an argument with no name";
            return RegisterStatus::NameMalformed;
        }
    }

    if (m_verbs.find(spec.name) != m_verbs.end()) {
        outError = "'" + spec.name + "' is already registered";
        return RegisterStatus::Duplicate;
    }

    const std::string name = spec.name;
    m_verbs.emplace(name, Entry{std::move(spec), std::move(handler)});
    return RegisterStatus::Ok;
}

bool HostVerbRegistry::has(const std::string& name) const {
    return m_verbs.find(name) != m_verbs.end();
}

HostVerbResult HostVerbRegistry::invoke(const std::string& name, const JSON& args,
                                        bool hostUiThreadAvailable) const {
    const auto it = m_verbs.find(name);
    if (it == m_verbs.end()) {
        return HostVerbResult::failure("unknown_verb", "no host verb named '" + name + "'");
    }
    const HostVerbSpec& spec = it->second.spec;

    if (spec.affinity == HostThreadAffinity::HostUiThread && !hostUiThreadAvailable) {
        // Not a marshalling problem: this process has no UI thread to marshal
        // onto. Say so, rather than running host code somewhere undefined.
        return HostVerbResult::failure(
            "host_unavailable",
            "'" + name + "' needs the application's UI thread; this session is headless");
    }

    // Args are optional only if every declared argument is optional.
    JSON empty = JSON::object();
    const JSON& provided = args.isObject() ? args : empty;
    if (!args.isNull() && !args.isObject()) {
        return HostVerbResult::failure("invalid_args", "args must be an object");
    }

    // Unknown args are refused by name, matching the rest of this surface: a
    // misspelled argument that is silently dropped runs the verb with a default
    // the caller never asked for and reports success.
    //
    // Checked BEFORE required args on purpose. Typing "framez" for "frames"
    // otherwise reports the required arg as missing, which sends the caller
    // looking for an argument they did in fact pass, spelled wrong.
    JSON mutableArgs = provided;
    for (const auto& entry : mutableArgs.asObject()) {
        const bool declared =
            std::any_of(spec.args.begin(), spec.args.end(),
                        [&](const HostVerbArg& arg) { return arg.name == entry.first; });
        if (!declared) {
            return HostVerbResult::failure("invalid_args",
                                           "unknown arg for " + name + ": " + entry.first);
        }
    }

    for (const auto& arg : spec.args) {
        const bool present = provided.has(arg.name);
        if (!present) {
            if (arg.required) {
                return HostVerbResult::failure("missing_arg",
                                               "'" + name + "' requires arg '" + arg.name + "' (" +
                                                   flagTypeName(arg.type) + ")");
            }
            continue;
        }
        std::string typeError;
        if (!valueMatches(arg, provided[arg.name], typeError)) {
            return HostVerbResult::failure("invalid_args", typeError);
        }
    }

    return it->second.handler(provided);
}

std::vector<HostVerbSpec> HostVerbRegistry::capabilities() const {
    std::vector<HostVerbSpec> out;
    out.reserve(m_verbs.size());
    for (const auto& entry : m_verbs) {
        out.push_back(entry.second.spec);
    }
    return out; // m_verbs is ordered, so enumeration is stable across calls
}

} // namespace Audio
} // namespace Aestra
