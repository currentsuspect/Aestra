// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// One install, start to "ready to restart": preflight, download (resumable), verify, unpack. It runs on its own thread
// and can be asked its state at any time (the Settings page does, every frame) and cancelled until unpacking starts.
// It stops at Ready: replacing the running program happens only when the user restarts (UpdateInstall.h: launchUpdater).

#include "UpdateInstall.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace Aestra {
namespace License {

enum class InstallPhase {
    Idle,
    Downloading,
    Unpacking,
    Ready,      ///< staged and verified: restarting will install it
    Failed,     ///< message says why; Start again to retry (a partial download resumes)
    Cancelled,
};

struct InstallState {
    InstallPhase phase = InstallPhase::Idle;
    uint64_t done = 0;   ///< bytes downloaded
    uint64_t total = 0;  ///< bytes expected
    std::string message; ///< a sentence for the page when Failed
    std::string version; ///< the version being installed
};

class UpdateSession {
public:
    explicit UpdateSession(std::unique_ptr<IHttpTransport> transport);
    ~UpdateSession();
    UpdateSession(const UpdateSession&) = delete;
    UpdateSession& operator=(const UpdateSession&) = delete;

    /// Begin installing @p manifest. False if one is already running. Starting again after Failed or Cancelled retries; after
    /// Ready for the same version it does nothing (already done).
    bool start(const UpdateConfig& config, const UpdateManifest& manifest, const UpdateLayout& layout);
    /// Stop a download in progress (the part file stays, so the next start resumes). Unpacking cannot be interrupted.
    void cancel();

    InstallState state() const;
    bool busy() const;
    /// The folder a Ready update was unpacked into (empty unless Ready).
    std::string stagedDir() const;

private:
    void run(UpdateConfig config, UpdateManifest manifest, UpdateLayout layout);
    void set(InstallPhase phase, const std::string& message = std::string());

    std::unique_ptr<IHttpTransport> m_transport;
    mutable std::mutex m_mutex;
    InstallState m_state;
    std::string m_staged;
    std::thread m_thread;
    std::atomic<bool> m_cancel{false};
};

/// A sentence for a failed download, for the page and the log.
std::string describeDownloadFailure(const DownloadOutcome& outcome);

} // namespace License
} // namespace Aestra
