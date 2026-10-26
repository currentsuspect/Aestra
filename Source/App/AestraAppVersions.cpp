// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// The app's side of Versions (SPEC-017, V81-V1): the panel's data, the title-bar chip, and the branch picker.
#include "AestraApp.h"

#include "AestraRootComponent.h"
#include "TrackManagerUI.h"
#include "VersionPicker.h"
#include "VersionsPanelWiring.h"
#include "../Panels/TakesPanel.h"
#include "../Core/AestraWindowManager.h"
#include "../AestraUI/Base/NUIContextMenu.h"
#include "../Core/AuditionHooks.h"
#include "AestraFileDialog.h"
#include <filesystem>

using namespace Aestra;

void AestraApp::wireVersionsPanel(Aestra::Audio::TakesPanel& panel) {
    const auto path = [this]() { return m_documentState.canonicalPath(); };
    wireVersionsDock(panel, [this]() -> Audio::CommandHistory& { return m_content->getTrackManager()->getCommandHistory(); }, path,
                     [this]() { return m_documentState.displayName(); }, [this]() { m_content->refreshAfterHistoryChange(); });
    wireVersionBounces(panel, path, [this](const std::string& p) { return m_content->togglePreviewFile(p); },
                       [this](const std::string& p) { return m_content->isPreviewFilePlaying(p); },
                       [this, path](const std::string& id) {
                           return bounceSavedVersion(path(), id, [this](std::vector<float>& a, uint32_t& c, uint32_t& r) { return m_content->renderMixToBuffer(a, c, r); });
                       });
    wireBringOver(panel, path, [this]() { return m_content->getTrackManager().get(); },
                  [this](const std::string&) { m_content->refreshAfterHistoryChange(); return true; });
    panel.setOnRefreshed([this]() { refreshVersionChip(); });
    if (m_windowManager) m_windowManager->setOnVersionChip([this](float x) { showVersionPicker(x); });
}

void AestraApp::refreshVersionChip() {
    if (!m_windowManager) return;
    const std::string path = m_documentState.canonicalPath();
    const auto manifest = path.empty() ? TakeManager::Manifest{} : TakeManager::loadManifest(path);
    const auto chip = VersionPicker::chip(manifest);
    const auto* open = manifest.ok ? manifest.activeTake() : nullptr;
    m_recordedSteps = open ? static_cast<int>(open->steps.size()) : 0;
    m_windowManager->setVersionChip(chip.branch, AestraUI::NUIColor::fromHex(VersionsTimeline::laneHex(chip.lane)), chip.version);
}

void AestraApp::pushProjectStatus() {
    if (!m_windowManager) return;
    auto* tm = m_content ? m_content->getTrackManager().get() : nullptr;
    const bool modified = tm && tm->isModified();
    std::string note = m_documentState.statusNote();
    if (note.empty() && modified) {  // "7 unsaved": what has been done since the open version was saved
        const int n = std::max(0, static_cast<int>(tm->getCommandHistory().getUndoStack().size()) - m_recordedSteps);
        if (n > 0) note = std::to_string(n) + " unsaved";
    }
    m_windowManager->setProjectStatus(m_documentState.displayName(), modified, note);
}

void AestraApp::showVersionPicker(float x) {
    const auto manifest = TakeManager::loadManifest(m_documentState.canonicalPath());
    const auto entries = VersionPicker::entries(manifest);
    if (!m_windowManager || entries.empty()) return;
    auto menu = std::make_shared<AestraUI::NUIContextMenu>();
    auto header = std::make_shared<AestraUI::NUIContextMenuItem>("BRANCHES");
    header->setEnabled(false);
    menu->addItem(header);
    for (const auto& e : entries) {
        auto item = std::make_shared<AestraUI::NUIContextMenuItem>(e.branch);
        item->setShortcut(e.versionName);
        item->setSwatch(AestraUI::NUIColor::fromHex(VersionsTimeline::laneHex(e.lane)));
        item->setChecked(e.current);
        if (!e.current) {
            item->setOnClick([this, id = e.versionId]() {
                if (!switchToTake(id).ok) return;
                refreshVersionChip();
                if (auto panel = m_content->getTakesPanel()) panel->refreshTakes();
            });
        }
        menu->addItem(item);
    }
    menu->addSeparator();
    menu->addItem("Open Versions...", [this]() { m_content->setViewOpen(Audio::ViewType::Takes, true); });
    menu->addItem("Save as an alt...", [this]() { if (createTakeFromCurrentProject(true)) refreshVersionChip(); });
    m_windowManager->showDropdownMenu(menu, x);
}

/// What the Audition hub gets from the app: the open project, a place to keep references, and the two requests.
void AestraApp::wireAudition() {
    if (!m_content) return;
    AuditionHooks hooks;
    hooks.project = [this]() { return std::make_pair(m_documentState.displayName(), m_documentState.canonicalPath()); };
    const std::string autosave = autosavePathOrEmpty();
    if (!autosave.empty()) hooks.libraryFile = (std::filesystem::path(autosave).parent_path() / "audition_references.txt").string();
    hooks.importReference = [this]() {
        const std::string filter("Audio\0*.wav;*.flac;*.mp3;*.ogg;*.aiff;*.aif;*.m4a\0All Files\0*.*\0", sizeof("Audio\0*.wav;*.flac;*.mp3;*.ogg;*.aiff;*.aif;*.m4a\0All Files\0*.*\0") - 1);
        pickFileAsync([filter](Aestra::IPlatformUtils& utils) { return utils.openFileDialog("Import a reference", filter); },
                      [this](const std::string& picked) { if (!picked.empty() && m_content) m_content->addAuditionReference(picked); });
    };
    hooks.bounceNow = [this]() {
        const std::string path = m_documentState.canonicalPath();
        if (path.empty()) return false;
        auto manifest = TakeManager::loadManifest(path);
        // A project opened from disk has no Versions manifest until its first save, and the tile did nothing at
        // all. Bouncing is a save of the current state as the open version, so make that version first.
        if (!(manifest.ok && manifest.activeTake()) && saveActiveTakeSnapshot()) manifest = TakeManager::loadManifest(path);
        const auto* open = manifest.ok ? manifest.activeTake() : nullptr;
        return open && bounceSavedVersion(m_documentState.canonicalPath(), open->id, [this](std::vector<float>& a, uint32_t& c, uint32_t& r) { return m_content->renderMixToBuffer(a, c, r); });
    };
    m_content->setAuditionHooks(std::move(hooks));
}
