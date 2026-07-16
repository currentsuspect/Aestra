// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Models/UnitManager.h"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }
}
} // namespace

int main() {
    using namespace Aestra::Audio;

    UnitManager manager;
    const UnitID unitId = manager.createUnit("Sampler", UnitType::Sampler);
    require(manager.getUnitType(unitId) == UnitType::Sampler, "unit did not start in sampler mode");

    require(manager.setUnitType(unitId, UnitType::Instrument), "instrument transition failed");
    const UnitInfo* instrument = manager.getUnit(unitId);
    require(instrument != nullptr, "transitioned unit is missing");
    require(instrument->type == UnitType::Instrument, "instrument type was not stored");
    require(instrument->group == UnitGroup::Synth, "instrument group was not synchronized");
    require(!manager.setUnitType(unitId + 1000, UnitType::Audio), "missing unit transition unexpectedly succeeded");

    UnitManager restored;
    restored.loadFromJSON(manager.saveToJSON());
    const UnitInfo* restoredUnit = restored.getUnit(unitId);
    require(restoredUnit != nullptr, "transitioned unit did not round-trip");
    require(restoredUnit->type == UnitType::Instrument, "instrument type did not round-trip");
    require(restoredUnit->group == UnitGroup::Synth, "instrument group did not round-trip");

    std::cout << "[PASS] ArsenalUnitTypeTransitionTest\n";
    return 0;
}
