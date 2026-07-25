// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/MuseGrammar.h" // FlagType

#include "AestraJSON.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @file HostVerbRegistry.h
 * @brief The seam that lets Muse operate Aestra, not just its audio engine.
 *
 * MuseService lives in AestraAudio and can therefore only reach what
 * AestraAudio owns. Settings, view navigation, the browser and dialogs live in
 * the application layer, so until now they were structurally unreachable — the
 * agent could change a gain but not open the mixer.
 *
 * The fix is NOT for AestraAudio to reach up into Source/; that would invert
 * the dependency the whole codebase is built on. Instead the application
 * registers its capabilities INTO this registry at startup:
 *
 *     Source/ ────registers────> MuseService
 *                                    ^
 *     Muse CLI / agent ───────────────┘
 *
 * MuseService never learns what a SettingsDialog or a TrackManagerUI is. It
 * holds a name, an argument schema, and a callable.
 *
 * ## A verb is an application capability, not a UI gesture
 *
 * Register `view.openMixer`, `settings.setAudioDevice`, `browser.search`.
 * Never `clickMixerButton` or `pressBrowserSearchField`. The first survives a
 * UI redesign and lets Muse operate Aestra semantically; the second makes Muse
 * an accessibility macro system welded to today's widgets, and every future
 * layout change silently breaks it.
 *
 * This is why registration requires a declared domain and a typed argument
 * schema rather than accepting an arbitrary `(name, std::function<JSON(JSON)>)`
 * pair. A generic escape hatch would become an undocumented internal RPC API
 * within a release or two, and nothing in the type system would object.
 */

/** @brief The capability domains the host may register into. */
enum class HostVerbDomain {
    Project,  ///< project/session lifecycle: save, load, export targets
    Settings, ///< application preferences and device configuration
    View,     ///< navigation between workspaces and panels
    Browser,  ///< the file/sample/plugin browser
    Dialog    ///< modal flows the host owns
};

/**
 * @brief Where a verb is allowed to run.
 *
 * Host capabilities almost always touch UI-owned state, which is main-thread
 * only. Headless processes (MuseRepl, tests) have no such thread at all, so
 * this is not merely a marshalling hint: it decides whether a verb can be
 * honoured in this process or must be refused with a reason.
 */
enum class HostThreadAffinity {
    Any,         ///< safe to run wherever the request arrives
    HostUiThread ///< requires the host's UI thread; refused where there is none
};

/** @brief One typed argument. Mirrors FlagSchema so both render alike in the manifest. */
struct HostVerbArg {
    std::string name;
    FlagType type = FlagType::String;
    bool required = false;
    double minValue = std::numeric_limits<double>::quiet_NaN();
    double maxValue = std::numeric_limits<double>::quiet_NaN();
    std::string description;
};

/**
 * @brief What a host verb returns.
 *
 * Deliberately not "arbitrary JSON". A handler must say whether it succeeded,
 * and a failure must carry a machine-readable code — agents that have to
 * pattern-match prose to find out what happened will do it badly, and the code
 * is what lets a caller distinguish "no such device" from "device busy"
 * without parsing English.
 */
struct HostVerbResult {
    bool ok = false;
    std::string errorCode; ///< empty when ok; snake_case, e.g. "no_such_device"
    std::string message;   ///< human-readable detail, never the API contract
    JSON result = JSON::object();

    static HostVerbResult success(JSON payload = JSON::object()) {
        HostVerbResult r;
        r.ok = true;
        r.result = std::move(payload);
        return r;
    }
    static HostVerbResult failure(std::string code, std::string detail) {
        HostVerbResult r;
        r.ok = false;
        r.errorCode = std::move(code);
        r.message = std::move(detail);
        return r;
    }
};

/** @brief Everything the registry knows about a capability, minus its implementation. */
struct HostVerbSpec {
    std::string name; ///< "<domain>.<lowerCamelCase>", e.g. "settings.setAudioDevice"
    HostVerbDomain domain = HostVerbDomain::View;
    std::vector<HostVerbArg> args;
    HostThreadAffinity affinity = HostThreadAffinity::HostUiThread;
    /** True when the verb changes persistent state a user would expect to undo or save. */
    bool mutates = false;
    std::string description;
};

using HostVerbHandler = std::function<HostVerbResult(const JSON& args)>;

/**
 * @brief Names the host may register, and the validation every call passes.
 *
 * Not thread-safe by construction: registration happens once during host
 * startup, before any request can arrive, and invocation is serialised by
 * MuseService onto the thread that owns it.
 */
class HostVerbRegistry {
public:
    enum class RegisterStatus {
        Ok,
        NameMalformed,  ///< not "<domain>.<name>", or the name part is empty/ill-formed
        DomainMismatch, ///< the prefix does not match the declared domain
        ReservedDomain, ///< "audio." belongs to the native engine surface
        Duplicate,      ///< already registered
        NoHandler
    };

    /**
     * @param outError filled with a message naming the offending part.
     * @note Registration is refused rather than overwritten. A silent
     *       last-one-wins would make two components fight over a capability
     *       with no diagnostic and a load-order-dependent winner.
     */
    RegisterStatus registerVerb(HostVerbSpec spec, HostVerbHandler handler, std::string& outError);

    bool has(const std::string& name) const;
    size_t size() const { return m_verbs.size(); }

    /**
     * @brief Validate arguments, check thread affinity, then invoke.
     *
     * @param hostUiThreadAvailable false in headless processes; HostUiThread
     *        verbs are refused with "host_unavailable" rather than run somewhere
     *        they would corrupt UI state.
     * @return a failure result when validation or affinity refuses; the
     *         handler's own result otherwise.
     */
    HostVerbResult invoke(const std::string& name, const JSON& args,
                          bool hostUiThreadAvailable) const;

    /** @brief Every registered capability, ordered by name. The answer to "what can this host do?". */
    std::vector<HostVerbSpec> capabilities() const;

    /** @brief The domain prefix for a name, or empty when it has none. */
    static std::string domainPrefixOf(const std::string& name);
    static const char* domainName(HostVerbDomain domain);

private:
    struct Entry {
        HostVerbSpec spec;
        HostVerbHandler handler;
    };
    std::map<std::string, Entry> m_verbs; ///< ordered so enumeration is deterministic
};

} // namespace Audio
} // namespace Aestra
