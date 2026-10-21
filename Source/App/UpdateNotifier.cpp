// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UpdateNotifier.h"

#include "AestraLog.h"
#include "AestraJSON.h"
#include "UpdateInstall.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifndef AESTRA_UPDATE_VERSION_STRING
#define AESTRA_UPDATE_VERSION_STRING "0.0.0"
#endif

namespace Aestra {

using namespace License;

namespace {

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

// The founder key is a credential: the file is for the user alone, and is replaced whole so a crash cannot leave half of it.
void writeFileAtomically(const std::string& path, const std::string& text) {
    std::error_code ec;
    const std::filesystem::path target(path);
    std::filesystem::create_directories(target.parent_path(), ec);
    const std::filesystem::path temp = target.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << text;
    }
    std::filesystem::permissions(temp, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace, ec);
    std::filesystem::rename(temp, target, ec);
    if (ec) Log::warning("[Update] could not save " + path + ": " + ec.message());
}

} // namespace

std::string UpdateNotifier::currentVersion() { return AESTRA_UPDATE_VERSION_STRING; }

UpdateNotifier::UpdateNotifier(Hooks hooks) : m_hooks(std::move(hooks)) {
    if (!m_hooks.settingsPath.empty()) m_settings = parseUpdateSettings(readFile(m_hooks.settingsPath));

    auto transport = createDefaultHttpTransport();
    m_checker = std::make_unique<UpdateChecker>(std::move(transport), [this] { return makeConfig(); },
                                                [this](const UpdateCheckResult& r) { onResult(r); });
    m_checker->setAutomaticAllowed([this] { return settings().autoCheck; });
}

UpdateNotifier::~UpdateNotifier() { stop(); }

void UpdateNotifier::start() {
    handleLastUpdate();
    m_checker->start();
}
void UpdateNotifier::stop() {
    if (m_checker) m_checker->stop();
}
void UpdateNotifier::checkNow() { m_checker->checkNow(); }

UpdateSettings UpdateNotifier::settings() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_settings;
}

void UpdateNotifier::setSettings(const UpdateSettings& settings) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_settings = settings;
    }
    save();
}

UpdateCheckResult UpdateNotifier::lastResult() const { return m_checker->lastResult(); }
bool UpdateNotifier::hasResult() const { return m_checker->hasResult(); }
uint64_t UpdateNotifier::resultCount() const { return m_checker->resultCount(); }

std::filesystem::path UpdateNotifier::dataDir() const {
    return m_hooks.settingsPath.empty() ? std::filesystem::path() : std::filesystem::path(m_hooks.settingsPath).parent_path();
}

// What happened to the last update, told once, and the old version's backup cleared only at the start AFTER the new
// version has run for a whole session.
void UpdateNotifier::handleLastUpdate() {
    namespace fs = std::filesystem;
    const fs::path dir = dataDir();
    if (dir.empty()) return;
    const fs::path fresh = dir / "last-update.json";
    const fs::path seen = dir / "last-update.seen.json";
    std::error_code ec;
    const std::string exe = currentExecutablePath();
    const fs::path appDir = exe.empty() ? fs::path() : fs::path(exe).parent_path();

    if (fs::exists(fresh, ec)) {
        std::string text = readFile(fresh.string());
        bool ok = false, rolledBack = false;
        std::string to, message;
        try {
            const JSON j = JSON::parse(text);
            ok = j["ok"].isBool() && j["ok"].asBool();
            rolledBack = j["rolledBack"].isBool() && j["rolledBack"].asBool();
            if (j["to"].isString()) to = j["to"].asString();
            if (j["message"].isString()) message = j["message"].asString();
        } catch (...) {
            fs::remove(fresh, ec);
            return;
        }
        Log::info(std::string("[Update] last update: ") + (ok ? "installed " : "did not install ") + to + " (" + message + ")");
        if (m_hooks.showMessage) {
            if (ok) m_hooks.showMessage("Aestra was updated to " + to + ".");
            else m_hooks.showMessage("The update to " + to + " did not work" + (rolledBack ? " and was undone" : "") + ": " + message + ".");
        }
        fs::rename(fresh, seen, ec);
        return;
    }
    if (fs::exists(seen, ec) && !appDir.empty()) {
        // One whole session has passed since the update: the new version is the one to keep.
        const std::string text = readFile(seen.string());
        bool ok = false;
        try {
            const JSON j = JSON::parse(text);
            ok = j["ok"].isBool() && j["ok"].asBool();
        } catch (...) {
        }
        if (ok) fs::remove_all(appDir.string() + ".old", ec);
        fs::remove(seen, ec);
    }
}

