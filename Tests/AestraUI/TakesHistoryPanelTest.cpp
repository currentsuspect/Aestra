// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Takes/History workspace panel behavior:
//   * History panel presents the session command timeline (undo + redo stacks),
//     refreshes on state change, and navigates safely through activateEntry.
//   * Takes panel lists the takes manifest with lineage depth, routes every
//     action through app-layer callbacks, supports inline rename via key
//     events, and never mutates project state itself.
//   * Panel opening: hidden panels ignore input; showing + refresh presents
//     current data.

#include "../../Source/Panels/AestraHistoryPanel.h"
#include "../../Source/Panels/TakesPanel.h"

#include "../../Source/Core/TakeManager.h"
#include "../Support/TestTempDirectory.h"
#include "Commands/ICommand.h"
#include "Models/TrackManager.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

using Aestra::Audio::AestraHistoryPanel;
using Aestra::Audio::TakesPanel;
using Aestra::Audio::TrackManager;

namespace {

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

/** Minimal named command so the history panel has real timeline entries. */
class NamedTestCommand : public Aestra::Audio::ICommand {
public:
    NamedTestCommand(std::string name, int& counter) : m_name(std::move(name)), m_counter(counter) {}
    void execute() override { ++m_counter; }
    void undo() override { --m_counter; }
    void redo() override { ++m_counter; }
    std::string getName() const override { return m_name; }

private:
    std::string m_name;
    int& m_counter;
};

std::string readFileContents(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

AestraUI::NUIKeyEvent keyPress(AestraUI::NUIKeyCode code, char character = 0) {
    AestraUI::NUIKeyEvent e;
    e.keyCode = code;
    e.character = character;
    e.pressed = true;
    return e;
}

AestraUI::NUIMouseEvent leftPress(float x, float y) {
    AestraUI::NUIMouseEvent e;
    e.type = AestraUI::NUIMouseEventType::Down;
    e.pressed = true;
    e.button = AestraUI::NUIMouseButton::Left;
    e.position = {x, y};
    return e;
}

// =============================================================================
// History panel — command timeline presentation and navigation
// =============================================================================

void testHistoryPanel() {
    auto trackManager = std::make_shared<TrackManager>();
    auto& history = trackManager->getCommandHistory();

    AestraHistoryPanel panel(trackManager);
    panel.setBounds(AestraUI::NUIRect(0.0f, 0.0f, 280.0f, 460.0f));
    panel.setVisible(true);

    require(panel.getEntryCount() == 0, "History panel should start empty");

    int counter = 0;
    history.pushAndExecute(std::make_shared<NamedTestCommand>("Move Clip", counter));
    history.pushAndExecute(std::make_shared<NamedTestCommand>("Add Track", counter));
    history.pushAndExecute(std::make_shared<NamedTestCommand>("Change Volume", counter));
    require(counter == 3, "Commands should have executed");

    panel.refreshHistory();
    require(panel.getEntryCount() == 3, "Panel should present all executed commands");
    for (int i = 0; i < 3; ++i) {
        require(!panel.isEntryRedo(i), "No redo entries expected before undo");
    }
    bool sawMove = false, sawAdd = false, sawVolume = false;
    for (int i = 0; i < 3; ++i) {
        const auto& name = panel.getEntryName(i);
        sawMove = sawMove || name == "Move Clip";
        sawAdd = sawAdd || name == "Add Track";
        sawVolume = sawVolume || name == "Change Volume";
    }
    require(sawMove && sawAdd && sawVolume, "Panel entries should carry command names");

    // Undo → most recent command moves to the redo (greyed) region.
    require(history.undo(), "Undo should succeed");
    panel.refreshHistory();
    require(panel.getEntryCount() == 3, "Undone commands stay visible in the timeline");
    int redoCount = 0;
    for (int i = 0; i < panel.getEntryCount(); ++i) {
        if (panel.isEntryRedo(i)) {
            ++redoCount;
            require(panel.getEntryName(i) == "Change Volume", "Undone command should appear as redo entry");
        }
    }
    require(redoCount == 1, "Exactly one redo entry expected after a single undo");
    require(counter == 2, "Undo should have reverted the command");

    // Navigate by clicking the redo entry — redoes it and fires the callback.
    int historyChangedCalls = 0;
    panel.setOnHistoryChanged([&historyChangedCalls]() { ++historyChangedCalls; });
    int redoIndex = -1;
    for (int i = 0; i < panel.getEntryCount(); ++i) {
        if (panel.isEntryRedo(i)) {
            redoIndex = i;
            break;
        }
    }
    require(redoIndex >= 0, "Redo entry should exist");
    panel.activateEntry(redoIndex);
    require(counter == 3, "Activating a redo entry should re-apply the command");
    require(!history.canRedo(), "Redo stack should be drained");
    require(historyChangedCalls == 1, "History navigation should notify listeners");

    // Jump back to the beginning of the session through the timeline.
    panel.activateEntry(panel.getEntryCount() - 1); // deepest undo row keeps that stackIndex
    require(history.canRedo(), "Timeline jump should produce redoable commands");
    require(counter < 3, "Timeline jump should have undone commands");

    // Hidden panel must ignore input entirely.
    panel.setVisible(false);
    require(!panel.onMouseEvent(leftPress(10.0f, 50.0f)), "Hidden history panel must not consume events");

    std::cout << "[PASS] History panel timeline\n";
}

// =============================================================================
// Takes panel — manifest presentation, actions, rename, non-destructive branch
// =============================================================================

void testTakesPanel() {
    const Aestra::Tests::ScopedTempDirectory tempDirScope{"TakesPanel"};
    const auto projectPath = (tempDirScope.path() / "panel_project.aes").string();

    auto init = TakeManager::ensureManifest(projectPath, "state-main", "Main");
    require(init.ok, "Failed to initialize takes manifest");
    auto created = TakeManager::createTake(projectPath, "state-idea-b", "Idea B");
    require(created.ok, "Failed to create second take");

    TakesPanel panel;
    panel.setBounds(AestraUI::NUIRect(0.0f, 0.0f, 320.0f, 480.0f));
    panel.setVisible(true);
    panel.setTakesProvider([&projectPath]() { return TakeManager::loadManifest(projectPath); });
    panel.setSnapshotsProvider([]() {
        return std::vector<TakesPanel::SnapshotEntry>{{"/nonexistent/snapshot.aes", "Jul 16 10:00"}};
    });

    // Opening semantics: refresh presents current manifest + snapshots.
    panel.refreshTakes();
    require(panel.getTakeCount() == 2, "Panel should list both takes");
    require(panel.getSnapshotCount() == 1, "Panel should list recovery snapshots");

    int mainRow = -1, ideaRow = -1;
    for (int i = 0; i < panel.getTakeCount(); ++i) {
        if (panel.getTakeAt(i).id == "main") mainRow = i;
        if (panel.getTakeAt(i).name == "Idea B") ideaRow = i;
    }
    require(mainRow >= 0 && ideaRow >= 0, "Both takes should be present");
    require(panel.getTakeDepthAt(mainRow) == 0, "Root take should have depth 0");
    require(panel.getTakeDepthAt(ideaRow) == 1, "Child take should be indented under its parent");
    require(panel.getTakeAt(ideaRow).active, "Newly created take should be marked active");

    // Open action routes through the callback with the selected take's id.
    std::string openedId;
    panel.setOnOpenTake([&](const std::string& takeId) {
        openedId = takeId;
        // Mimic the app: activate the take in the manifest (snapshot load is app-side).
        return TakeManager::setActiveTake(projectPath, takeId).ok;
    });
    panel.selectTake(mainRow);
    require(panel.getSelectedTake() == mainRow, "Selection should stick");
    require(panel.requestOpenSelected(), "Open action should succeed");
    require(openedId == "main", "Open action should receive the selected take id");
    // The refresh after the action must reflect the new active take.
    for (int i = 0; i < panel.getTakeCount(); ++i) {
        if (panel.getTakeAt(i).id == "main") {
            require(panel.getTakeAt(i).active, "Manifest refresh should show the opened take as active");
        }
    }

    // Create action → new take appears after the built-in refresh.
    panel.setOnCreateTake([&]() { return TakeManager::createTake(projectPath, "state-c", "Idea C").ok; });
    require(panel.requestCreateTake(), "Create action should succeed");
    require(panel.getTakeCount() == 3, "New take should appear in the panel");

    // Mouse selection: click on the first take row.
    panel.selectTake(-1);
    const float rowCenterY = 36.0f + 30.0f + 13.0f; // header + toolbar + half row
    require(panel.onMouseEvent(leftPress(160.0f, rowCenterY)), "Row click should be consumed");
    require(panel.getSelectedTake() == 0, "Click should select the first take row");

    // Inline rename: keyboard-driven edit, committed through the callback.
    panel.setOnRenameTake([&](const std::string& takeId, const std::string& name) {
        return TakeManager::renameTake(projectPath, takeId, name).ok;
    });
    for (int i = 0; i < panel.getTakeCount(); ++i) {
        if (panel.getTakeAt(i).name == "Idea C") panel.selectTake(i);
    }
    panel.beginRenameSelected();
    require(panel.isRenameActive(), "Rename edit should be active");
    // Clear the prefilled name, type a new one, commit with Enter.
    while (!panel.getRenameBuffer().empty()) {
        require(panel.handleKeyEvent(keyPress(AestraUI::NUIKeyCode::Backspace)), "Backspace should be consumed");
    }
    for (char c : std::string("Chorus V2")) {
        require(panel.handleKeyEvent(keyPress(AestraUI::NUIKeyCode::Unknown, c)), "Typing should be consumed");
    }
    require(panel.getRenameBuffer() == "Chorus V2", "Rename buffer should reflect typed name");
    require(panel.handleKeyEvent(keyPress(AestraUI::NUIKeyCode::Enter)), "Enter should commit rename");
    require(!panel.isRenameActive(), "Rename edit should end on commit");
    auto renamedManifest = TakeManager::loadManifest(projectPath);
    require(renamedManifest.ok, "Manifest should reload after rename");
    bool foundRenamed = false;
    for (const auto& take : renamedManifest.takes) {
        foundRenamed = foundRenamed || take.name == "Chorus V2";
    }
    require(foundRenamed, "Rename should persist to the manifest");

    // Escape cancels a rename without touching the manifest.
    panel.beginRenameSelected();
    require(panel.handleKeyEvent(keyPress(AestraUI::NUIKeyCode::Escape)), "Escape should be consumed");
    require(!panel.isRenameActive(), "Escape should cancel rename");

    // Branch: duplicate + activate, source take untouched (non-destructive).
    const auto beforeBranch = TakeManager::loadManifest(projectPath);
    require(beforeBranch.ok, "Manifest should load before branch");
    const auto* branchSource = beforeBranch.findTake("main");
    require(branchSource != nullptr, "Branch source should exist");
    const std::string sourceSnapshotPath = TakeManager::resolveSnapshotPath(projectPath, *branchSource);
    const std::string sourceContentsBefore = readFileContents(sourceSnapshotPath);
    require(!sourceContentsBefore.empty(), "Branch source snapshot should exist");

    panel.setOnBranchTake([&](const std::string& takeId) {
        auto dup = TakeManager::duplicateTake(projectPath, takeId, "Main Branch");
        if (!dup.ok) return false;
        return TakeManager::setActiveTake(projectPath, dup.take.id).ok;
    });
    for (int i = 0; i < panel.getTakeCount(); ++i) {
        if (panel.getTakeAt(i).id == "main") panel.selectTake(i);
    }
    require(panel.requestBranchSelected(), "Branch action should succeed");

    const auto afterBranch = TakeManager::loadManifest(projectPath);
    require(afterBranch.ok, "Manifest should load after branch");
    require(afterBranch.takes.size() == beforeBranch.takes.size() + 1, "Branch should add a take");
    const auto* branch = afterBranch.activeTake();
    require(branch != nullptr && branch->name == "Main Branch", "Branch should be active");
    require(branch->parentId == "main", "Branch should record its source as parent");
    require(readFileContents(sourceSnapshotPath) == sourceContentsBefore,
            "Branching must not modify the source take snapshot");
    require(readFileContents(TakeManager::resolveSnapshotPath(projectPath, *branch)) == sourceContentsBefore,
            "Branch snapshot should start as a copy of its source");

    // A failing action surfaces a status message instead of silently dropping.
    panel.setOnDuplicateTake([](const std::string&) { return false; });
    panel.selectTake(0);
    require(!panel.requestDuplicateSelected(), "Failing duplicate should report false");
    require(!panel.getStatusText().empty(), "Failing action should surface a status message");

    // Hidden panel ignores input.
    panel.setVisible(false);
    require(!panel.onMouseEvent(leftPress(160.0f, rowCenterY)), "Hidden takes panel must not consume events");
    require(!panel.handleKeyEvent(keyPress(AestraUI::NUIKeyCode::Enter)), "Hidden takes panel must not consume keys");

    std::cout << "[PASS] Takes panel actions\n";
}

} // namespace

int main() {
    testHistoryPanel();
    testTakesPanel();
    std::cout << "[PASS] TakesHistoryPanelTest\n";
    return 0;
}
