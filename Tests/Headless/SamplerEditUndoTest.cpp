// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Undo for the Sample Editor's controls: a gesture is one step, undo and redo walk the sampler between the two
// states, an unchanged gesture records nothing, and a new gesture after an undo clears redo.

#include "Commands/CommandHistory.h"
#include "Commands/SetSamplerStateCommand.h"
#include "Models/UnitManager.h"
#include "Plugin/SamplerPlugin.h"

#include <cmath>
#include <iostream>
#include <memory>

using namespace Aestra::Audio;
using Plugins::SamplerPlugin;

namespace {
int g_failures = 0;
void check(bool ok, const char* what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok) ++g_failures;
}
bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
} // namespace

int main() {
    UnitManager units;
    const UnitID unit = units.createUnit("Hat", UnitType::Sampler);
    auto sampler = std::make_shared<SamplerPlugin>();
    units.attachPlugin(unit, "com.Aestrastudios.sampler", sampler);
    CommandHistory history;

    const SamplerEditState start = SamplerEditState::capture(*sampler);
    check(start == SamplerEditState::capture(*sampler), "capturing twice gives the same state");

    // A gesture: the editor changes the sampler live, then records one step from start to now.
    sampler->setEnvelope(0.05f, 0.3f, 0.6f, 0.8f);
    sampler->setCoarseSemitones(7.0f);
    sampler->setFineTuneCents(-25.0f);
    sampler->setSampleWindow(0.1f, 0.8f);
    sampler->setLoopMode(SamplerPlugin::LoopMode::PingPong);
    sampler->setMaxVoices(6);
    sampler->setRootMidiNote(48);
    sampler->setMonoMode(true);
    const SamplerEditState edited = SamplerEditState::capture(*sampler);
    check(edited != start, "the edit changed the state");
    int refreshes = 0;
    check(history.pushExecuted(std::make_shared<SetSamplerStateCommand>(units, unit, start, edited, [&] { ++refreshes; })),
          "the gesture is recorded as one undo step");
    check(history.getUndoStack().size() == 1, "one step on the stack");

    // Undo puts every control back.
    check(history.undo(), "undo runs");
    check(SamplerEditState::capture(*sampler) == start, "undo restores the envelope, trim, loop, pitch, voices, root and mono");
    check(near(sampler->getAttack(), start.attack) && sampler->getMaxVoices() == start.voices && !sampler->isMonoMode(), "spot-check: attack, voices, mono");
    check(refreshes == 1, "the editor is told to show the restored values");

    // Redo brings the edit back.
    check(history.redo(), "redo runs");
    check(SamplerEditState::capture(*sampler) == edited, "redo restores the edit");
    check(near(sampler->getCoarseSemitones(), 7.0f) && sampler->getLoopMode() == SamplerPlugin::LoopMode::PingPong, "spot-check: coarse, loop mode");

    // A second gesture, then two undos walk back through both.
    sampler->setEnvelope(0.01f, 0.1f, 1.0f, 0.1f);
    const SamplerEditState second = SamplerEditState::capture(*sampler);
    history.pushExecuted(std::make_shared<SetSamplerStateCommand>(units, unit, edited, second));
    history.undo();
    check(SamplerEditState::capture(*sampler) == edited, "undo steps back one gesture");
    history.undo();
    check(SamplerEditState::capture(*sampler) == start, "and then to the start");

    // A new gesture after an undo clears redo.
    sampler->setFineTuneCents(10.0f);
    history.pushExecuted(std::make_shared<SetSamplerStateCommand>(units, unit, start, SamplerEditState::capture(*sampler)));
    check(!history.redo(), "a new gesture clears redo");

    // A command for a unit that has gone does nothing, and does not crash.
    UnitManager empty;
    SetSamplerStateCommand orphan(empty, 99, start, edited);
    orphan.undo();
    orphan.redo();
    check(true, "an orphaned command is harmless");

    std::cout << (g_failures ? "FAILED\n" : "SamplerEditUndoTest passed\n");
    return g_failures ? 1 : 0;
}
