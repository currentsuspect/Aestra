// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// FD-27 point 4, "rows that open into step lanes": every unit gets a Velocity lane, a pitched sampler also gets
// Pitch and Length (the fields its playback reads), a lane value round-trips through the note, and the columns
// line up with the pads above.

#include "ArsenalStepLanes.h"

#include <iostream>
#include <string>

using namespace Aestra;
using Lane = ArsenalStepLanes::Lane;
using Audio::MidiNote;
using Audio::UnitType;

namespace {
int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok) ++g_failures;
}
bool near(float a, float b) { return std::abs(a - b) < 1e-3f; }
} // namespace

int main() {
    std::cout << "[1] lanes fit the unit\n";
    check(ArsenalStepLanes::lanesFor(UnitType::Sampler).size() == 3, "a sampler has Velocity, Chance, Ratchet");
    check(ArsenalStepLanes::lanesFor(UnitType::PitchedSampler).size() == 5, "a pitched sampler also gets Pitch and Length");
    check(ArsenalStepLanes::lanesFor(UnitType::Audio).empty(), "an audio unit has no step lanes");
    check(ArsenalStepLanes::heightFor(0) == 0.0f, "no lanes, no height");
    check(ArsenalStepLanes::heightFor(3) > ArsenalStepLanes::heightFor(1), "more lanes are taller");

    std::cout << "[2] a pointer position means a value, and back\n";
    check(near(ArsenalStepLanes::fromFraction(Lane::Velocity, 1.0f), 1.0f), "top of Velocity is full");
    check(ArsenalStepLanes::fromFraction(Lane::Velocity, 0.0f) >= ArsenalStepLanes::kVelMin, "bottom of Velocity is never silent");
    check(ArsenalStepLanes::fromFraction(Lane::Pitch, 0.5f) == 0.0f, "middle of Pitch is the root");
    check(ArsenalStepLanes::fromFraction(Lane::Pitch, 1.0f) == 12.0f && ArsenalStepLanes::fromFraction(Lane::Pitch, 0.0f) == -12.0f, "Pitch spans an octave each way");
    check(near(ArsenalStepLanes::toFraction(Lane::Pitch, ArsenalStepLanes::fromFraction(Lane::Pitch, 0.75f)), 0.75f), "Pitch snaps to semitones and round-trips");
    check(near(ArsenalStepLanes::fromFraction(Lane::Length, 0.0f), 0.1f) && near(ArsenalStepLanes::fromFraction(Lane::Length, 1.0f), 2.0f), "Length spans the engine's gate range");

    std::cout << "[3] values read and write the fields playback uses\n";
    MidiNote n;
    n.pitch = 60; n.velocity = 0.8f; n.gate = 1.0f;
    ArsenalStepLanes::writeValue(n, Lane::Pitch, 7.0f, 60);
    check(n.pitchOffset == 7 && n.pitch == 67, "Pitch sets the offset and the pitch together");
    check(near(ArsenalStepLanes::readValue(n, Lane::Pitch, 60), 7.0f), "and reads back as +7");
    MidiNote legacy;
    legacy.pitch = 55; legacy.pitchOffset = 0;
    check(near(ArsenalStepLanes::readValue(legacy, Lane::Pitch, 60), -5.0f), "a note stored by pitch alone reads as its offset from the root");
    ArsenalStepLanes::writeValue(n, Lane::Length, 5.0f, 60);
    check(near(n.gate, 2.0f), "Length clamps to the gate range");
    ArsenalStepLanes::writeValue(n, Lane::Velocity, 0.0f, 60);
    check(n.velocity >= ArsenalStepLanes::kVelMin, "Velocity clamps off silence");

    std::cout << "[3b] chance and ratchet\n";
    {
        check(near(ArsenalStepLanes::fromFraction(Lane::Chance, 0.25f), 0.25f), "Chance follows the pointer");
        check(ArsenalStepLanes::fromFraction(Lane::Ratchet, 0.0f) == 1.0f && ArsenalStepLanes::fromFraction(Lane::Ratchet, 1.0f) == 4.0f, "Ratchet runs 1 to 4");
        check(ArsenalStepLanes::fromFraction(Lane::Ratchet, 0.5f) == 3.0f || ArsenalStepLanes::fromFraction(Lane::Ratchet, 0.5f) == 2.0f, "Ratchet steps in whole hits");
        MidiNote r;
        ArsenalStepLanes::writeValue(r, Lane::Ratchet, 9.0f, 60);
        check(r.ratchet == 4, "Ratchet clamps to four hits");
        ArsenalStepLanes::writeValue(r, Lane::Chance, 0.4f, 60);
        check(near(r.chance, 0.4f) && near(ArsenalStepLanes::readValue(r, Lane::Chance, 60), 0.4f), "Chance writes and reads back");
        ArsenalStepLanes::writeValue(r, Lane::Chance, -3.0f, 60);
        check(r.chance == 0.0f, "Chance clamps at never");
    }

    std::cout << "[4] columns line up with the pads\n";
    // 16 steps, 20 px pads, 2 px group gap, strip at x=100.
    check(ArsenalStepLanes::stepAtX(100.0f + 5.0f, 100, 400, 20, 2, 0, 16) == 0, "first pad");
    check(ArsenalStepLanes::stepAtX(100.0f + 4 * 20 + 2 + 3, 100, 400, 20, 2, 0, 16) == 4, "first pad of the second group (past the gap)");
    check(ArsenalStepLanes::stepAtX(100.0f + 4 * 20 + 1, 100, 400, 20, 2, 0, 16) == -1, "the group gap is no step");
    check(ArsenalStepLanes::stepAtX(50.0f, 100, 400, 20, 2, 0, 16) == -1, "left of the strip is no step");
    check(ArsenalStepLanes::stepAtX(100.0f + 5.0f, 100, 400, 20, 2, 40.0f, 16) == 2, "scrolling shifts the columns");
    check(near(ArsenalStepLanes::stepCentreX(5, 100, 20, 2, 0), 100 + 5 * 20 + 1 * 2 + 10), "a step's centre sits in its pad");

    std::cout << (g_failures == 0 ? "ALL PASSED\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