UpdateLayout UpdateNotifier::makeLayout(std::string* error) const {
    namespace fs = std::filesystem;
    UpdateLayout layout;
    const std::string exe = currentExecutablePath();
    if (exe.empty() || dataDir().empty()) {
        if (error) *error = "Aestra cannot tell where it is installed, so it cannot update itself.";
        return layout;
    }
    const fs::path exePath(exe);
    layout.appDir = exePath.parent_path().string();
    layout.executableName = exePath.filename().string();
    layout.updaterName = std::string("AestraUpdater") + (exePath.extension() == ".exe" ? ".exe" : "");
    layout.downloadDir = (dataDir() / "updates-download").string();
    return layout;
}

void UpdateNotifier::startInstall() {
    const UpdateCheckResult result = lastResult();
    if (result.status != UpdateCheckStatus::Available) return;
    m_localFailure.clear();
    std::string error;
    const UpdateLayout layout = makeLayout(&error);
    if (layout.appDir.empty()) {
        m_localFailure = error;
        return;
    }
    if (!m_session) m_session = std::make_unique<UpdateSession>(createDefaultHttpTransport());
    m_layout = layout;
    m_installingVersion = result.manifest.version;
    m_restartRequested = false;
    m_session->start(makeConfig(), result.manifest, layout);
}

void UpdateNotifier::cancelInstall() {
    if (m_session) m_session->cancel();
}

InstallState UpdateNotifier::installState() const {
    if (!m_localFailure.empty()) {
        InstallState failed;
        failed.phase = InstallPhase::Failed;
        failed.message = m_localFailure;
        return failed;
    }
    return m_session ? m_session->state() : InstallState();
}

void UpdateNotifier::restartToUpdate() {
    if (!m_session || m_session->state().phase != InstallPhase::Ready) return;
    m_restartRequested = true;
    if (m_hooks.requestQuit) m_hooks.requestQuit();
}

void UpdateNotifier::applyPendingRestart() {
    if (!m_restartRequested || !m_session || m_session->state().phase != InstallPhase::Ready) return;
    m_restartRequested = false;
    std::string error;
    const std::filesystem::path result = dataDir() / "last-update.json";
    if (!launchUpdater(m_layout, result.string(), currentVersion(), m_installingVersion, currentProcessId(), &error))
        Log::error("[Update] could not hand over to the updater: " + error);
    else
        Log::info("[Update] handed over to the updater for " + m_installingVersion);
}

UpdateConfig UpdateNotifier::makeConfig() const {
    const UpdateSettings s = settings();
    UpdateConfig config;
    config.channel = s.channel;
    config.founderKey = s.founderKey;
    config.currentVersion = currentVersion();
    config.platform = currentUpdatePlatform();
    std::memcpy(config.publicKey, bundledUpdatePublicKey(), 32);
    return config;
}

void UpdateNotifier::save() const {
    if (m_hooks.settingsPath.empty()) return;
    writeFileAtomically(m_hooks.settingsPath, serializeUpdateSettings(settings()));
}

void UpdateNotifier::onResult(const UpdateCheckResult& result) {
    // Never the key, never the feed body: the status and one line.
    Log::info("[Update] " + std::string(result.status == UpdateCheckStatus::Available ? "available" : "no update") + ": " + result.detail);
    if (!shouldAnnounceUpdate(result.status, result.manifest.version, settings()) || !m_hooks.postToMain) return;

    const std::string version = result.manifest.version;
    const std::weak_ptr<bool> alive = m_alive;
    m_hooks.postToMain([this, version, alive]() {
        if (alive.expired()) return;
        UpdateSettings s = settings();
        if (!shouldAnnounceUpdate(UpdateCheckStatus::Available, version, s)) return; // announced meanwhile
        s.lastNotified = version;
        setSettings(s);
        if (m_hooks.showMessage) m_hooks.showMessage("Aestra " + version + " is available. Settings > Updates has the details.");
    });
}

} // namespace Aestra
