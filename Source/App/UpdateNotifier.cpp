// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UpdateNotifier.h"

#include "AestraLog.h"

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

void UpdateNotifier::start() { m_checker->start(); }
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
