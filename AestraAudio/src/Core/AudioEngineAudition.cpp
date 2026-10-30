// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Auditioning a unit outside the transport: the piano roll's click (an eighth of a second) and the editors'
// Preview (the sample's length, and a stop). Runs on the audio thread from the command drain; bounded, no
// allocation, no locks.
#include "Core/AudioEngine.h"

#include <algorithm>

namespace Aestra {
namespace Audio {

void AudioEngine::applyAuditionUnitRt(const AudioQueueCommand& cmd) {
    // A negative note stops this unit's auditions (the Sample Editor's Preview, pressed again).
    if (cmd.value1 < 0.0f) {
        for (auto& s : m_unitAuditionStates) {
            if (s.active && s.unitId == static_cast<UnitID>(cmd.trackIndex)) {
                s.noteOffSamplesRemaining = std::min<uint32_t>(s.noteOffSamplesRemaining, 1);
            }
        }
        return;
    }
    // Claim a slot from the pool: a free one when possible, otherwise
    // the one closest to its note-off. A chord stamp sends one command
    // per pitch, so simultaneous triad tones each get their own slot.
    UnitAuditionState* slot = nullptr;
    for (auto& s : m_unitAuditionStates) {
        if (!s.active) {
            slot = &s;
            break;
        }
    }
    if (!slot) {
        slot = &m_unitAuditionStates[0];
        for (auto& s : m_unitAuditionStates) {
            if (s.noteOffSamplesRemaining < slot->noteOffSamplesRemaining) {
                slot = &s;
            }
        }
    }
    slot->unitId = static_cast<UnitID>(cmd.trackIndex);
    slot->note = static_cast<uint8_t>(std::clamp(static_cast<int>(cmd.value1), 0, 127));
    slot->velocity = static_cast<uint8_t>(std::clamp(static_cast<int>(cmd.value2 * 127.0f), 1, 127));
    // A length, when given (an editor previewing a whole sample), capped at a minute; else the click's eighth.
    const uint32_t eighth = std::max<uint32_t>(1, m_sampleRate.load(std::memory_order_relaxed) / 8);
    const uint64_t minute = static_cast<uint64_t>(m_sampleRate.load(std::memory_order_relaxed)) * 60u;
    slot->noteOffSamplesRemaining =
        cmd.samplePos > 0 ? static_cast<uint32_t>(std::min<uint64_t>(cmd.samplePos, minute)) : eighth;
    slot->noteOnPending = true;
    slot->active = true;
    // Wake the master out of the post-stop Silent fast path so the
    // audition is actually rendered. After the transport stops the fade
    // settles to Silent, whose early-return drops everything before the
    // unit-audition MIDI is injected — mirrors the metronome count-in
    // recovery, and matches the pre-first-play (None) state.
    if (m_fadeState.load(std::memory_order_relaxed) == FadeState::Silent) {
        m_fadeState.store(FadeState::None, std::memory_order_relaxed);
    }
}

} // namespace Audio
} // namespace Aestra
