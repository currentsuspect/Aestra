// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Roles in a project: a unit takes its role from its name, the user's choice outranks it and is the only thing
// saved, an older file with no role loads as before, a copy keeps what the user said, and setting or clearing a
// role is one undo step (Connected Understanding, owner brief 16 Nov 2026).

#include "Commands/RenameCommand.h"
#include "Commands/SetRoleCommand.h"
#include "Models/TrackManager.h"
#include "Models/UnitManager.h"

#include <cstdio>
#include <string>

using namespace Aestra::Audio;

namespace {
int g_failures = 0;
void check(bool c, const std::string& m) { if (!c) { std::printf("[FAIL] %s\n", m.c_str()); ++g_failures; } }
} // namespace

int main() {
    // A unit reads its role from its name, at creation and on rename.
    {
        UnitManager units;
        const UnitID kick = units.createUnit("Kick 01", UnitType::Sampler);
        const UnitID audio = units.createUnit("Audio 17", UnitType::Sampler);
        check(units.unitRole(kick).category == RoleCategory::Drums && units.unitRole(kick).sub == RoleSub::Kick, "\"Kick 01\" is drums, kick from the start");
        check(units.unitRole(kick).source == RoleSource::Name, "and that is a name-given role, not a confirmed one");
        check(!units.unitRole(audio).known(), "\"Audio 17\" is not anything");

        units.setUnitName(audio, "Vocal Lead");
        check(units.unitRole(audio).category == RoleCategory::Vocals, "a rename gives the role the new name says");
        units.setUnitName(audio, "Audio 18");
        check(!units.unitRole(audio).known(), "and a name that says nothing takes the role back");

        // The user's choice outranks the name, and survives a rename.
        units.setUnitRole(kick, userRole(RoleCategory::Percussion, RoleSub::Shaker));
        check(units.unitRole(kick).category == RoleCategory::Percussion && units.unitRole(kick).source == RoleSource::User, "a chosen role is confirmed");
        units.setUnitName(kick, "Kick 02");
        check(units.unitRole(kick).category == RoleCategory::Percussion, "a rename does not overrule the user");
        units.setUnitRole(kick, MusicalRole{});
        check(units.unitRole(kick).category == RoleCategory::Drums && units.unitRole(kick).source == RoleSource::Name, "clearing the choice goes back to the name");
    }

    // Saved: only the user's choice. Loaded: an older file, a name, and a choice.
    {
        UnitManager source;
        const UnitID named = source.createUnit("Snare", UnitType::Sampler);
        const UnitID chosen = source.createUnit("Audio 3", UnitType::Sampler);
        source.setUnitRole(chosen, userRole(RoleCategory::Vocals, RoleSub::Backing));
        const Aestra::JSON saved = source.saveToJSON();
        const std::string text = saved.toString();
        check(text.find("\"role\"") != std::string::npos, "the user's choice is saved");

        UnitManager loaded;
        loaded.loadFromJSON(saved);
        check(loaded.unitRole(chosen).category == RoleCategory::Vocals && loaded.unitRole(chosen).sub == RoleSub::Backing &&
                  loaded.unitRole(chosen).source == RoleSource::User,
              "the choice comes back exactly");
        check(loaded.unitRole(named).category == RoleCategory::Drums && loaded.unitRole(named).source == RoleSource::Name,
              "a name-given role is read from the name again");

        // The name's role is not stored: a unit with none chosen writes no "role" at all (and an older Aestra reads the file as before).
        UnitManager plain;
        plain.createUnit("Kick 01", UnitType::Sampler);
        check(plain.saveToJSON().toString().find("\"role\"") == std::string::npos, "a role read from the name is not written");

        // A newer file with a category this build does not know loads as the name says, without an error.
        UnitManager future;
        const UnitID f = future.createUnit("Hat", UnitType::Sampler);
        Aestra::JSON doc = future.saveToJSON();
        std::string textFuture = doc.toString();
        const size_t at = textFuture.find("\"name\":\"Hat\"");
        if (at != std::string::npos) {
            textFuture.insert(at, "\"role\":{\"category\":\"theremin\"},");
            UnitManager reloaded;
            reloaded.loadFromJSON(Aestra::JSON::parse(textFuture));
            check(reloaded.unitRole(f).category == RoleCategory::Drums && reloaded.unitRole(f).source == RoleSource::Name,
                  "a category from a newer file is ignored; the name still speaks");
        }
    }

    // A copy keeps what the user said.
    {
        UnitManager units;
        const UnitID a = units.createUnit("Audio 1", UnitType::Sampler);
        units.setUnitRole(a, userRole(RoleCategory::Bass));
        const UnitID copy = units.duplicateUnit(a);
        check(units.unitRole(copy).category == RoleCategory::Bass, "a duplicate keeps the user's choice");
    }

    // Setting a role is undoable, in one step, and undo restores exactly what was there.
    {
        UnitManager units;
        const UnitID a = units.createUnit("Kick 01", UnitType::Sampler);
        SetUnitRoleCommand cmd(units, a, userRole(RoleCategory::Fx));
        cmd.execute();
        check(units.unitRole(a).category == RoleCategory::Fx, "the command sets the role");
        cmd.undo();
        check(units.unitRole(a).category == RoleCategory::Drums && units.unitRole(a).source == RoleSource::Name, "undo restores the name-given role, source included");
        cmd.redo();
        check(units.unitRole(a).category == RoleCategory::Fx, "redo sets it again");
    }

    // Tracks: the same rules, through creation and rename (and undo of the rename).
    {
        TrackManager tm;
        const auto lane = tm.getPlaylistModel().createLane("Kick 3");
        const uint64_t id = tm.createTrack(lane, "Kick 3");
        check(tm.getTrack(id)->role.category == RoleCategory::Drums, "a track named \"Kick 3\" is drums");
        const auto lane2 = tm.getPlaylistModel().createLane("Track 2");
        const uint64_t id2 = tm.createTrack(lane2, "Track 2");
        check(!tm.getTrack(id2)->role.known(), "\"Track 2\" is not anything");

        auto rename = RenameCommand::forTrack(tm, lane2, "Vocal Lead");
        rename->execute();
        check(tm.getTrack(id2)->role.category == RoleCategory::Vocals, "renaming a track \"Vocal Lead\" makes it vocals");
        rename->undo();
        check(!tm.getTrack(id2)->role.known(), "and undoing the rename takes it back");

        SetTrackRoleCommand choose(tm, id, userRole(RoleCategory::Bass));
        choose.execute();
        auto rename2 = RenameCommand::forTrack(tm, lane, "Snare");
        rename2->execute();
        check(tm.getTrack(id)->role.category == RoleCategory::Bass, "renaming a track the user categorised leaves the user's choice");
        rename2->undo();
        choose.undo();
        check(tm.getTrack(id)->role.category == RoleCategory::Drums && tm.getTrack(id)->role.source == RoleSource::Name, "undoing both returns the name-given role");
    }

    if (g_failures) { std::printf("%d check(s) failed\n", g_failures); return 1; }
    std::printf("MusicalRoleProjectTest: all checks passed\n");
    return 0;
}
