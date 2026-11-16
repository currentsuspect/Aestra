// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Roles: what a sound or track IS in the song (Connected Understanding, owner brief 16 Nov 2026). The rules under
// test are the brief's: the user's choice wins; a recognised name gives a role the user can override; everything
// else is Unknown, and Aestra never guesses. Header-only, links nothing.

#include "Models/MusicalRole.h"

#include <cstdio>
#include <string>

using namespace Aestra::Audio;

namespace {
int g_failures = 0;
void check(bool c, const std::string& m) { if (!c) { std::printf("[FAIL] %s\n", m.c_str()); ++g_failures; } }

void expectRole(const char* name, RoleCategory category, RoleSub sub) {
    const MusicalRole r = roleFromName(name);
    check(r.category == category && r.sub == sub && r.source == (category == RoleCategory::Unknown ? RoleSource::None : RoleSource::Name),
          std::string("\"") + name + "\" reads as " + roleLabel({category, sub, RoleSource::Name}) + " (got " + (r.known() ? roleLabel(r) : "unknown") + ")");
}
} // namespace

int main() {
    // The brief's own examples.
    expectRole("Kick 01", RoleCategory::Drums, RoleSub::Kick);
    expectRole("Vocal Lead", RoleCategory::Vocals, RoleSub::Lead);
    expectRole("Vocal Double", RoleCategory::Vocals, RoleSub::Double);
    expectRole("Drum Fill", RoleCategory::Drums, RoleSub::None);
    expectRole("Bass Layer", RoleCategory::Bass, RoleSub::None);
    expectRole("Piano Chords", RoleCategory::Keys, RoleSub::None);
    expectRole("Guitar Solo", RoleCategory::Guitar, RoleSub::None);
    expectRole("Snare", RoleCategory::Drums, RoleSub::Snare);
    expectRole("Shaker", RoleCategory::Percussion, RoleSub::Shaker);
    expectRole("Lead Vocal", RoleCategory::Vocals, RoleSub::Lead);
    expectRole("Backing Vocal", RoleCategory::Vocals, RoleSub::Backing);

    // Real names from the owner's projects: "hh - huge * umbra", "clap - stuff * umbra", "synth - fiasco * umbra".
    expectRole("hh - huge * umbra", RoleCategory::Drums, RoleSub::HiHat);
    expectRole("clap - stuff * umbra", RoleCategory::Drums, RoleSub::Clap);
    expectRole("synth - fiasco * umbra", RoleCategory::Synth, RoleSub::None);

    // Spelling: case, camelCase, separators, numbers stuck to the word.
    expectRole("KICK", RoleCategory::Drums, RoleSub::Kick);
    expectRole("kick01", RoleCategory::Drums, RoleSub::Kick);
    expectRole("HiHat", RoleCategory::Drums, RoleSub::HiHat);
    expectRole("hi-hat open", RoleCategory::Drums, RoleSub::HiHat);
    expectRole("808 Sub 01", RoleCategory::Bass, RoleSub::None);
    expectRole("Rhodes_2", RoleCategory::Keys, RoleSub::None);

    // Compounds are read as what they mean, not as their parts.
    expectRole("Bass Drum", RoleCategory::Drums, RoleSub::Kick);
    expectRole("Bass Guitar", RoleCategory::Bass, RoleSub::None);

    // Not enough to go on, or two different things: Unknown, never a guess.
    expectRole("Audio 17", RoleCategory::Unknown, RoleSub::None);
    expectRole("Track 3", RoleCategory::Unknown, RoleSub::None);
    expectRole("Channel 4", RoleCategory::Unknown, RoleSub::None);
    expectRole("Insert 1", RoleCategory::Unknown, RoleSub::None);
    expectRole("", RoleCategory::Unknown, RoleSub::None);
    expectRole("Pad", RoleCategory::Unknown, RoleSub::None);        // a drum pad, or a synth pad
    expectRole("Sub", RoleCategory::Unknown, RoleSub::None);
    expectRole("Lead", RoleCategory::Unknown, RoleSub::None);       // a modifier on its own names nothing
    expectRole("bass kick", RoleCategory::Unknown, RoleSub::None);  // two categories
    expectRole("kick vocal", RoleCategory::Unknown, RoleSub::None);
    expectRole("Skick", RoleCategory::Unknown, RoleSub::None);      // a word that merely contains one is not it

    // Two sub-roles in one category are the category, nothing finer.
    expectRole("kick snare", RoleCategory::Drums, RoleSub::None);
    // A vocal modifier next to something that is not a vocal means nothing.
    expectRole("Synth Lead", RoleCategory::Synth, RoleSub::None);

    // Precedence: the user's choice outranks any name, and a name never overwrites it.
    const MusicalRole chosen = userRole(RoleCategory::Percussion, RoleSub::Shaker);
    check(chosen.source == RoleSource::User, "the user's choice is confirmed");
    check(roleAfterNaming(chosen, "Kick 01") == chosen, "renaming a user-chosen thing \"Kick 01\" does not change what the user said");
    const MusicalRole fromName = roleFromName("Kick 01");
    check(roleAfterNaming(fromName, "Vocal Lead").category == RoleCategory::Vocals, "a name-given role follows the name when it changes");
    check(!roleAfterNaming(fromName, "Audio 17").known(), "and goes when the new name says nothing: it does not outlive its name");
    check(!userRole(RoleCategory::Unknown).known(), "an unknown choice is no choice");
    check(userRole(RoleCategory::Vocals, RoleSub::Kick).sub == RoleSub::None, "a sub that does not fit its category is dropped");

    // Stable ids round-trip (they are what a saved project says).
    for (const auto& e : roleCategoryTable()) check(roleCategoryFromId(roleCategoryId(e.category)) == e.category, std::string("category id round-trips: ") + e.id);
    for (const auto& e : roleSubTable()) check(roleSubFromId(roleSubId(e.sub)) == e.sub, std::string("sub id round-trips: ") + e.id);
    check(roleCategoryFromId("nonsense") == RoleCategory::Unknown, "an id from a newer file is Unknown, not an error");
    check(roleLabel({RoleCategory::Drums, RoleSub::Kick, RoleSource::User}) == "Drums \xc2\xb7 Kick", "the label reads Drums · Kick");

    if (g_failures) { std::printf("%d check(s) failed\n", g_failures); return 1; }
    std::printf("MusicalRoleTest: all checks passed\n");
    return 0;
}
