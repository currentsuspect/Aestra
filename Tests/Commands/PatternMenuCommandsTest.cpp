// © 2026 Aestra Studios — All Rights Reserved.
// V82-AR1 (FD-52): Rename, Duplicate and Delete of a pattern from the Arsenal's pattern menu. Each is one undo
// step, and undo gives back exactly what was there: name, notes, swing and the pattern's own id.

#include "Commands/CommandHistory.h"
#include "Commands/PatternMenuCommands.h"
#include "Models/PatternManager.h"

#include <iostream>
#include <memory>
#include <string>

using namespace Aestra::Audio;

namespace {
int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok) ++g_failures;
}

PatternID makeVerse(PatternManager& pm) {
    MidiPayload payload;
    MidiNote n;
    n.pitch = 60;
    n.startBeat = 1.0;
    n.durationBeats = 0.5;
    payload.notes.push_back(n);
    const PatternID id = pm.createMidiPattern("Verse", 8.0, payload);
    pm.applyPatch(id, [](PatternSource& p) { p.swing = 0.4f; });
    return id;
}
size_t count(const PatternManager& pm) {
    size_t n = 0;
    pm.forEachPattern([&](const PatternSource&) { ++n; });
    return n;
}
} // namespace

int main() {
    std::cout << "[1] rename: one step, undo restores the name\n";
    {
        PatternManager pm;
        CommandHistory history;
        const PatternID id = makeVerse(pm);
        history.pushAndExecute(std::make_shared<RenamePatternCommand>(pm, id, "Chorus"));
        check(pm.getPattern(id)->name == "Chorus", "renamed");
        history.undo();
        check(pm.getPattern(id)->name == "Verse", "undo gives the old name back");
        history.redo();
        check(pm.getPattern(id)->name == "Chorus", "redo renames again");
    }

    std::cout << "[2] rename: empty, unchanged or missing does nothing\n";
    {
        PatternManager pm;
        const PatternID id = makeVerse(pm);
        RenamePatternCommand empty(pm, id, "");
        empty.execute();
        RenamePatternCommand same(pm, id, "Verse");
        same.execute();
        RenamePatternCommand missing(pm, PatternID{999}, "Nope");
        missing.execute();
        check(pm.getPattern(id)->name == "Verse" && !empty.wasApplied() && !same.wasApplied() && !missing.wasApplied(),
              "no edit, and none reports itself applied");
    }

    std::cout << "[3] duplicate: a copy with its own name, the same notes; undo removes it, redo returns the same id\n";
    {
        PatternManager pm;
        CommandHistory history;
        const PatternID id = makeVerse(pm);
        auto dup = std::make_shared<DuplicatePatternCommand>(pm, id);
        history.pushAndExecute(dup);
        const PatternID copy = dup->copyId();
        check(copy.isValid() && copy.value != id.value && count(pm) == 2, "a second pattern exists");
        check(pm.getPattern(copy)->name == "Verse copy", "named 'Verse copy'");
        check(std::get<MidiPayload>(pm.getPattern(copy)->payload).notes.size() == 1 && pm.getPattern(copy)->swing == 0.4f &&
                  pm.getPattern(copy)->lengthBeats == 8.0,
              "notes, swing and length carried over");
        check(pm.getPattern(id)->name == "Verse", "the original keeps its name");
        history.undo();
        check(count(pm) == 1 && !pm.getPattern(copy), "undo removes the copy");
        history.redo();
        check(count(pm) == 2 && pm.getPattern(copy) && pm.getPattern(copy)->name == "Verse copy", "redo brings back the same copy");
    }

    std::cout << "[4] duplicate names count on\n";
    {
        PatternManager pm;
        const PatternID id = makeVerse(pm);
        DuplicatePatternCommand a(pm, id), b(pm, id), c(pm, id);
        a.execute();
        b.execute();
        c.execute();
        check(pm.getPattern(a.copyId())->name == "Verse copy" && pm.getPattern(b.copyId())->name == "Verse copy 2" &&
                  pm.getPattern(c.copyId())->name == "Verse copy 3",
              "copy, copy 2, copy 3");
        const PatternID blank = pm.createMidiPattern("", 4.0, MidiPayload{});
        DuplicatePatternCommand d(pm, blank);
        d.execute();
        check(pm.getPattern(d.copyId())->name == "Pattern copy", "an unnamed pattern copies as 'Pattern copy'");
        DuplicatePatternCommand gone(pm, PatternID{999});
        gone.execute();
        check(!gone.copyId().isValid(), "a missing source makes no copy");
    }

    std::cout << "[5] delete: undo puts back the same pattern, same id, same notes\n";
    {
        PatternManager pm;
        CommandHistory history;
        const PatternID id = makeVerse(pm);
        const PatternID other = pm.createMidiPattern("Chorus", 4.0, MidiPayload{});
        history.pushAndExecute(std::make_shared<DeletePatternCommand>(pm, id));
        check(!pm.getPattern(id) && pm.getPattern(other) && count(pm) == 1, "gone, the other stays");
        history.undo();
        const auto* back = pm.getPattern(id);
        check(back && back->name == "Verse" && back->swing == 0.4f && back->lengthBeats == 8.0 &&
                  std::get<MidiPayload>(back->payload).notes.size() == 1,
              "undo restores name, swing, length and notes under the same id");
        history.redo();
        check(!pm.getPattern(id), "redo deletes again");
        history.undo();
        check(pm.getPattern(id) != nullptr, "and undo again restores it");
        const PatternID next = pm.createMidiPattern("New", 4.0, MidiPayload{});
        check(next.value != id.value && next.value != other.value, "a pattern made later never reuses the id");
    }

    std::cout << (g_failures == 0 ? "PatternMenuCommandsTest: all passed\n" : "PatternMenuCommandsTest: FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
