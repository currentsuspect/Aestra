// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// The app's side of updates (V82-U2): start the background check once the window has content, stop it first at shutdown.
#include "AestraApp.h"

#include "UpdateNotifier.h"
#include "../Core/AestraContent.h"
#include "../Settings/SettingsDialog.h"
#include "../Settings/UpdatesSettingsPage.h"

#include <filesystem>

using namespace Aestra;

void AestraApp::startUpdateChecks() {
    if (m_updateNotifier) return;
    UpdateNotifier::Hooks hooks;
    if (IPlatformUtils* utils = Platform::getUtils()) {
        const std::filesystem::path dir(utils->getAppDataPath("Aestra"));
        if (!dir.empty()) hooks.settingsPath = (dir / "updates.json").string();
    }
    hooks.postToMain = [this](std::function<void()> task) { enqueueMainThreadTask(std::move(task)); };
    hooks.requestQuit = [this]() { requestClose(); }; // the usual way out, so unsaved work is asked about first
    hooks.showMessage = [this](const std::string& text) {
        if (m_content) m_content->showToast(text, 9.0);
    };
    m_updateNotifier = std::make_shared<UpdateNotifier>(std::move(hooks));
    m_updateNotifier->start();
}

void AestraApp::stopUpdateChecks() {
    if (!m_updateNotifier) return;
    m_updateNotifier->applyPendingRestart(); // the user chose "Restart and update": hand over to the updater as we exit
    m_updateNotifier->stop(); // joins the checker thread: no result can arrive after this
    m_updateNotifier.reset();
}

void AestraApp::addUpdatesPage(Aestra::SettingsDialog& dialog) {
    // The page needs the notifier; if the checks were never started (no content yet) there is nothing for it to talk to.
    if (m_updateNotifier) dialog.addPage(std::make_shared<UpdatesSettingsPage>(m_updateNotifier));
}
