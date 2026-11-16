// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// ArsenalUnitGroupTest (#1400, FD-96): a sample's name gives its unit a better first group, the user can move a unit
// between Synth and Drums as one undo step, and the group the user chose survives saving and reopening.

#include "Commands/SetUnitGroupCommand.h"
#include "Models/TrackManager.h"
#include "Models/UnitManager.h"

#include <cstdlib>
#include <iostream>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "[FAIL] " << message << '\n'; ++failures; }
}
} // namespace

int main() {
    using namespace Aestra::Audio;

    // The first guess from the sample's name: melodic one-shots are not drums.
    check(unitGroupForSample(UnitType::Sampler, "bell - alot \xE2\x9C\xB1 umbra") == UnitGroup::Synth, "a bell is a synth sound");
    check(unitGroupForSample(UnitType::Sampler, "Pluck - Response") == UnitGroup::Synth, "a pluck is a synth sound");
    check(unitGroupForSample(UnitType::Sampler, "lead - first") == UnitGroup::Synth, "a lead is a synth sound");
    check(unitGroupForSample(UnitType::Sampler, "Keys_Rhodes_C3") == UnitGroup::Synth, "keys are a synth sound");
    check(unitGroupForSample(UnitType::Sampler, "bass - crush") == UnitGroup::Synth, "a bass follows the key: synth");
    check(unitGroupForSample(UnitType::Sampler, "clap - stuff") == UnitGroup::Drums, "a clap stays a drum");
    check(unitGroupForSample(UnitType::Sampler, "hh - huge") == UnitGroup::Drums, "a hat stays a drum");
    check(unitGroupForSample(UnitType::Sampler, "Snare 10 Crack") == UnitGroup::Drums, "a snare stays a drum");
    check(unitGroupForSample(UnitType::Sampler, "keyboard smash fx") == UnitGroup::Synth, "words are matched whole or as a prefix");
    check(unitGroupForSample(UnitType::Sampler, "monkey snare") == UnitGroup::Drums, "'key' inside another word is not keys");
    check(unitGroupForSample(UnitType::Instrument, "anything") == UnitGroup::Synth, "an instrument is a synth");
    check(unitGroupForSample(UnitType::Audio, "bell loop") == UnitGroup::Audio, "audio stays audio");

    UnitManager manager;
    const UnitID bell = manager.createUnit("bell - alot", UnitType::Sampler);
    check(manager.getUnit(bell) && manager.getUnit(bell)->group == UnitGroup::Synth, "a new bell unit lands under Synth");
    const UnitID kick = manager.createUnit("Kick 01", UnitType::Sampler);
    check(manager.getUnit(kick) && manager.getUnit(kick)->group == UnitGroup::Drums, "a new kick unit lands under Drums");

    // The user's choice survives a save and a reopen (it used to be replaced by the type's default on load).
    manager.setUnitGroup(kick, UnitGroup::Synth);
    UnitManager restored;
    restored.loadFromJSON(manager.saveToJSON());
    check(restored.getUnit(kick) && restored.getUnit(kick)->group == UnitGroup::Synth, "a moved unit keeps its group after reopening");
    check(restored.getUnit(kick) && restored.getUnit(kick)->type == UnitType::Sampler, "moving a unit does not change its type");

    // Moving a unit is one undo step.
    TrackManager tracks;
    auto& units = tracks.getUnitManager();
    const UnitID clap = units.createUnit("clap - stuff", UnitType::Sampler);
    tracks.getCommandHistory().pushAndExecute(std::make_shared<SetUnitGroupCommand>(tracks, clap, UnitGroup::Synth));
    check(units.getUnit(clap)->group == UnitGroup::Synth, "the command moves the unit");
    tracks.getCommandHistory().undo();
    check(units.getUnit(clap)->group == UnitGroup::Drums, "one undo puts it back");
    tracks.getCommandHistory().redo();
    check(units.getUnit(clap)->group == UnitGroup::Synth, "redo moves it again");

    if (failures) { std::cerr << "[FAIL] ArsenalUnitGroupTest: " << failures << " check(s)\n"; return 1; }
    std::cout << "[PASS] ArsenalUnitGroupTest\n";
    return 0;
}
