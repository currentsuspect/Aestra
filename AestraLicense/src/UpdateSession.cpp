// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UpdateSession.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace Aestra {
namespace License {

std::string describeDownloadFailure(const DownloadOutcome& outcome) {
    switch (outcome.status) {
    case DownloadStatus::Unauthorized: return "The update server did not accept your founder key.";
    case DownloadStatus::NetworkError: return "The download was interrupted. Try again: it carries on from where it stopped. (" + outcome.detail + ")";
    case DownloadStatus::SizeMismatch:
    case DownloadStatus::HashMismatch: return "The download did not match what was published, so it was thrown away and nothing was installed. Try again.";
    case DownloadStatus::DiskError: return outcome.detail;
    case DownloadStatus::BadConfig: return "The update could not be requested safely: " + outcome.detail;
    case DownloadStatus::Cancelled: return "Cancelled.";
    case DownloadStatus::Ok: return {};
    }
    return outcome.detail;
}

UpdateSession::UpdateSession(std::unique_ptr<IHttpTransport> transport) : m_transport(std::move(transport)) {}

UpdateSession::~UpdateSession() {
    m_cancel = true;
    if (m_thread.joinable()) m_thread.join();
}

bool UpdateSession::busy() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state.phase == InstallPhase::Downloading || m_state.phase == InstallPhase::Unpacking;
}

InstallState UpdateSession::state() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

std::string UpdateSession::stagedDir() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state.phase == InstallPhase::Ready ? m_staged : std::string();
}

void UpdateSession::cancel() { m_cancel = true; }

void UpdateSession::set(InstallPhase phase, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_state.phase = phase;
    m_state.message = message;
}

bool UpdateSession::start(const UpdateConfig& config, const UpdateManifest& manifest, const UpdateLayout& layout) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state.phase == InstallPhase::Downloading || m_state.phase == InstallPhase::Unpacking) return false;
        if (m_state.phase == InstallPhase::Ready && m_state.version == manifest.version) return true; // already staged
    }
    if (m_thread.joinable()) m_thread.join(); // the previous run has finished (its phase is no longer a running one)
    m_cancel = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = InstallState();
        m_state.phase = InstallPhase::Downloading;
        m_state.total = manifest.size;
        m_state.version = manifest.version;
        m_staged.clear();
    }
    m_thread = std::thread([this, config, manifest, layout] { run(config, manifest, layout); });
    return true;
}

void UpdateSession::run(UpdateConfig config, UpdateManifest manifest, UpdateLayout layout) {
    std::string why;
    if (!installLocationWritable(layout.appDir, &why)) {
        set(InstallPhase::Failed, why);
        return;
    }

    const DownloadOutcome got = downloadUpdatePackage(*m_transport, config, manifest, layout.downloadDir, [this](uint64_t done, uint64_t total) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_state.done = done;
            if (total > 0) m_state.total = total;
        }
        return !m_cancel.load();
    });
    if (got.status == DownloadStatus::Cancelled || (m_cancel.load() && got.status != DownloadStatus::Ok)) {
        set(InstallPhase::Cancelled, "Cancelled.");
        return;
    }
    if (got.status != DownloadStatus::Ok) {
        set(InstallPhase::Failed, describeDownloadFailure(got));
        return;
    }

    set(InstallPhase::Unpacking);
    std::string error;
    if (!extractUpdatePackage(got.path, layout.stagedDir(), layout.executableName, &error)) {
        set(InstallPhase::Failed, "The update could not be unpacked: " + error);
        return;
    }
    std::error_code ec;
    fs::remove(got.path, ec); // the unpacked copy is what gets installed; the package is not needed any more
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_staged = layout.stagedDir();
        m_state.phase = InstallPhase::Ready;
        m_state.done = m_state.total;
        m_state.message.clear();
    }
}

} // namespace License
} // namespace Aestra
