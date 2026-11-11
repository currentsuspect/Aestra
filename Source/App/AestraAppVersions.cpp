// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// The app's side of Versions (SPEC-017, V81-V1): the panel's data, the title-bar chip, and the branch picker.
#include "AestraApp.h"

#include "AestraRootComponent.h"
#include "TrackManagerUI.h"
#include "VersionPicker.h"
#include "VersionsPanelWiring.h"
#include "../Panels/TakesPanel.h"
#include "../Core/AestraWindowManager.h"
#include "../Settings/ConfirmationDialog.h"
#include "../AestraUI/Base/NUIContextMenu.h"
#include "../Core/AuditionHooks.h"
#include "AestraFileDialog.h"
#include <filesystem>

using namespace Aestra;

void AestraApp::wireVersionsPanel(Aestra::Audio::TakesPanel& panel) {
    const auto path = [this]() { return m_documentState.canonicalPath(); };
    wireVersionsDock(panel, [this]() -> Audio::CommandHistory& { return m_content->getTrackManager()->getCommandHistory(); },
                     [this]() { return m_documentState.displayName(); }, [this]() { m_content->refreshAfterHistoryChange(); });
    panel.setUnsavedProvider([this]() { return currentVersionEdits().steps; });
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
    // The open version's recorded steps feed the unsaved count; re-read them whenever this version may have
    // changed (a save rewrites them, a switch and a load move to another version).
    const std::string path = m_documentState.canonicalPath();
    const auto manifest = path.empty() ? TakeManager::Manifest{} : TakeManager::loadManifest(path);
    const auto* open = manifest.ok ? manifest.activeTake() : nullptr;
    m_openVersionSteps = open ? open->steps : std::vector<TakeManager::TakeEntry::Step>{};
    m_versionEditsStale = true;

    if (!m_windowManager) return;
    const auto chip = VersionPicker::chip(manifest);
    m_windowManager->setVersionChip(chip.branch, AestraUI::NUIColor::fromHex(VersionsTimeline::laneHex(chip.lane)), chip.version);
}

VersionEdits::State AestraApp::currentVersionEdits() {
    auto* tm = m_content ? m_content->getTrackManager().get() : nullptr;
    if (!tm) return {};
    const auto& history = tm->getCommandHistory();
    // Reading the stacks is O(1); measuring is O(steps). The frame loop asks every frame, so measure only
    // when the cursor moved — undo or redo always changes one of these two depths.
    const size_t undo = history.getUndoStack().size();
    const size_t redo = history.getRedoStack().size();
    if (m_versionEditsStale || undo != m_versionEditsUndoDepth || redo != m_versionEditsRedoDepth) {
        m_versionEdits = VersionEdits::measure(history, m_openVersionSteps);
        m_versionEditsUndoDepth = undo;
        m_versionEditsRedoDepth = redo;
        m_versionEditsStale = false;
    }
    return m_versionEdits;
}

void AestraApp::pushProjectStatus() {
    if (!m_windowManager) return;
    auto* tm = m_content ? m_content->getTrackManager().get() : nullptr;
    const bool modified = tm && tm->isModified();
    std::string note = m_documentState.statusNote();
    if (note.empty() && modified) {  // what has been done since the open version was saved
        const auto edits = currentVersionEdits();
        if (edits.counted && edits.steps > 0) note = std::to_string(edits.steps) + " unsaved";
        else if (edits.diverged) note = "Edited"; // steps were taken back: not a count, but not the version either
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
                requestSwitchToTake(id);
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

void AestraApp::requestSwitchToTake(const std::string& takeId) {
    if (takeId.empty() || m_documentState.canonicalPath().empty()) {
        switchToTake(takeId);
        return;
    }
    const auto edits = currentVersionEdits();
    if (!edits.diverged) { // the project is still the version: nothing to ask about
        switchToTake(takeId);
        return;
    }
    showVersionSwitchPrompt(takeId, edits);
}

void AestraApp::showVersionSwitchPrompt(const std::string& takeId, const VersionEdits::State& edits) {
    if (!m_windowManager) return;
    const auto manifest = TakeManager::loadManifest(m_documentState.canonicalPath());
    const std::string openName = VersionPicker::chip(manifest).version;

    // One line: the dialog draws its message without wrapping.
    std::string message;
    if (edits.counted && edits.steps > 0)
        message = "\"" + openName + "\" does not have your latest " + std::to_string(edits.steps) +
                  (edits.steps == 1 ? " step yet." : " steps yet.");
    else
        message = "The project has changed since \"" + openName + "\" was saved.";

    auto dialog = m_windowManager->getConfirmationDialog();
    if (!dialog) {
        dialog = std::make_shared<ConfirmationDialog>();
        m_windowManager->setConfirmationDialog(dialog);
    }
    if (auto* root = m_windowManager->getRootComponent()) {
        dialog->setBounds(root->getBounds());
        root->removeChild(dialog);
        root->addChild(dialog);
    }

    dialog->showVersionSwitch("Unsaved Work", message, [this, takeId](DialogResponse response) {
        // The answer decides everything, and only Discard leaves work behind (VersionEdits::plan, tested headlessly).
        const auto choice = response == DialogResponse::Save           ? VersionEdits::Choice::Save
                            : response == DialogResponse::SaveAsBranch ? VersionEdits::Choice::SaveAsNewBranch
                            : response == DialogResponse::DontSave     ? VersionEdits::Choice::Discard
                                                                       : VersionEdits::Choice::Cancel;
        const auto plan = VersionEdits::plan(choice);
        if (!plan.proceed) return; // Cancel or a dismissed dialog: nothing moves
        if (plan.saveAsNewBranch && !createTakeFromCurrentProject(true)) {
            Log::warning("[Versions] Could not save the working state as an alt; not switching");
            return;
        }
        const auto result = switchToTake(takeId, plan.saveCurrent);
        if (!result.ok) Log::error("[Versions] Could not open the version: " + result.errorMessage);
        if (auto panel = m_content->getTakesPanel()) panel->refreshTakes();
    });
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
        // The bounce renders the project as it is now, so the open version's snapshot must be the same state:
        // save it first (it also creates the Versions manifest a project opened from disk doesn't have yet;
        // without one the tile did nothing at all). A failed save means no bounce.
        if (!saveActiveTakeSnapshot()) return false;
        const auto manifest = TakeManager::loadManifest(path);
        const auto* open = manifest.ok ? manifest.activeTake() : nullptr;
        return open && bounceSavedVersion(m_documentState.canonicalPath(), open->id, [this](std::vector<float>& a, uint32_t& c, uint32_t& r) { return m_content->renderMixToBuffer(a, c, r); });
    };
    m_content->setAuditionHooks(std::move(hooks));
}
