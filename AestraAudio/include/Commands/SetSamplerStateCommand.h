// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/UnitManager.h"
#include "Plugin/SamplerPlugin.h"

#include <functional>
#include <memory>
#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Everything the Sample Editor's controls set on a sampler: envelope, trim and loop, pitch, voices, play
 * modes. One value type, so a gesture is one undo step however many of them it touched.
 */
struct SamplerEditState {
    float attack{0.0f}, decay{0.0f}, sustain{1.0f}, release{0.0f};
    float coarse{0.0f}, fine{0.0f};
    float windowStart{0.0f}, windowEnd{1.0f};
    Plugins::SamplerPlugin::LoopMode loopMode{Plugins::SamplerPlugin::LoopMode::OneShot};
    int voices{4};
    int rootNote{60};
    bool mono{false}, cutSelf{false};
    Plugins::SamplerPlugin::PitchMode pitchMode{Plugins::SamplerPlugin::PitchMode::Resample};

    static SamplerEditState capture(const Plugins::SamplerPlugin& s) {
        SamplerEditState e;
        e.attack = s.getAttack(); e.decay = s.getDecay(); e.sustain = s.getSustain(); e.release = s.getRelease();
        e.coarse = s.getCoarseSemitones(); e.fine = s.getFineTuneCents();
        e.windowStart = s.getLoopStartNorm(); e.windowEnd = s.getLoopEndNorm(); e.loopMode = s.getLoopMode();
        e.voices = s.getMaxVoices(); e.rootNote = s.getRootMidiNote();
        e.mono = s.isMonoMode(); e.cutSelf = s.isCutSelfMode(); e.pitchMode = s.getPitchMode();
        return e;
    }
    void apply(Plugins::SamplerPlugin& s) const {
        s.setEnvelope(attack, decay, sustain, release);
        s.setCoarseSemitones(coarse); s.setFineTuneCents(fine);
        s.setSampleWindow(windowStart, windowEnd); s.setLoopMode(loopMode);
        s.setMaxVoices(voices); s.setRootMidiNote(rootNote);
        s.setMonoMode(mono); s.setCutSelfMode(cutSelf); s.setPitchMode(pitchMode);
    }
    bool operator==(const SamplerEditState& o) const {
        return attack == o.attack && decay == o.decay && sustain == o.sustain && release == o.release && coarse == o.coarse &&
               fine == o.fine && windowStart == o.windowStart && windowEnd == o.windowEnd && loopMode == o.loopMode &&
               voices == o.voices && rootNote == o.rootNote && mono == o.mono && cutSelf == o.cutSelf && pitchMode == o.pitchMode;
    }
    bool operator!=(const SamplerEditState& o) const { return !(*this == o); }
};

/**
 * @brief One undo step for a gesture in the Sample Editor (a slider drag, a trim, an envelope handle, a mode key).
 *
 * The gesture has already changed the sampler live, so the command is recorded with `pushExecuted`: it carries the
 * state before and after, and undo or redo writes the other one back, saves the unit's plugin state, and tells the
 * editor (if it is open on this unit) to show it.
 */
class SetSamplerStateCommand : public ICommand {
public:
    SetSamplerStateCommand(UnitManager& units, UnitID unit, SamplerEditState before, SamplerEditState after,
                           std::function<void()> onApplied = {})
        : m_units(units), m_unit(unit), m_before(before), m_after(after), m_onApplied(std::move(onApplied)) {}

    void execute() override { write(m_after); }
    void undo() override { write(m_before); }
    void redo() override { write(m_after); }

    std::string getName() const override { return "Edit Sampler"; }
    bool changesProjectState() const override { return true; }

private:
    void write(const SamplerEditState& state) {
        auto sampler = std::dynamic_pointer_cast<Plugins::SamplerPlugin>(m_units.getUnitPlugin(m_unit));
        if (!sampler) return;
        state.apply(*sampler);
        m_units.captureUnitPluginState(m_unit);
        if (m_onApplied) m_onApplied();
    }

    UnitManager& m_units;
    UnitID m_unit;
    SamplerEditState m_before, m_after;
    std::function<void()> m_onApplied;
};

} // namespace Audio
} // namespace Aestra
