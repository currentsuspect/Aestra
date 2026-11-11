// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Note scheduling glue between the audio thread and the pattern scheduler.
//
// The scheduler (PatternPlaybackEngine::refillWindow) runs on the main loop and
// queues a quarter second of notes ahead of the playhead. Two things keep that
// sample-accurate across a loop wrap and a seek:
//   - every loop, pattern or timeline, is planned in a monotonic frame domain
//     (ScheduleLoop): the next pass is queued before the wrap, so the loop top
//     never waits for a UI tick;
//   - the audio thread publishes {queue epoch, frame, loop} as one tuple each
//     block, and the refill plans from exactly that tuple, so a rewind between
//     reading the playhead and stamping events cannot revive stale notes.
#include "AudioEngine.h"
#include "Playback/PatternPlaybackEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Aestra {
namespace Audio {

void AudioEngine::refillNoteSchedule() {
    auto* scheduler = m_patternEngine.load(std::memory_order_acquire);
    if (!scheduler || !m_transportPlaying.load(std::memory_order_relaxed)) {
        return;
    }
    const uint32_t sampleRate = m_sampleRate.load(std::memory_order_relaxed);
    if (sampleRate == 0) {
        return;
    }
    const int lookahead = std::max(4096, static_cast<int>(sampleRate / 4)); // 250 ms: see refillWindow() docs

    ScheduleSnapshot snapshot;
    if (!m_schedulePublication.read(snapshot)) {
        if (m_schedulePublication.hasPublished()) {
            return; // the audio thread kept publishing over the read; try next tick
        }
        // No block has played yet: plan linearly from the cued playhead. Ticket first,
        // playhead second (see refillWindow).
        snapshot = ScheduleSnapshot{};
        snapshot.epoch = scheduler->refillTicket();
        snapshot.frame = m_globalSamplePos.load(std::memory_order_relaxed);
    }

    if (snapshot.epoch == m_lastRefillEpoch && snapshot.loop != m_lastRefillLoop) {
        // Same queue, new domain: tempo, sample rate, pattern length or the loop braces
        // changed, or a loop engaged or let go. Everything queued is timed in the old
        // domain, so rewind; the audio thread republishes under the new epoch next block.
        scheduler->rewindScheduledInstances();
        m_lastRefillLoop = snapshot.loop;
        return;
    }
    m_lastRefillLoop = snapshot.loop;
    m_lastRefillEpoch = snapshot.epoch;
    scheduler->refillWindow(snapshot.epoch, snapshot.frame, static_cast<int>(sampleRate), lookahead, snapshot.loop);
}

ScheduleLoop AudioEngine::resolveScheduleLoopRt(bool patternMode, uint64_t loopStart, uint64_t loopEnd,
                                                uint64_t position, bool transportRestart) {
    ScheduleLoop loop;
    const bool valid = loopEnd > loopStart;
    if (patternMode) {
        // The pattern is the whole world: [0, length), wrapped regardless of where playback began.
        if (valid) {
            loop.start = loopStart;
            loop.length = loopEnd - loopStart;
        }
        return loop;
    }

    uint64_t pass = m_patternLoopIteration.load(std::memory_order_relaxed);
    if (transportRestart && pass != 0) {
        // A play or seek placed the playhead absolutely; the pass count starts over.
        pass = 0;
        m_patternLoopIteration.store(0, std::memory_order_relaxed);
    }
    // Mirrors the wrap test in processBlock: the loop catches a playhead that reaches its
    // end from inside. Exactly AT the end counts when the playhead got there by advancing.
    const auto engagedIn = [&](uint64_t p) {
        if (!valid) {
            return false;
        }
        const bool beforeEnd = position < loopEnd || (position == loopEnd && (p > 0 || !transportRestart));
        return p == 0 ? beforeEnd : (position >= loopStart && beforeEnd);
    };
    if (pass != 0 && !engagedIn(pass)) {
        // The loop was switched off, or moved off the playhead, mid-pass: the pass count no
        // longer describes where the playhead is. Back to the linear domain, and drop what
        // was queued in the old one.
        pass = 0;
        m_patternLoopIteration.store(0, std::memory_order_relaxed);
        if (auto* scheduler = m_patternEngine.load(std::memory_order_relaxed)) {
            scheduler->rewindScheduledInstances();
        }
    }
    if (!engagedIn(pass)) {
        return loop; // playing linearly (started past the loop end, or no loop)
    }
    loop.start = loopStart;
    loop.length = loopEnd - loopStart;
    loop.timeline = true;
    return loop;
}

// Tempo is a timing-domain change too: every position the engine holds is in samples, so
// switching tempo without rescaling them moved the playhead to a different bar (bar 9 at
// 120 BPM became bar ~10.3 at 140). While playing, the rescale and the tempo switch land in
// the same audio block; stopped, nothing advances the playhead, so it is done here.
double AudioEngine::setTempoKeepingBeat(float bpm) {
    if (!std::isfinite(bpm) || bpm <= 0.0f) {
        return 1.0;
    }
    // While playing, the audio thread applies tempo changes in order, so each one is measured from the
    // tempo last SENT, not the one the metronome shows: it may not have taken the previous command yet.
    // Measured from getBPM(), two edits in one block both rescaled from the same base, and a quick
    // 120 -> 140 -> 120 hit the "unchanged" shortcut and left the engine at 140.
    float& requested = m_transportStart.requestedBpm;
    const bool playing = isTransportPlaying();
    if (!playing) {
        requested = 0.0f; // nothing in flight: the metronome is current
    }
    const float previous = requested > 0.0f ? requested : getBPM();
    if (!(previous > 0.0f) || previous == bpm) {
        if (!playing) {
            setBPM(bpm);
        }
        return 1.0;
    }
    const double factor = static_cast<double>(previous) / static_cast<double>(bpm);
    if (playing) {
        AudioQueueCommand cmd;
        cmd.type = AudioQueueCommandType::SetTempoKeepingBeat;
        cmd.value1 = bpm;
        cmd.value2 = previous;
        if (m_commandQueue.pushReliable(cmd)) {
            requested = bpm;
            return factor;
        }
        Aestra::Log::error("[AudioEngine] tempo change could not reach the audio thread; applying it directly");
    }
    const uint64_t position = m_globalSamplePos.load(std::memory_order_relaxed);
    m_globalSamplePos.store(static_cast<uint64_t>(std::llround(static_cast<double>(position) * factor)),
                            std::memory_order_relaxed);
    setBPM(bpm);
    if (auto* scheduler = m_patternEngine.load(std::memory_order_acquire)) {
        scheduler->rewindScheduledInstances();
    }
    return factor;
}

uint64_t AudioEngine::applyTempoKeepingBeatRt(float bpm, float previousBpm, uint64_t position) {
    if (!std::isfinite(bpm) || bpm <= 0.0f || !std::isfinite(previousBpm) || previousBpm <= 0.0f) {
        return position;
    }
    const uint64_t rescaled = static_cast<uint64_t>(
        std::llround(static_cast<double>(position) * static_cast<double>(previousBpm) / static_cast<double>(bpm)));
    m_globalSamplePos.store(rescaled, std::memory_order_relaxed);
    m_metronomeEngine.setBPM(bpm);
    m_metronomeEngine.reset(rescaled, m_sampleRate.load(std::memory_order_relaxed));
    if (auto* scheduler = m_patternEngine.load(std::memory_order_relaxed)) {
        scheduler->rewindScheduledInstances(); // queued notes are timed at the old tempo
    }
    return rescaled;
}

// Pressing play: the scheduler runs on the main loop, so without this the queue was empty when
// the first block played and notes at the play position came out a UI frame late (or never,
// if short). Instead the start is parked: the block plays silence, the playhead stays put and
// {epoch, frame, loop} is published, so the next refill plans from exactly the start. Playback
// begins on the first block after that refill lands, bounded at 50 ms so a stalled main loop
// cannot hold the transport. Only with the live app's opt-in and something scheduled; offline
// renders and manual renders never park (their timing must stay frame-exact).
bool AudioEngine::holdStartForScheduleRt(bool transportRestart, float* output, uint32_t numFrames,
                                         uint32_t channels) {
    TransportStartState& start = m_transportStart;
    auto* scheduler = m_patternEngine.load(std::memory_order_relaxed);
    const uint32_t sampleRate = m_sampleRate.load(std::memory_order_relaxed);
    const bool patternMode = m_patternPlaybackMode.load(std::memory_order_relaxed);
    const uint64_t position = m_globalSamplePos.load(std::memory_order_relaxed);
    const double samplesPerBeat =
        (static_cast<double>(sampleRate) * 60.0) / std::max(static_cast<double>(m_metronomeEngine.getBPM()), 1.0);
    const double loopStartBeat = patternMode ? 0.0 : m_loopStartBeat.load(std::memory_order_relaxed);
    const double loopEndBeat = patternMode ? m_patternLengthBeats.load(std::memory_order_relaxed)
                                           : m_loopEndBeat.load(std::memory_order_relaxed);
    const uint64_t loopStart = static_cast<uint64_t>(loopStartBeat * samplesPerBeat);
    const uint64_t loopEnd = static_cast<uint64_t>(loopEndBeat * samplesPerBeat);
    const bool loopOn = patternMode || m_loopEnabled.load(std::memory_order_relaxed);

    if (transportRestart) {
        const bool live = start.holdForSchedule.load(std::memory_order_relaxed) &&
                          !m_offlineRenderActive.load(std::memory_order_relaxed) &&
                          m_manualRenderOwner.load(std::memory_order_relaxed) == 0;
        // A play placed exactly on a timeline loop's end is linear (processBlock's wrap test),
        // and only the restart block can tell; it is not parked.
        const bool onLoopEnd = !patternMode && loopOn && position == loopEnd;
        start.holdFramesLeft = (live && !onLoopEnd && scheduler && scheduler->hasScheduledInstances() && sampleRate > 0)
                                   ? sampleRate / 20
                                   : 0;
        if (start.holdFramesLeft > 0 && !patternMode) {
            m_patternLoopIteration.store(0, std::memory_order_relaxed); // the pass count starts over
        }
    }
    if (start.holdFramesLeft == 0 || !scheduler || !m_transportPlaying.load(std::memory_order_relaxed)) {
        start.holdFramesLeft = 0;
        return false;
    }
    const uint16_t epoch = scheduler->refillTicket();
    if (scheduler->plannedEpoch() == epoch) {
        start.holdFramesLeft = 0; // the queue holds this start's notes: go
        return false;
    }

    ScheduleSnapshot snapshot;
    snapshot.epoch = epoch;
    if (loopOn && loopEnd > loopStart && (patternMode || position < loopEnd)) {
        snapshot.loop.start = loopStart;
        snapshot.loop.length = loopEnd - loopStart;
        snapshot.loop.timeline = !patternMode;
    }
    snapshot.frame = snapshot.loop.active()
                         ? m_patternLoopIteration.load(std::memory_order_relaxed) * snapshot.loop.length + position
                         : position;
    m_schedulePublication.publish(snapshot);

    if (output && numFrames > 0) {
        std::memset(output, 0, static_cast<size_t>(numFrames) * std::max<uint32_t>(1, channels) * sizeof(float));
    }
    start.holdFramesLeft = numFrames >= start.holdFramesLeft ? 0 : start.holdFramesLeft - numFrames;
    return true;
}

} // namespace Audio
} // namespace Aestra
