// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "ProjectLifecycle.h"

#include "AestraApp.h"

#include "Commands/HostVerbRegistry.h"
#include "Commands/MuseGrammar.h"
#include "Commands/MuseService.h"
#include "Core/AestraContent.h"
#include "Core/ProjectSerializer.h"
#include "MuseProjectLoadReport.h"

#include "AestraLog.h"

#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

// AestraApp and AestraContent are both in the global namespace — see
// MuseHostVerbs.h, which explains why forward-declaring AestraApp inside
// namespace Aestra silently creates a second, unrelated type. Log is in
// Aestra, so it needs naming from here.

// Fully qualified: this file is at global scope, because AestraApp is. The
// MuseHostVerbs family gets away with short names only by sitting inside
// namespace Aestra.
using Aestra::Log;
using Aestra::JSON;
using Aestra::MuseProjectLoadOrigin;
using Aestra::makeMuseProjectLoadReport;
using Aestra::Audio::FlagType;
using Aestra::Audio::HostThreadAffinity;
using Aestra::Audio::HostVerbArg;
using Aestra::Audio::HostVerbDomain;
using Aestra::Audio::HostVerbHandler;
using Aestra::Audio::HostVerbRegistry;
using Aestra::Audio::HostVerbResult;
using Aestra::Audio::HostVerbSpec;

void syncRecordingProjectPath(const std::shared_ptr<AestraContent>& content,
                              const std::string& projectPath) {
    if (!content) {
        return;
    }
    if (auto trackManager = content->getTrackManager()) {
        trackManager->setRecordingProjectPath(projectPath);
    }
}

// --- The operations ----------------------------------------------------------
//
// Extracted from the File menu's inline lambdas verbatim. The menu items now
// call these, so the agent and the human run the same sequence.

void AestraApp::createNewProject() {
    if (m_content && m_content->getTrackManager()) m_content->getTrackManager()->stop();
    // Leave the old session: discards its unsaved/unreferenced takes.
    cleanupUnreferencedRecordings();
    if (m_content) m_content->resetToDefaultProject();
    clearProjectLoadReport();
    m_documentState.startUntitled(autosavePathOrEmpty());
    reinitAutosaveManager();
    syncRecordingProjectPath(m_content, m_documentState.canonicalPath());
    m_lastWindowTitle.clear();
    updateWindowTitle();
    Log::info("New project created");
}

ProjectSerializer::LoadResult AestraApp::openProjectFromPath(const std::string& path) {
    // Old session's keeper path: captured BEFORE the load, so discarded-take
    // cleanup (which runs only on a successful switch) keeps exactly the previous
    // project's recordings. The old session's redo history may still require its
    // WAVs if the new project fails to load, so cleanup is not unconditional.
    const std::string oldKeeperPath = m_documentState.canonicalPath();
    auto result = loadProjectFromPath(path);
    if (result.ok) {
        cleanupUnreferencedRecordings(oldKeeperPath);
    } else {
        Log::error("Failed to load project: " + path + " (" + result.errorMessage + ")");
    }
    return result;
}

// --- The verbs ---------------------------------------------------------------

