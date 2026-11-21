// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// ProjectKeyCommandTest — V83-KEY3 (FD-98): what the transport bar's key pill does.
//
// The pill sets the song key, steps it a semitone, and offers "Move them there" or "Just call it that".
// What must hold:
//   1. steppingTransposeBy moves the sounding key one semitone at a time, within -24..+24, and never past it;
//   2. moveToKey turns the project the shortest way (never more than 6 semitones), as withSoundingKey does;
//   3. callItKey only renames: the sounding key becomes the named one, the written notes and the transpose stay
//      exactly as they were, so nothing sounds different;
//   4. a project with no key: naming one takes the label and moves nothing;
//   5. every one is ONE undoable command that also marks the project modified, and undo puts the key back;
//   6. the units that follow the key (and so move) are listed, so the board can say which parts moved;
//   7. the pill's words: the key as HEARD ("A min"), "No key" when there is none, and the transpose ("+3").

#include "Commands/CommandHistory.h"
#include "Commands/SetProjectKeyCommand.h"
#include "Music/KeyPillText.h"
#include "Models/TrackManager.h"
#include "Models/UnitManager.h"

#include <iostream>
#include <memory>
#include <string>

using namespace Aestra::Audio;

namespace {
int g_failures = 0;
void check(bool cond, const std::string& what) {
    std::cout << "  " << (cond ? "PASS " : "FAIL ") << what << "\n";
    if (!cond)
        ++g_failures;
}
ProjectHarmony keyOf(int tonic, bool minor, int transpose = 0) {
    ProjectHarmony h;
    h.writtenTonic = tonic;
    h.minor = minor;
    h.transposeSemitones = transpose;
    return h;
}
} // namespace

int main() {
    std::cout << "[1] stepping the transpose\n";
    {
        const ProjectHarmony c = keyOf(0, false);
        check(steppedTranspose(c, +1).soundingTonic() == 1 && steppedTranspose(c, +1).transposeSemitones == 1, "+1 moves C to C#");
        check(steppedTranspose(c, -1).soundingTonic() == 11, "-1 moves C to B");
        check(steppedTranspose(keyOf(0, false, 24), +1).transposeSemitones == 24, "stops at +24");
        check(steppedTranspose(keyOf(0, false, -24), -1).transposeSemitones == -24, "stops at -24");
        check(steppedTranspose(ProjectHarmony{}, +1).transposeSemitones == 1 && !steppedTranspose(ProjectHarmony{}, +1).hasKey(),
              "with no key the song still moves, and stays keyless");
    }

    std::cout << "[2] Move them there: the shortest way\n";
    {
        const ProjectHarmony c = keyOf(0, false); // written in C
        check(movedToKey(c, 9, true).transposeSemitones == -3 && movedToKey(c, 9, true).soundingTonic() == 9 && movedToKey(c, 9, true).minor,
              "C to A minor is 3 down, not 9 up");
        check(movedToKey(c, 6, false).transposeSemitones == 6, "the tritone, the one tie, goes up");
        check(movedToKey(keyOf(0, false, 5), 0, false).transposeSemitones == 0, "back to the written key is no transpose");
    }

    std::cout << "[3] Just call it that: only the label moves\n";
    {
        const ProjectHarmony c = keyOf(0, false, 2); // written C, sounding D
        const ProjectHarmony named = calledKey(c, 7, false);
        check(named.soundingTonic() == 7, "the sounding key is now the one named (G)");
        check(named.transposeSemitones == 2, "the transpose is untouched, so nothing sounds different");
        check(((named.writtenTonic + named.transposeSemitones) % 12) == 7, "written key + transpose = the named key");
    }

    std::cout << "[4] a project with no key\n";
    {
        const ProjectHarmony none;
        const ProjectHarmony named = calledKey(none, 9, true);
        check(named.hasKey() && named.soundingTonic() == 9 && named.transposeSemitones == 0 && named.minor, "naming takes the label, moves nothing");
        check(movedToKey(none, 9, true).soundingTonic() == 9 && movedToKey(none, 9, true).transposeSemitones == 0,
              "Move on a keyless project is the same as naming it (there is nothing to move from)");
    }

    std::cout << "[5] one undoable command; undo puts the key back\n";
    {
        TrackManager tm;
        tm.getUnitManager().setHarmony(keyOf(0, false));
        tm.setModified(false);
        auto cmd = std::make_shared<SetProjectKeyCommand>(tm, movedToKey(keyOf(0, false), 9, true), "Set Key");
        tm.getCommandHistory().pushAndExecute(cmd);
        check(tm.getUnitManager().harmony().soundingTonic() == 9 && tm.getUnitManager().harmony().transposeSemitones == -3, "executed");
        check(tm.isModified(), "the project is marked modified");
        tm.getCommandHistory().undo();
        check(tm.getUnitManager().harmony() == keyOf(0, false), "undo restores the key exactly");
        tm.getCommandHistory().redo();
        check(tm.getUnitManager().harmony().soundingTonic() == 9, "redo applies it again");
        check(cmd->getName() == "Set Key", "the history says what it did");
    }

    std::cout << "[6] which parts move\n";
    {
        TrackManager tm;
        auto& units = tm.getUnitManager();
        const UnitID keys = units.createUnit("Keys", UnitType::Instrument);
        const UnitID drums = units.createUnit("Kick", UnitType::Sampler);
        const UnitID forced = units.createUnit("Pad sample", UnitType::Sampler);
        units.setUnitFollowsKey(forced, true);
        const auto moving = unitsFollowingKey(units);
        auto has = [&](UnitID id) { return std::find(moving.begin(), moving.end(), id) != moving.end(); };
        check(has(keys) && has(forced) && !has(drums), "an instrument and a sample set to follow move; a drum sampler does not");
    }

    std::cout << "[7] the pill's words\n";
    {
        check(keyName(ProjectHarmony{}) == "No key", "a keyless song says so");
        check(keyName(keyOf(9, true)) == "A min", "A minor reads \"A min\"");
        check(keyName(keyOf(0, false)) == "C maj", "C major reads \"C maj\"");
        check(keyName(keyOf(0, true, 3)) == "D# min", "the key is the one HEARD: C min moved up 3 reads D# min");
        check(keyName(keyOf(0, false, -1)) == "B maj", "a step down wraps below C");
        check(transposeText(keyOf(0, false, 0)).empty(), "no transpose, no mark");
        check(transposeText(keyOf(0, false, 3)) == "+3" && transposeText(keyOf(0, false, -2)) == "-2", "signed transpose");
    }

    std::cout << (g_failures ? "FAILED\n" : "All project-key tests passed.\n");
    return g_failures ? 1 : 0;
}
