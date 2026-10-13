// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// SPEC-017 acceptance criteria 1 and 6 (data model): branches, kinds, step logs, notes, and the take migration.

#include "../../Source/Core/TakeManager.h"

#include "../../Source/Core/ProjectSerializer.h"
#include "../../Source/Core/VersionSteps.h"
#include "Commands/CommandHistory.h"
#include "../Support/TestTempDirectory.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

struct NamedCommand : Aestra::Audio::ICommand {
    explicit NamedCommand(std::string n) : name(std::move(n)) {}
    void execute() override {}
    void undo() override {}
    void redo() override {}
    std::string getName() const override { return name; }
    std::string name;
};

} // namespace

int main() {
    {   // The step log is the undo stack, oldest first; an unnamed command still gets a row.
        Aestra::Audio::CommandHistory history;
        history.pushAndExecute(std::make_shared<NamedCommand>("Add unit · Kick"));
        history.pushAndExecute(std::make_shared<NamedCommand>(""));
        history.pushAndExecute(std::make_shared<NamedCommand>("Set swing 18%"));
        const auto steps = Aestra::collectVersionSteps(history);
        require(steps.size() == 3, "one step per command on the undo stack");
        require(steps[0].label == "Add unit · Kick" && steps[2].label == "Set swing 18%", "oldest first");
        require(steps[1].label == "(unnamed)", "an unnamed command still gets a row");
        history.undo();
        require(Aestra::collectVersionSteps(history).size() == 2, "an undone step is not part of the version");
    }

    const Aestra::Tests::ScopedTempDirectory scope{"TakeManagerVersions"};
    const auto project = (scope.path() / "song.aes").string();
    const std::string contents = "{}";
    require(ProjectSerializer::writeAtomically(project, contents), "write project");

    // Criterion 1: save twice and branch once -> 3 versions on 2 branches, with a fork at the parent.
    auto init = TakeManager::ensureManifest(project, contents, "Main");
    require(init.ok && init.manifest.takes.size() == 1, "initial manifest");
    const std::string root = init.manifest.takes[0].id;

    std::vector<TakeManager::TakeEntry::Step> steps = {{"Move clip · Pad", "MoveClipCommand", 1000},
                                                       {"Set swing 18%", "SetSwingCommand", 2000}};
    auto v2 = TakeManager::createVersion(project, contents, "Drums in", root, "", TakeManager::TakeEntry::Kind::Saved, steps);
    require(v2.ok, "second version");
    require(v2.take.branch == "Main", "an empty branch continues the parent's branch");
    require(v2.take.steps.size() == 2, "the step log is stored");

    auto v3 = TakeManager::createVersion(project, contents, "Boom bap try", root, "Idea", TakeManager::TakeEntry::Kind::Saved, {});
    require(v3.ok && v3.take.branch == "Idea", "a named branch starts from the chosen parent");
    require(v3.take.parentId == root, "the fork sits at the parent");

    auto loaded = TakeManager::loadManifest(project);
    require(loaded.ok && loaded.takes.size() == 3, "3 versions");
    const auto branches = loaded.branches();
    require(branches.size() == 2 && branches[0] == "Main" && branches[1] == "Idea", "2 branches, Main first");
    const auto* reloaded = loaded.findTake(v2.take.id);
    require(reloaded && reloaded->steps.size() == 2 && reloaded->steps[1].label == "Set swing 18%" &&
                reloaded->steps[1].commandType == "SetSwingCommand" && reloaded->steps[1].atMs == 2000,
            "the step log survives a reload");

    // Duplicating keeps the source's branch; branching starts a named one, and either carries the steps.
    auto dup = TakeManager::duplicateTake(project, v2.take.id);
    require(dup.ok && dup.take.branch == "Main" && dup.take.steps.size() == 2, "a duplicate stays on the branch with its steps");
    auto fork = TakeManager::duplicateTake(project, v2.take.id, "", "Branch 3");
    require(fork.ok && fork.take.branch == "Branch 3" && fork.take.parentId == v2.take.id, "branching starts a named branch at the source");
    require(TakeManager::loadManifest(project).branches().size() == 3, "Main, Idea and Branch 3");

    // Autosaves are versions of their own kind.
    auto autosave = TakeManager::createVersion(project, contents, "Autosave", v2.take.id, "", TakeManager::TakeEntry::Kind::Autosave, {});
    require(autosave.ok, "autosave version");
    loaded = TakeManager::loadManifest(project);
    require(loaded.findTake(autosave.take.id)->kind == TakeManager::TakeEntry::Kind::Autosave, "autosave kind round-trips");
    require(loaded.findTake(root)->kind == TakeManager::TakeEntry::Kind::Saved, "saved kind is the default");

    // Notes and replacing a step log.
    require(TakeManager::addVersionNote(project, v2.take.id, "kick too loud").ok, "add a note");
    require(!TakeManager::addVersionNote(project, v2.take.id, "").ok, "an empty note is refused");
    require(TakeManager::setVersionSteps(project, v3.take.id, steps).ok, "replace a step log");
    loaded = TakeManager::loadManifest(project);
    require(loaded.findTake(v2.take.id)->notes.size() == 1 && loaded.findTake(v2.take.id)->notes[0].text == "kick too loud", "note stored");
    require(loaded.findTake(v3.take.id)->steps.size() == 2, "step log replaced");
    require(!TakeManager::setVersionSteps(project, "nope", steps).ok, "unknown version is refused");

    // Criterion 6: an old manifest (no branch fields): 3 takes, one nested -> same tree, branches assigned.
    const Aestra::Tests::ScopedTempDirectory oldScope{"TakeManagerVersionsOld"};
    const auto oldProject = (oldScope.path() / "old.aes").string();
    require(ProjectSerializer::writeAtomically(oldProject, contents), "write old project");
    std::filesystem::create_directories(TakeManager::getTakesDirectory(oldProject));
    {
        std::ofstream out(TakeManager::getManifestPath(oldProject));
        out << R"({"version":1,"projectPath":"x","activeTakeId":"a","takes":[
          {"id":"a","name":"Main","parentId":"","snapshotPath":"a.aes","createdAtMs":1,"updatedAtMs":1},
          {"id":"b","name":"Second","parentId":"a","snapshotPath":"b.aes","createdAtMs":2,"updatedAtMs":2},
          {"id":"c","name":"Alt idea","parentId":"a","snapshotPath":"c.aes","createdAtMs":3,"updatedAtMs":3}]})";
    }
    const auto old = TakeManager::loadManifest(oldProject);
    require(old.ok && old.takes.size() == 3, "an old manifest still opens");
    require(old.findTake("a")->branch == "Main", "a top-level take is on Main");
    require(old.findTake("b")->branch == "Main", "a first child continues its parent's branch");
    require(old.findTake("c")->branch == "Alt idea", "a later child starts a branch named after itself");
    require(old.findTake("c")->parentId == "a" && old.findTake("b")->parentId == "a", "the tree is unchanged");

    std::cout << "TakeManagerVersionsTest passed\n";
    return 0;
}
