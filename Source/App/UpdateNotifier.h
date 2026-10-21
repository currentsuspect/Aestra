// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The app's side of updates: owns the background checker, the saved choices (channel, founder key, automatic checks), and
// the one unprompted message ("Aestra 0.8.2 is available"). Installing is a separate step (Settings > Updates).
// Results arrive on the checker's thread and are handed to the main thread before anything touches the UI.

#include "UpdateChecker.h"
#include "UpdateSettings.h"

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

    /// The version this build reports (AESTRA_UPDATE_VERSION_STRING).
    static std::string currentVersion();

private:
    License::UpdateConfig makeConfig() const;
    void onResult(const License::UpdateCheckResult& result);
    void save() const;

    Hooks m_hooks;
    mutable std::mutex m_mutex;
    License::UpdateSettings m_settings;
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)}; ///< main-thread tasks bail if the notifier is gone
    std::unique_ptr<License::UpdateChecker> m_checker;
};

} // namespace Aestra
