// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The app's side of updates: owns the background checker, the saved choices (channel, founder key, automatic checks), and
// the one unprompted message ("Aestra 0.8.2 is available"). Installing is a separate step (Settings > Updates).
// Results arrive on the checker's thread and are handed to the main thread before anything touches the UI.

#include "UpdateChecker.h"
#include "UpdateSession.h"
#include "UpdateSettings.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace Aestra {

class UpdateNotifier {
public:
    struct Hooks {
        std::function<void(std::function<void()>)> postToMain; ///< run on the main thread
        std::function<void(const std::string&)> showMessage;   ///< a toast; main thread only
        std::function<void()> requestQuit;                     ///< close Aestra the normal way (with its save prompts)
        std::string settingsPath;                              ///< updates.json
    };

    explicit UpdateNotifier(Hooks hooks);
    ~UpdateNotifier();

    void start();
    void stop();
    void checkNow();

    License::UpdateSettings settings() const;
    /// Save and apply (the next check uses them). Safe from the main thread.
    void setSettings(const License::UpdateSettings& settings);
    License::UpdateCheckResult lastResult() const;
    bool hasResult() const;
    uint64_t resultCount() const;

    // Installing. Download, verify and unpack run in the background and stop at Ready; nothing is replaced until the user
    // restarts. The swap itself is done by AestraUpdater once Aestra has closed.
    void startInstall();
    void cancelInstall();
    License::InstallState installState() const;
    /// Ask Aestra to close the usual way; as it exits, the staged update is installed and Aestra starts again.
    void restartToUpdate();
    /// Called as Aestra exits: if the user asked to restart into an update, hand over to the updater.
    void applyPendingRestart();

    /// The version this build reports (AESTRA_UPDATE_VERSION_STRING).
    static std::string currentVersion();

private:
    License::UpdateConfig makeConfig() const;
    void onResult(const License::UpdateCheckResult& result);
    void save() const;

    Hooks m_hooks;
    mutable std::mutex m_mutex;
    License::UpdateSettings m_settings;
    std::filesystem::path dataDir() const;
    void handleLastUpdate();
    License::UpdateLayout makeLayout(std::string* error) const;

    std::unique_ptr<License::UpdateSession> m_session;
    License::UpdateLayout m_layout;
    std::string m_installingVersion;
    std::string m_localFailure;     ///< an install that could not even begin (no program path, ...)
    bool m_restartRequested = false;
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)}; ///< main-thread tasks bail if the notifier is gone
    std::unique_ptr<License::UpdateChecker> m_checker;
};

} // namespace Aestra
