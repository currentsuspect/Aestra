// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// What the app does with the crash-recovery card's answers (SPEC-017, V81-V1).
#include "AestraApp.h"

#include "AestraRootComponent.h"
#include "AppLifecycle.h"
#include "CrashFlagPath.h"
#include "ProjectLifecycle.h"
#include "RecoveryDialog.h"
#include "TakeManager.h"
#include "../Core/AestraWindowManager.h"
#include "../Core/RecoveryInfo.h"
#include "../../AestraCore/include/AestraLog.h"

#include <chrono>
#include <filesystem>

using namespace Aestra;

RecoveryInfo::Info AestraApp::recoveryInfoFor(const std::string& originalProjectPath, const std::vector<std::string>& candidates) {
    std::vector<uint64_t> times;
    for (const auto& path : candidates) {
        std::error_code ec;
        const auto ftime = std::filesystem::last_write_time(path, ec);
        if (ec) continue;
        const auto sys = std::chrono::time_point_cast<std::chrono::milliseconds>(ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
        times.push_back(static_cast<uint64_t>(sys.time_since_epoch().count()));
    }
    const auto manifest = originalProjectPath.empty() ? TakeManager::Manifest{} : TakeManager::loadManifest(originalProjectPath);
    return RecoveryInfo::build(manifest, std::move(times));
}

/// Keep the autosave as a rescued session beside the project instead of deleting it.
static void archiveAutosave(const std::string& autosavePath, const std::string& projectPath) {
    if (projectPath.empty() || !std::filesystem::exists(autosavePath)) return;
    const std::filesystem::path dir = ProjectSerializer::getHistoryDirectory(projectPath);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", &tm);
    std::filesystem::copy_file(autosavePath, dir / (std::filesystem::path(projectPath).stem().string() + "_rescued_" + stamp + ".aes"),
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) Log::warning("[Recovery] Could not keep the autosave as a rescued session: " + ec.message());
}

void AestraApp::onRecoveryResponse(Aestra::RecoveryResponse response, const std::string& autosavePath, const std::string& projectPath,
                                   bool hasRequestedProject, const std::string& originalProjectPath,
                                   const std::vector<std::string>& recoveryCandidates) {
    m_recoveryHandled = true;
    m_pendingAutosavePath.clear();
    if (response == Aestra::RecoveryResponse::Recover || response == Aestra::RecoveryResponse::RecoverAsAlt) {
        beginProjectLoadAttempt();
        auto selected = ProjectSerializer::loadFirstValid(recoveryCandidates, m_content ? m_content->getTrackManager() : nullptr, originalProjectPath);
        auto result = std::move(selected.result);
        recordProjectLoadAttempt(result, ProjectLoadSource::Recovery);
        if (result.ok) {
            result = applyLoadedProject(selected.loadedPath, ProjectLoadSource::Recovery, originalProjectPath, std::move(result));
            if (m_content && m_content->getTrackManager()) m_content->getTrackManager()->markModified();
            m_autoSaveManager.markDirty();
            updateWindowTitle();
            Log::info("[Recovery] Autosave recovered successfully from: " + selected.loadedPath);
            // As an alt: the recovered state becomes its own branch, and the saved branch is left as it was.
            if (response == Aestra::RecoveryResponse::RecoverAsAlt && !m_documentState.canonicalPath().empty()) {
                if (!createTakeFromCurrentProject(true)) Log::warning("[Recovery] Recovered, but could not save it as an alt");
                refreshVersionChip();
            }
        } else {
            Log::error("[Recovery] Failed to load autosave");
            if (m_content) m_content->resetToDefaultProject();
            clearProjectLoadReport();
            m_documentState.startUntitled(autosavePathOrEmpty());
            reinitAutosaveManager();
            syncRecordingProjectPath(m_content, m_documentState.canonicalPath());
        }
    } else if (response == Aestra::RecoveryResponse::Discard || response == Aestra::RecoveryResponse::OpenLastSaved) {
        if (response == Aestra::RecoveryResponse::OpenLastSaved) archiveAutosave(autosavePath, originalProjectPath);
        std::error_code ec;
        std::filesystem::remove(autosavePath, ec);
        std::filesystem::remove(getRecoveryMarkerPath(autosavePath), ec);
        if (ec) Log::warning("[Recovery] Failed to remove autosave: " + ec.message());
        else Log::info("[Recovery] Autosave discarded");
        if (hasRequestedProject) {
            if (!loadProjectFromPath(projectPath).ok) Log::error("Failed to load requested project after discarding recovery: " + projectPath);
        } else {
            if (m_content) m_content->resetToDefaultProject();
            clearProjectLoadReport();
            m_documentState.startUntitled(autosavePathOrEmpty());
            reinitAutosaveManager();
            syncRecordingProjectPath(m_content, m_documentState.canonicalPath());
            updateWindowTitle();
        }
    }
}
