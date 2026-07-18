// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <string>
#include <utility>

namespace Aestra {

/**
 * @brief Tracks the distinct file identities involved in one editing session.
 *
 * The canonical path is the only path targeted by an ordinary Save. Autosave,
 * recovery, and snapshot paths are sources of session state, never implicit
 * canonical documents.
 */
class ProjectDocumentState {
public:
    enum class SourceKind { Untitled, Canonical, Recovery, Snapshot };

    void startUntitled(std::string autosavePath) {
        m_canonicalPath.clear();
        m_autosavePath = std::move(autosavePath);
        m_recoveryPath.clear();
        m_snapshotPath.clear();
        m_overwriteProtected = false;
        m_sourceKind = SourceKind::Untitled;
    }

    void openCanonical(std::string canonicalPath) {
        m_canonicalPath = std::move(canonicalPath);
        m_recoveryPath.clear();
        m_snapshotPath.clear();
        m_overwriteProtected = false;
        m_sourceKind = m_canonicalPath.empty() ? SourceKind::Untitled : SourceKind::Canonical;
    }

    void recoverFrom(std::string recoveryPath, std::string canonicalPath = {}) {
        m_canonicalPath = std::move(canonicalPath);
        m_recoveryPath = std::move(recoveryPath);
        m_snapshotPath.clear();
        m_overwriteProtected = false;
        m_sourceKind = SourceKind::Recovery;
    }

    void restoreSnapshot(std::string snapshotPath, std::string canonicalPath) {
        m_canonicalPath = std::move(canonicalPath);
        m_recoveryPath.clear();
        m_snapshotPath = std::move(snapshotPath);
        m_overwriteProtected = false;
        m_sourceKind = SourceKind::Snapshot;
    }

    void adoptCanonical(std::string canonicalPath) {
        m_canonicalPath = std::move(canonicalPath);
        m_recoveryPath.clear();
        m_snapshotPath.clear();
        m_overwriteProtected = false;
        m_sourceKind = m_canonicalPath.empty() ? SourceKind::Untitled : SourceKind::Canonical;
    }

    const std::string& canonicalPath() const { return m_canonicalPath; }
    const std::string& autosavePath() const { return m_autosavePath; }
    const std::string& recoveryPath() const { return m_recoveryPath; }
    const std::string& snapshotPath() const { return m_snapshotPath; }
    SourceKind sourceKind() const { return m_sourceKind; }
    void protectCanonicalFromOverwrite() { m_overwriteProtected = true; }
    bool requiresSaveAs() const { return m_canonicalPath.empty() || m_overwriteProtected; }
    bool isOverwriteProtected() const { return m_overwriteProtected; }
    bool isRecovered() const { return m_sourceKind == SourceKind::Recovery; }
    bool isSnapshotRestore() const { return m_sourceKind == SourceKind::Snapshot; }

    std::string windowTitle() const {
        std::string title = m_canonicalPath.empty() ? "Untitled - Aestra" : m_canonicalPath + " - Aestra";
        if (isRecovered()) {
            title = "Recovered - " + title;
        } else if (isSnapshotRestore()) {
            title = "Snapshot Restore - " + title;
        }
        if (m_overwriteProtected) {
            title = "Integrity Warning - " + title;
        }
        return title;
    }

private:
    std::string m_canonicalPath;
    std::string m_autosavePath;
    std::string m_recoveryPath;
    std::string m_snapshotPath;
    bool m_overwriteProtected{false};
    SourceKind m_sourceKind{SourceKind::Untitled};
};

} // namespace Aestra
