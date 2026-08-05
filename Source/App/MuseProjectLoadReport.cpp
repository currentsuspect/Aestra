// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "MuseProjectLoadReport.h"

#include <string>

namespace Aestra {
namespace {

const char* issueCode(const LoadIssue& issue) {
    if (issue.category == "integrity") return "integrity_mismatch";
    if (issue.category == "clip") return "missing_pattern_reference";
    if (issue.category == "unit") return "missing_unit_reference";
    return "project_load_issue";
}

JSON globalEvidence() {
    JSON evidence = JSON::object();
    evidence.set("stableIdentityAvailable", JSON(false));
    evidence.set("positionalIdentityAvailable", JSON(false));
    evidence.set("identityKind", JSON("none"));
    return evidence;
}

JSON issueEvidence(const LoadIssue& issue) {
    if (issue.objectId == 0) {
        return globalEvidence();
    }

    JSON evidence = JSON::object();
    evidence.set("stableIdentityAvailable", JSON(true));
    evidence.set("positionalIdentityAvailable", JSON(false));
    evidence.set("identityKind", JSON("stable_id"));
    evidence.set("objectId", JSON(std::to_string(issue.objectId)));
    if (!issue.referenceId.empty()) evidence.set("referenceId", JSON(issue.referenceId));
    if (!issue.context.empty()) evidence.set("context", JSON(issue.context));
    return evidence;
}

JSON assetEvidence(const std::string& path) {
    JSON evidence = JSON::object();
    evidence.set("stableIdentityAvailable", JSON(false));
    evidence.set("positionalIdentityAvailable", JSON(false));
    evidence.set("identityKind", JSON("asset_path"));
    evidence.set("assetPath", JSON(path));
    return evidence;
}

JSON pluginEvidence(const std::string& location) {
    JSON evidence = JSON::object();
    evidence.set("stableIdentityAvailable", JSON(false));
    evidence.set("positionalIdentityAvailable", JSON(false));
    evidence.set("identityKind", JSON("owner_label"));
    evidence.set("ownerLabel", JSON(location));
    return evidence;
}

JSON makeIssueEntry(const char* code, const std::string& message, const JSON& evidence) {
    JSON entry = JSON::object();
    entry.set("issueCode", JSON(code));
    entry.set("message", JSON(message));
    entry.set("evidence", evidence);
    return entry;
}

const char* integrityName(ProjectSerializer::LoadIntegrity integrity) {
    switch (integrity) {
    case ProjectSerializer::LoadIntegrity::Unchecked: return "unchecked";
    case ProjectSerializer::LoadIntegrity::Verified: return "verified";
    case ProjectSerializer::LoadIntegrity::Mismatch: return "mismatch";
    }
    return "unchecked";
}

const char* migrationName(MigrationOutcome outcome) {
    switch (outcome) {
    case MigrationOutcome::None: return "none";
    case MigrationOutcome::Transformed: return "transformed";
    case MigrationOutcome::Failed: return "failed";
    }
    return "none";
}

const char* originName(MuseProjectLoadOrigin origin) {
    switch (origin) {
    case MuseProjectLoadOrigin::Canonical: return "canonical";
    case MuseProjectLoadOrigin::Recovery: return "recovery";
    case MuseProjectLoadOrigin::Snapshot: return "snapshot";
    }
    return "canonical";
}

const char* recoveryName(MuseProjectLoadOrigin origin, bool ok) {
    switch (origin) {
    case MuseProjectLoadOrigin::Canonical: return "not_attempted";
    case MuseProjectLoadOrigin::Recovery: return ok ? "recovered" : "recovery_failed";
    case MuseProjectLoadOrigin::Snapshot: return ok ? "snapshot_restored" : "snapshot_restore_failed";
    }
    return "not_attempted";
}

bool isDegraded(const ProjectSerializer::LoadResult& result, MuseProjectLoadOrigin origin) {
    return result.schemaVersionAdvanced() ||
           result.migrationOutcome != MigrationOutcome::None ||
           origin == MuseProjectLoadOrigin::Recovery ||
           result.integrity == ProjectSerializer::LoadIntegrity::Mismatch ||
           !result.missingAssets.empty() || !result.missingPlugins.empty() ||
           (result.report && !result.report->issues.empty());
}

} // namespace

JSON makeMuseProjectLoadReport(const ProjectSerializer::LoadResult& result,
                               MuseProjectLoadOrigin origin) {
    JSON warnings = JSON::array();
    JSON errors = JSON::array();
    JSON missingPlugins = JSON::array();
    JSON missingAssets = JSON::array();
    JSON unrestoredState = JSON::array();

    if (!result.ok) {
        errors.push(makeIssueEntry("project_load_failed", result.errorMessage, globalEvidence()));
    }

    if (result.report) {
        for (const auto& issue : result.report->issues) {
            JSON entry = makeIssueEntry(issueCode(issue), issue.message, issueEvidence(issue));
            entry.set("category", JSON(issue.category));
            if (issue.severity == LoadIssueSeverity::Error) {
                errors.push(entry);
            } else {
                warnings.push(entry);
            }

            if (issue.category == "clip" || issue.category == "unit") {
                JSON unrestored = JSON::object();
                unrestored.set("issueCode", JSON(issueCode(issue)));
                unrestored.set("stateKind",
                               JSON(issue.category == "clip" ? "pattern_reference" : "unit_reference"));
                unrestored.set("restored", JSON(false));
                unrestored.set("referencePreserved", JSON(true));
                unrestored.set("evidence", issueEvidence(issue));
                unrestoredState.push(unrestored);
            }
        }
    }

    for (const auto& plugin : result.missingPlugins) {
        const JSON evidence = pluginEvidence(plugin.location);

        JSON missing = JSON::object();
        missing.set("pluginId", JSON(plugin.pluginId));
        missing.set("location", JSON(plugin.location));
        missing.set("placeholderPreserved", JSON(true));
        missing.set("issueCode", JSON("missing_plugin_placeholder"));
        missing.set("evidence", evidence);
        missingPlugins.push(missing);

        warnings.push(makeIssueEntry(
            "missing_plugin_placeholder",
            "Plugin could not be instantiated; its stored slot and opaque state were preserved",
            evidence));

        JSON unrestored = JSON::object();
        unrestored.set("issueCode", JSON("missing_plugin_placeholder"));
        unrestored.set("stateKind", JSON("plugin_instance"));
        unrestored.set("restored", JSON(false));
        unrestored.set("placeholderPreserved", JSON(true));
        unrestored.set("evidence", evidence);
        unrestoredState.push(unrestored);
    }

    for (const auto& path : result.missingAssets) {
        const JSON evidence = assetEvidence(path);

        JSON missing = JSON::object();
        missing.set("path", JSON(path));
        missing.set("issueCode", JSON("missing_or_unreadable_asset"));
        missing.set("evidence", evidence);
        missingAssets.push(missing);

        warnings.push(makeIssueEntry("missing_or_unreadable_asset",
                                     "Audio asset was missing or unreadable during project load",
                                     evidence));

        JSON unrestored = JSON::object();
        unrestored.set("issueCode", JSON("missing_or_unreadable_asset"));
        unrestored.set("stateKind", JSON("audio_asset_content"));
        unrestored.set("restored", JSON(false));
        unrestored.set("evidence", evidence);
        unrestoredState.push(unrestored);
    }

    JSON format = JSON::object();
    format.set("encounteredVersion", JSON(static_cast<double>(result.sourceSchemaVersion)));
    format.set("resultingVersion", JSON(static_cast<double>(result.resultingSchemaVersion)));

    JSON migration = JSON::object();
    migration.set("state", JSON(migrationName(result.migrationOutcome)));
    migration.set("schemaVersionAdvanced", JSON(result.schemaVersionAdvanced()));
    migration.set("legacyAudioPatternsSplit", JSON(static_cast<double>(result.legacyAudioPatternsSplit)));

    JSON recovery = JSON::object();
    recovery.set("state", JSON(recoveryName(origin, result.ok)));
    recovery.set("loadOrigin", JSON(originName(origin)));

    JSON report = JSON::object();
    report.set("status", JSON(!result.ok ? "failed" : isDegraded(result, origin) ? "degraded" : "clean"));
    report.set("observed", JSON(true));
    report.set("format", format);
    report.set("migration", migration);
    report.set("recovery", recovery);
    report.set("integrity", JSON(integrityName(result.integrity)));
    report.set("missingPlugins", missingPlugins);
    report.set("missingAssets", missingAssets);
    report.set("unrestoredState", unrestoredState);
    report.set("warnings", warnings);
    report.set("errors", errors);
    return report;
}

} // namespace Aestra
