// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "MuseHostVerbs.h"

#include "Commands/HostVerbRegistry.h"
#include "Commands/MuseService.h"
#include "Core/AestraContent.h"
#include "ViewTypes.h"

#include "AestraLog.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace Aestra {
namespace {

using Audio::HostThreadAffinity;
using Audio::HostVerbArg;
using Audio::HostVerbDomain;
using Audio::HostVerbResult;
using Audio::HostVerbSpec;
using Audio::ViewType;

// The workspaces Muse can name. These strings are protocol: they appear in
// get_capabilities, agents will hardcode them, and renaming one is a breaking
// change even though the enum behind it is free to move.
struct NamedView {
    const char* name;
    ViewType view;
};

constexpr std::array<NamedView, 6> kViews{{
    {"mixer", ViewType::Mixer},
    {"arsenal", ViewType::Sequencer},   // "Sequencer" internally; Arsenal in the product
    {"pianoRoll", ViewType::PianoRoll},
    {"timeline", ViewType::Playlist},   // "Playlist" internally; Timeline in the product
    {"history", ViewType::History},
    {"takes", ViewType::Takes},
}};

std::string knownViewList() {
    std::string out;
    for (const auto& v : kViews) {
        if (!out.empty()) out += ", ";
        out += v.name;
    }
    return out;
}

// Case-insensitive, because an agent that read "pianoRoll" from get_capabilities
// and typed "pianoroll" has made no meaningful error.
bool lookupView(const std::string& name, ViewType& out) {
    const auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    const std::string wanted = lower(name);
    for (const auto& v : kViews) {
        if (lower(v.name) == wanted) {
            out = v.view;
            return true;
        }
    }
    return false;
}

HostVerbArg viewArg() {
    HostVerbArg arg;
    arg.name = "view";
    arg.type = Audio::FlagType::String;
    arg.required = true;
    arg.description = "one of: " + knownViewList();
    return arg;
}

void registerOrLog(Audio::MuseService& service, HostVerbSpec spec, Audio::HostVerbHandler handler) {
    std::string error;
    const auto status = service.hostVerbs().registerVerb(std::move(spec), std::move(handler), error);
    if (status != Audio::HostVerbRegistry::RegisterStatus::Ok) {
        // Registration is refused rather than overwritten, so a duplicate or a
        // malformed name is a programming error here, not a runtime condition.
        Log::warning("[MuseHostVerbs] refused: " + error);
    }
}

} // namespace

void registerMuseHostVerbs(Audio::MuseService& service, ::AestraContent& content) {
    ::AestraContent* c = &content;

    // --- view.open / view.close ---------------------------------------------
    const auto setOpen = [c](const JSON& args, bool open) -> HostVerbResult {
        ViewType view{};
        const std::string requested = args["view"].asString();
        if (!lookupView(requested, view)) {
            return HostVerbResult::failure(
                "no_such_view", "no view named '" + requested + "'; known views: " + knownViewList());
        }
        const bool already = c->isViewOpen(view);
        c->setViewOpen(view, open);

        JSON result = JSON::object();
        result.set("view", JSON(requested));
        result.set("open", JSON(open));
        // Say whether anything actually moved. "Already open" is a legitimate,
        // successful outcome, and a caller that cannot tell it apart from a
        // change will re-issue commands trying to force one.
        result.set("changed", JSON(already != open));
        return HostVerbResult::success(result);
    };

    {
        HostVerbSpec spec;
        spec.name = "view.open";
        spec.domain = HostVerbDomain::View;
        spec.affinity = HostThreadAffinity::HostUiThread;
        spec.mutates = false; // window layout, not project state — nothing to undo
        spec.description = "Open a workspace overlay. Known views: " + knownViewList() + ".";
        spec.args.push_back(viewArg());
        registerOrLog(service, std::move(spec),
                      [setOpen](const JSON& args) { return setOpen(args, true); });
    }
    {
        HostVerbSpec spec;
        spec.name = "view.close";
        spec.domain = HostVerbDomain::View;
        spec.affinity = HostThreadAffinity::HostUiThread;
        spec.mutates = false;
        spec.description = "Close a workspace overlay. Known views: " + knownViewList() + ".";
        spec.args.push_back(viewArg());
        registerOrLog(service, std::move(spec),
                      [setOpen](const JSON& args) { return setOpen(args, false); });
    }

    // --- view.current --------------------------------------------------------
    {
        HostVerbSpec spec;
        spec.name = "view.current";
        spec.domain = HostVerbDomain::View;
        spec.affinity = HostThreadAffinity::HostUiThread;
        spec.mutates = false;
        spec.description =
            "Which workspaces are open right now. Returns every known view with its "
            "open state, so an agent can see the workspace instead of assuming it.";
        registerOrLog(service, std::move(spec), [c](const JSON&) {
            JSON views = JSON::array();
            for (const auto& v : kViews) {
                JSON entry = JSON::object();
                entry.set("view", JSON(std::string(v.name)));
                entry.set("open", JSON(c->isViewOpen(v.view)));
                views.push(entry);
            }
            JSON result = JSON::object();
            result.set("views", views);
            return HostVerbResult::success(result);
        });
    }
}

} // namespace Aestra