void AestraApp::registerMuseProjectVerbs(Aestra::Audio::MuseService& service) {
    const auto registerOrLog = [&service](HostVerbSpec spec, HostVerbHandler handler) {
        std::string error;
        if (service.hostVerbs().registerVerb(std::move(spec), std::move(handler), error) !=
            HostVerbRegistry::RegisterStatus::Ok) {
            Log::warning("[MuseProjectVerbs] refused: " + error);
        }
    };

    // --- project.save -------------------------------------------------------
    //
    // No path means "save where this project already lives". An untitled project
    // has nowhere to live, and that is reported rather than guessed: inventing a
    // path in the user's home directory is how an agent ends up scattering
    // projects where nobody will find them.
    {
        HostVerbSpec spec;
        spec.name = "project.save";
        spec.domain = HostVerbDomain::Project;
        spec.affinity = HostThreadAffinity::HostUiThread;
        spec.mutates = true;
        spec.description =
            "Save the project to disk. With 'path', write there and make it the "
            "project's canonical location. Without one, save to the current "
            "canonical path, which fails for a project that has never been saved. "
            "Returns the path written and whether it is now the canonical path.";
        HostVerbArg path;
        path.name = "path";
        path.type = FlagType::String;
        path.required = false;
        path.description = "Absolute or relative project file path (.aes). Omit to re-save in place.";
        spec.args.push_back(path);

        registerOrLog(std::move(spec), [this](const JSON& args) -> HostVerbResult {
            const bool wantsPath = args.has("path") && args["path"].isString() &&
                                   !args["path"].asString().empty();
            const std::string target = wantsPath ? args["path"].asString()
                                                 : m_documentState.canonicalPath();

            if (target.empty()) {
                return HostVerbResult::failure(
                    "no_canonical_path",
                    "this project has never been saved, so there is nowhere to save it; "
                    "pass an explicit 'path'");
            }

            // establishCanonical only when the caller named a path: re-saving in
            // place must not re-point the document at itself, which is the
            // caller's decision to make, not a side effect of a verb.
            const bool established = saveProjectToPath(target, wantsPath);
            if (!established) {
                return HostVerbResult::failure("save_failed",
                                               "the project could not be written to " + target);
            }

            JSON result = JSON::object();
            result.set("path", JSON(m_documentState.canonicalPath()));
            result.set("untitled", JSON(m_documentState.canonicalPath().empty()));
            return HostVerbResult::success(result);
        });
    }

    // --- project.open -------------------------------------------------------
    //
    // A failed load still produces a full diagnostic report — missing samples,
    // missing plugins, plugins that rejected their state — and that report is
    // published rather than returned here, because HostVerbResult::failure
    // carries no payload. An agent that gets `load_failed` should then call
    // get_project_load_report, which is the verb that exists to answer "what
    // actually went wrong".
    {
        HostVerbSpec spec;
        spec.name = "project.open";
        spec.domain = HostVerbDomain::Project;
        spec.affinity = HostThreadAffinity::HostUiThread;
        spec.mutates = true;
        spec.description =
            "Open a project file, replacing the current session. Returns the load "
            "report on success. On failure the reason is in the error and the full "
            "diagnostics (missing samples, missing or state-rejecting plugins) are "
            "available from get_project_load_report.";
        HostVerbArg path;
        path.name = "path";
        path.type = FlagType::String;
        path.required = true;
        path.description = "Path to an Aestra project file (.aes).";
        spec.args.push_back(path);

        registerOrLog(std::move(spec), [this](const JSON& args) -> HostVerbResult {
            const std::string path = args["path"].asString();

            std::error_code ec;
            if (!std::filesystem::exists(path, ec)) {
                return HostVerbResult::failure("no_such_file",
                                               ec ? "could not read " + path + " (" + ec.message() + ")"
                                                   : "no such project file: " + path);
            }

            const auto result = openProjectFromPath(path);
            if (!result.ok) {
                return HostVerbResult::failure("load_failed", result.errorMessage);
            }

            JSON payload = makeMuseProjectLoadReport(result, MuseProjectLoadOrigin::Canonical);
            payload.set("path", JSON(m_documentState.canonicalPath()));
            return HostVerbResult::success(payload);
        });
    }

    // --- project.new --------------------------------------------------------
    {
        HostVerbSpec spec;
        spec.name = "project.new";
        spec.domain = HostVerbDomain::Project;
        spec.affinity = HostThreadAffinity::HostUiThread;
        spec.mutates = true;
        spec.description =
            "Discard the current session and start an empty project. Anything not "
            "saved is lost, exactly as it is when a person picks File > New Project.";
        registerOrLog(std::move(spec), [this](const JSON&) -> HostVerbResult {
            const std::string previous = m_documentState.canonicalPath();
            createNewProject();

            JSON result = JSON::object();
            // An untitled project has an empty canonical path, and saying so
            // plainly is more useful than reporting "" as if it were a location.
            result.set("previousPath", JSON(previous));
            result.set("untitled", JSON(true));
            return HostVerbResult::success(result);
        });
    }
}
