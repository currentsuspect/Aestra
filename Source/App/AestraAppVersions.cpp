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
#include <mutex>

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
    hooks.bounceNow = [this]() -> std::shared_ptr<BounceJob> {
        const std::string path = m_documentState.canonicalPath();
        if (path.empty()) return BounceJob::failed("Save the project first: a bounce is kept with the saved project.");
        // The bounce renders the project as it is now, so the open version's snapshot must be the same state:
        // save it first (it also creates the Versions manifest a project opened from disk doesn't have yet;
        // without one the tile did nothing at all). A failed save means no bounce.
        if (!saveActiveTakeSnapshot()) return BounceJob::failed("The project couldn't be saved, so nothing was bounced.");
        const auto manifest = TakeManager::loadManifest(path);
        const auto* open = manifest.ok ? manifest.activeTake() : nullptr;
        if (!open) return BounceJob::failed("No open version to bounce. Nothing was changed.");
        stopBounceWorker(); // the tile allows one at a time; this only reaps the last, finished thread

        // Owner rule (2026-11-08): no render on the UI thread. The live song renders on a worker, as Export does:
        // the stream is stopped and input is held (presses, wheel, keys) until it is done, so nothing edits or plays
        // what is being rendered, and the window keeps painting the tile filling up. A render of a fresh copy would
        // free the song meanwhile, but the offline path does not sound like the app yet (#1332).
        auto job = std::make_shared<BounceJob>();
        m_bounceJob = job;
        const bool resumeStream = m_content->pauseStreamForRender();
        auto queue = m_mainThreadQueue;
        auto* content = m_content.get(); // outlives the worker: shutdown() joins it first (stopBounceWorker)
        const std::string takeId = open->id;
        m_bounceThread = std::thread([job, queue, content, path, takeId, resumeStream]() {
            const std::string name = VersionBounce::writeBounce(path, takeId,
                [&](std::vector<float>& a, uint32_t& c, uint32_t& r) {
                    return content->renderMixCore(a, c, r,
                        [&job](float pct) { job->progress.store(std::max(0.01f, pct), std::memory_order_relaxed); },
                        [&job]() { return job->cancel.load(std::memory_order_relaxed); });
                },
                [](const std::vector<float>& audio, uint32_t channels, uint32_t rate) { return Audio::analyzeAudio(audio, channels, rate).integratedLufs; });
            std::lock_guard<std::mutex> lock(queue->mutex);
            if (queue->shuttingDown) return;
            queue->tasks.push_back([job, content, path, takeId, name, resumeStream]() {
                content->resumeStreamAfterRender(resumeStream);
                const bool ok = !name.empty() && TakeManager::setVersionBounce(path, takeId, name).ok;
                job->outcome = {ok, ok ? std::string() : "The timeline didn't render. Is there anything on it?"};
                job->finished = true;
            });
        });
        return job;
    };
    m_content->setAuditionHooks(std::move(hooks));
    // Input is held only while a bounce is rendering the live song (and released on the frame its result lands).
    if (m_windowManager) m_windowManager->setInputHold([this]() { return m_bounceJob && !m_bounceJob->finished; });
}

void AestraApp::stopBounceWorker() {
    if (m_bounceJob) m_bounceJob->cancel.store(true);
    if (m_bounceThread.joinable()) m_bounceThread.join();
    m_bounceJob.reset();
}
