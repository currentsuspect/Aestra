// © 2025 Aestra Studios — All Rights Reserved.
#include "PatternPlaybackEngine.h"

#include "RealtimeThreadGuard.h"
#include "../../AestraCore/include/AestraLog.h"
#include "Playback/NoteExpression.h"
#include "Playback/PatternRepeat.h"
#include "Plugin/SamplerPlugin.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace Aestra {
namespace Audio {

namespace {
uint8_t toMidiVelocity(float velocity) {
    const float clamped = std::max(0.0f, velocity);
    if (clamped <= 1.0f) {
        return static_cast<uint8_t>(std::clamp<int>(static_cast<int>(std::lround(clamped * 127.0f)), 0, 127));
    }
    return static_cast<uint8_t>(std::clamp<int>(static_cast<int>(std::lround(clamped)), 0, 127));
}

// One id per note occurrence: its instance, its ON and OFF frames (distinct per loop pass; the
// off frame tells apart stacked same-pitch notes of different lengths) and its resolved pitch.
// The note-off is often scheduled in a later window than the note-on, so the id must be
// recomputable from the occurrence alone. Never 0 (0 means "no id"). An edit that moves a
// sounding note changes this id; the gate record keeps the voice's id (see refillWindow).
uint32_t noteOccurrenceId(uint32_t instanceId, uint64_t noteFrame, uint64_t offFrame, uint8_t midiNote) {
    uint64_t h = 0x9E3779B97F4A7C15ull ^ (static_cast<uint64_t>(instanceId) << 32) ^ midiNote;
    h ^= noteFrame + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h ^= offFrame + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    const uint32_t id = static_cast<uint32_t>(h ^ (h >> 32));
    return id != 0 ? id : 1u;
}

bool eventComesBefore(const ScheduledEvent& a, const ScheduledEvent& b) {
    if (a.sampleFrame != b.sampleFrame) {
        return a.sampleFrame < b.sampleFrame;
    }
    if (a.priority != b.priority) {
        return a.priority < b.priority;
    }
    if (a.instanceId != b.instanceId) {
        return a.instanceId < b.instanceId;
    }
    if (a.statusByte != b.statusByte) {
        return a.statusByte < b.statusByte;
    }
    if (a.data1 != b.data1) {
        return a.data1 < b.data1;
    }
    return a.data2 < b.data2;
}

constexpr double kPitchedSamplerStepBeats = 0.25;

const MidiNote* findNextActiveStepForUnit(const MidiPayload& midi, const MidiNote& currentNote) {
    const double nextStepBeat = currentNote.startBeat + kPitchedSamplerStepBeats;
    for (const auto& candidate : midi.notes) {
        if (candidate.unitId != currentNote.unitId || candidate.durationBeats <= 0.0) {
            continue;
        }
        if (std::abs(candidate.startBeat - nextStepBeat) <= 0.001) {
            return &candidate;
        }
    }
    return nullptr;
}

// When a scheduled note ends, and whether it ends at all. ONE definition, used by the refill
// that queues the note-off and by the live-edit reconciliation that decides whether a sounding
// note still exists: a pitched-sampler gate can outlast the note's duration, so the two
// computing it differently released notes early (or kept stale ones).
struct NoteOffTiming {
    double offBeat;
    bool suppressNoteOff; // pitched-sampler slide into the next step holds the voice
};

// How far past its own start one repeat of a pattern can still ring: its longest note, plus a beat for swing
// delay and pitched-sampler gates. Bounds the repeats a window walks (PatternRepeat::inWindow).
double repeatReachBeats(const std::vector<MidiNote>& notes, double patternLengthBeats) {
    double reach = patternLengthBeats;
    // A pitched-sampler gate holds a note up to twice its length (calculateNoteOffTiming).
    for (const auto& n : notes)
        reach = std::max(reach, n.startBeat + n.durationBeats * std::clamp(static_cast<double>(n.gate), 1.0, 2.0));
    return reach + 1.0;
}

NoteOffTiming calculateNoteOffTiming(const MidiPayload& midi, const MidiNote& note, double instanceStartBeat,
                                     double sourceEndBeatAbsolute, bool isPitchedSampler) {
    const double noteBeat = instanceStartBeat + note.startBeat;
    NoteOffTiming timing{std::min(noteBeat + note.durationBeats, sourceEndBeatAbsolute), false};
    if (isPitchedSampler) {
        // Gate is the share of the note's OWN length that sounds, as on a step sequencer: a grid step
        // (one step long) rings for gate x a step, and a note drawn at any length in the piano roll
        // rings for that length. Timing it from the step alone cut every drawn note to a blip.
        const double gate = std::clamp(static_cast<double>(note.gate), 0.1, 2.0);
        const double length = note.durationBeats > 0.0 ? note.durationBeats : kPitchedSamplerStepBeats;
        timing.offBeat = std::min(noteBeat + gate * length, sourceEndBeatAbsolute);
        if (const auto* nextStep = findNextActiveStepForUnit(midi, note)) {
            if (note.slide) {
                timing.suppressNoteOff = true;
            } else {
                timing.offBeat = std::min(timing.offBeat, instanceStartBeat + nextStep->startBeat);
            }
        }
    }
    return timing;
}

int resolvePitchedSamplerMidiNote(const MidiNote& note, int rootMidiNote) {
    if (note.pitchOffset == 0 && note.pitch > 0 && note.pitch != rootMidiNote) {
        return std::clamp(note.pitch, 0, 127);
    }
    return std::clamp(rootMidiNote + static_cast<int>(note.pitchOffset), 0, 127);
}
} // namespace

PatternPlaybackEngine::PatternPlaybackEngine(TimelineClock* clock, PatternManager* patternMgr, UnitManager* unitMgr)
    : m_clock(clock), m_patternManager(patternMgr), m_unitManager(unitMgr), m_overflowCounter(0),
      m_processedCounter(0) {
    // Initialize cancellation flags
    for (auto& flag : m_instanceCancelled) {
        flag.store(false, std::memory_order_relaxed);
    }
    m_scratchEvents.reserve(1024);
}

void PatternPlaybackEngine::schedulePatternInstance(PatternID pid, double startBeat, uint32_t instanceId,
                                                    double sourceStartBeat, double durationBeats, bool repeatPattern) {
    if (instanceId >= 256) {
        Aestra::Log::error("[PatternPlayback] Instance ID must be < 256");
        return;
    }

    // Reset cancellation flag
    m_instanceCancelled[instanceId].store(false, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        PatternInstance inst;
        inst.patternId = pid;
        inst.startBeat = startBeat;
        inst.instanceId = instanceId;
        inst.sourceStartBeat = std::max(0.0, sourceStartBeat);
        inst.sourceEndBeat =
            durationBeats > 0.0 ? (inst.sourceStartBeat + durationBeats) : std::numeric_limits<double>::infinity();
        inst.repeatPattern = repeatPattern;
        inst.scheduledThroughFrame = 0;

        // An instanceId identifies a SLOT, not an occurrence — cancellation is keyed on
        // it (m_instanceCancelled is indexed by id), so two live entries sharing an id
        // were never individually addressable anyway. This used to push_back
        // unconditionally: the Arsenal preview always re-arms slot 1, and nothing ever
        // erased entries (rewindScheduledInstances() only rewinds scheduledThroughFrame),
        // so a session accumulated dozens of overlapping copies — 31 observed in one
        // sitting — each emitting its own events into the same units. Re-arming a slot
        // must replace it.
        auto existing = std::find_if(m_activeInstances.begin(), m_activeInstances.end(),
                                     [instanceId](const PatternInstance& e) { return e.instanceId == instanceId; });
        if (existing != m_activeInstances.end()) {
            *existing = inst;
        } else {
            m_activeInstances.push_back(inst);
        }
        m_instanceCount.store(m_activeInstances.size(), std::memory_order_release);
    }

    Aestra::Log::info("[PatternPlayback] Scheduled instance " + std::to_string(instanceId) + " pattern " +
                      std::to_string(pid.value) + " at beat " + std::to_string(startBeat) +
                      " sourceStart=" + std::to_string(sourceStartBeat) +
                      " duration=" + std::to_string(durationBeats) + (repeatPattern ? " repeats" : ""));
}

void PatternPlaybackEngine::updatePatternInstance(PatternID pid, double startBeat, uint32_t instanceId,
                                                  double sourceStartBeat, double durationBeats, bool repeatPattern) {
    if (instanceId >= 256) {
        Aestra::Log::error("[PatternPlayback] Instance ID must be < 256");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto existing = std::find_if(m_activeInstances.begin(), m_activeInstances.end(),
                                     [instanceId](const PatternInstance& e) { return e.instanceId == instanceId; });
        if (existing != m_activeInstances.end()) {
            existing->patternId = pid;
            existing->startBeat = startBeat;
            existing->sourceStartBeat = std::max(0.0, sourceStartBeat);
            existing->sourceEndBeat = durationBeats > 0.0 ? (existing->sourceStartBeat + durationBeats)
                                                          : std::numeric_limits<double>::infinity();
            existing->repeatPattern = repeatPattern;
            // scheduledThroughFrame deliberately kept: see the header.
            return;
        }
    }
    schedulePatternInstance(pid, startBeat, instanceId, sourceStartBeat, durationBeats, repeatPattern);
}

void PatternPlaybackEngine::removePatternInstance(uint32_t instanceId) {
    if (instanceId >= 256)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_activeInstances.erase(
        std::remove_if(m_activeInstances.begin(), m_activeInstances.end(),
                       [instanceId](const PatternInstance& inst) { return inst.instanceId == instanceId; }),
        m_activeInstances.end());
    m_instanceCount.store(m_activeInstances.size(), std::memory_order_release);
}

void PatternPlaybackEngine::cancelPatternInstance(uint32_t instanceId) {
    if (instanceId >= 256)
        return;

    // Set cancellation flag (RT-safe)
    m_instanceCancelled[instanceId].store(true, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // Remove from active instances (non-RT)
        m_activeInstances.erase(
            std::remove_if(m_activeInstances.begin(), m_activeInstances.end(),
                           [instanceId](const PatternInstance& inst) { return inst.instanceId == instanceId; }),
            m_activeInstances.end());
        m_instanceCount.store(m_activeInstances.size(), std::memory_order_release);
    }
}

uint16_t PatternPlaybackEngine::getChannelForUnit(UnitID unitId) const {
    auto* unit = m_unitManager->getUnit(unitId);
    if (!unit || unit->targetMixerRoute < 0) {
        return 0; // Fallback to channel 0
    }
    return static_cast<uint16_t>(std::max(0, std::min(unit->targetMixerRoute, 15)));
}

void PatternPlaybackEngine::refillWindow(uint64_t currentFrame, int sampleRate, int lookaheadSamples,
                                         uint64_t loopLengthSamples) {
    ScheduleLoop loop;
    loop.length = loopLengthSamples;
    refillWindow(refillTicket(), currentFrame, sampleRate, lookaheadSamples, loop);
}

void PatternPlaybackEngine::refillWindow(uint16_t ticket, uint64_t currentFrame, int sampleRate,
                                         int lookaheadSamples, const ScheduleLoop& loop) {
    // RT safety: this method must NOT be called from the audio callback.
    // It acquires a mutex, allocates from scratch buffer, and calls pattern lookup.
    // See performNonRealtimeMaintenance() for the correct call site.
    if (reportRealtimeMisuse("PatternPlaybackEngine::refillWindow")) {
        return;
    }

    uint64_t windowEnd = currentFrame + lookaheadSamples;

    std::lock_guard<std::mutex> lock(m_mutex);

    // A rewind requested since the last refill (from the audio thread or the UI) bumped the
    // queue epoch; everything queued under the old one is already dead to processAudio().
    // The ticket predates the caller's playhead read, so a rewind after that read leaves
    // this refill stamping an epoch that is already stale: its events drop unplayed and
    // the next refill replans from the new position.
    if (ticket != m_scheduledEpoch) {
        for (auto& inst : m_activeInstances) {
            inst.scheduledThroughFrame = 0;
        }
        m_lastRefillFrame = 0;
        m_scheduledEpoch = ticket;
    }

    if (currentFrame < m_lastRefillFrame) {
        for (auto& inst : m_activeInstances) {
            inst.scheduledThroughFrame = 0;
        }
        // The frame domain jumped backwards (bounce restart, stop→locate):
        // events already queued for the old, higher frames — including the
        // next loop iteration pre-scheduled before a wrap — would sit at the
        // queue head and block everything queued below them. Discard them; the
        // window below re-queues whatever is actually due.
        invalidateQueuedEventsLocked();
    }
    m_lastRefillFrame = currentFrame;

    // Gate records whose note-off already fired are dead weight.
    m_gatedNotes.erase(std::remove_if(m_gatedNotes.begin(), m_gatedNotes.end(),
                                      [currentFrame](const GatedNote& g) { return g.offFrame < currentFrame; }),
                       m_gatedNotes.end());

    // Content edit while playing: pull each instance's frontier back to the
    // playhead so this pass re-queues from live pattern data. Deletions drop
    // out of the queue immediately and additions ahead of the playhead enter
    // at their exact frame. The frontier stays > 0, so the entry catch-up in
    // the loop below stays dormant — a full rewind here re-fired every
    // currently-sounding note (flam on each grid edit) and made fresh
    // placements sound before the playhead reached them (#823).
    // Edits announce themselves through patternContentEdited() from the Arsenal grid, but the
    // Piano Roll, Muse note verbs, undo and a dozen direct note writes do not. With the longer
    // lookahead (performNonRealtimeMaintenance) an unannounced edit would wait up to a quarter
    // second to be heard, so every active pattern is fingerprinted on each pass and any change is
    // treated exactly like an announced edit. First sight of an instance only records its print.
    bool contentEdited = m_contentEditRequested.exchange(false, std::memory_order_acq_rel);
    for (auto& inst : m_activeInstances) {
        const PatternSource* pattern = m_patternManager ? m_patternManager->getPattern(inst.patternId) : nullptr;
        const uint64_t print = pattern ? contentFingerprint(*pattern) : 0;
        if (inst.contentFingerprint != 0 && print != inst.contentFingerprint) {
            contentEdited = true;
        }
        inst.contentFingerprint = print;
    }
    if (contentEdited) {
        // A note deleted while still inside its gate leaves a hung voice: its
        // ON already fired but its queued OFF dies with the drain below, and
        // the note is gone from the pattern so the refill loop cannot re-emit
        // it. Dispatch immediate OFFs for tracked gates whose note no longer
        // exists; survivors are untouched — their OFFs are re-queued normally
        // by the loop below.
        auto findInstance = [this](uint32_t id) -> PatternInstance* {
            for (auto& inst : m_activeInstances)
                if (inst.instanceId == id)
                    return &inst;
            return nullptr;
        };

        std::vector<ScheduledEvent> deletedOffs;
        const uint64_t playheadPass = loop.passOf(currentFrame);
        const uint64_t loopBase = loop.baseOf(playheadPass);
        for (const auto& gated : m_gatedNotes)
        {
            continue;
        }
        // Gate records released here, or already past, are dropped after the pass. A surviving
        // record takes its note's CURRENT off frame: an edit that lengthened a sounding note left
        // the old one, the record was purged once that passed while the note still sounded, and a
        // later edit deleting the note then had nothing to release it with (review, #956).
        std::vector<size_t> consumedGates;
        // Each gate follows ITS note: first the note whose id is unchanged (an unedited note,
        // even among overlapping same-pitch notes); only for gates left over, a same-pitch note
        // still spanning the playhead that no other gate claimed (the note was moved or resized
        // while sounding; the gate adopts its new id and keeps its voice's). Exact matches run
        // for every gate before any fallback, so a fallback can't take another gate's note.
        struct GateMatch {
            bool found = false;
            uint64_t offFrame = 0;
            uint32_t currentId = 0;
        };
        std::vector<GateMatch> matches(m_gatedNotes.size());
        std::vector<uint32_t> claimedIds;
        const auto forEachSpanningNote = [&](const GatedNote& gated, auto&& visit) {
            PatternInstance* instPtr = findInstance(gated.instanceId);
            // A removed slot (a clip deleted, muted or moved out of the timeline while its
            // note sounds) can re-emit nothing, so its held note is released below or it hangs.
            auto* pattern = instPtr ? m_patternManager->getPattern(instPtr->patternId) : nullptr;
            if (!instPtr || !pattern || !pattern->isMidi()) return;
            const auto& patternNotes = std::get<MidiPayload>(pattern->payload).notes;
            const bool expressive = needsExpression(patternNotes, pattern->swing);
            // The repeats that can be sounding at the playhead (V82-T3); just repeat 0 when the clip doesn't repeat.
            const double repeatLength = instPtr->repeatPattern ? pattern->lengthBeats : 0.0;
            const double playheadBeat =
                m_clock->beatAtSampleFrame(currentFrame > loopBase ? currentFrame - loopBase : 0, sampleRate);
            const PatternRepeat::Range repeatsHere = PatternRepeat::inWindow(
                repeatLength, instPtr->sourceStartBeat, instPtr->sourceEndBeat, instPtr->startBeat, playheadBeat,
                playheadBeat, instPtr->repeatPattern ? repeatReachBeats(patternNotes, repeatLength) : 0.0);
            for (int64_t repeat = repeatsHere.first; repeat <= repeatsHere.last; ++repeat) {
            const double repeatStartBeat = instPtr->startBeat + static_cast<double>(repeat) * repeatLength;
            const std::vector<MidiNote> expandedNotes =
                expressive ? expandForPlayback(patternNotes, pattern->swing, pattern->id.value, instPtr->instanceId,
                                               PatternRepeat::passSeed(
                                                   playheadPass, repeat))
                           : std::vector<MidiNote>{};
            for (const auto& note : (expressive ? expandedNotes : patternNotes)) {
                if (note.unitId != gated.unitId || (repeatLength > 0.0 && note.startBeat >= repeatLength))
                    continue;
                const UnitInfo* unit = m_unitManager->getUnit(note.unitId);
                const bool isPitchedSampler = unit && unit->type == UnitType::PitchedSampler;
                int resolved = std::clamp(note.pitch, 0, 127);
                if (isPitchedSampler) {
                    const auto sampler =
                        unit ? std::dynamic_pointer_cast<Plugins::SamplerPlugin>(unit->plugin) : nullptr;
                    resolved = resolvePitchedSamplerMidiNote(note, sampler ? sampler->getRootMidiNote() : 60);
                }
                if (resolved != static_cast<int>(gated.noteNumber))
                    continue;
                const double onBeat = repeatStartBeat + note.startBeat;
                uint64_t onFrame = loopBase + m_clock->sampleFrameAtBeat(onBeat, sampleRate);
                // The note must still SPAN the playhead at its current placement. Started
                // before the playhead is not enough: a clip moved (or a note shortened) so the
                // note has already ended there gets no re-queued off, so the old voice would hang.
                const NoteOffTiming offTiming =
                    calculateNoteOffTiming(std::get<MidiPayload>(pattern->payload), note, repeatStartBeat,
                                           instPtr->startBeat + instPtr->sourceEndBeat, isPitchedSampler);
                uint64_t offFrame = loopBase + m_clock->sampleFrameAtBeat(offTiming.offBeat, sampleRate);
                if (!loop.placeInPass(playheadPass, onFrame, offFrame))
                    continue;
                if (onFrame < currentFrame && (offTiming.suppressNoteOff || offFrame > currentFrame)) {
                    const uint32_t id =
                        noteOccurrenceId(gated.instanceId, onFrame, offFrame, static_cast<uint8_t>(resolved));
                    if (visit(id, offFrame)) return;
                }
            }
            } // repeat
        };
        const auto claimed = [&](uint32_t id) {
            return std::find(claimedIds.begin(), claimedIds.end(), id) != claimedIds.end();
        };
        for (size_t i = 0; i < m_gatedNotes.size(); ++i) { // exact: the unedited note
            const auto& gated = m_gatedNotes[i];
            if (gated.offFrame <= currentFrame) continue;
            forEachSpanningNote(gated, [&](uint32_t id, uint64_t offFrame) {
                if (id != gated.currentId || claimed(id)) return false;
                matches[i] = {true, offFrame, id};
                claimedIds.push_back(id);
                return true;
            });
        }
        for (size_t i = 0; i < m_gatedNotes.size(); ++i) { // fallback: the note was edited
            const auto& gated = m_gatedNotes[i];
            if (gated.offFrame <= currentFrame || matches[i].found) continue;
            forEachSpanningNote(gated, [&](uint32_t id, uint64_t offFrame) {
                if (claimed(id)) return false;
                matches[i] = {true, offFrame, id};
                claimedIds.push_back(id);
                return true;
            });
        }
        for (size_t gateIndex = 0; gateIndex < m_gatedNotes.size(); ++gateIndex) {
            auto& gated = m_gatedNotes[gateIndex];
            if (gated.offFrame <= currentFrame) {
                consumedGates.push_back(gateIndex);
                continue;
            }
            if (matches[gateIndex].found) {
                gated.offFrame = matches[gateIndex].offFrame;
                gated.currentId = matches[gateIndex].currentId;
                continue;
            }
            ScheduledEvent off{};
            off.sampleFrame = currentFrame;
            off.instanceId = gated.instanceId;
            off.unitId = gated.unitId;
            off.channelIdx = gated.channelIdx;
            off.statusByte = 0x80;
            off.data1 = gated.noteNumber;
            off.data2 = 0;
            off.priority = 0;
            off.noteId = gated.noteId;
            deletedOffs.push_back(off);
            consumedGates.push_back(gateIndex);
        }
        // Drop consumed gate records in the same pass (indices ascend; erase from the back).
        for (auto it = consumedGates.rbegin(); it != consumedGates.rend(); ++it) {
            m_gatedNotes.erase(m_gatedNotes.begin() + static_cast<std::ptrdiff_t>(*it));
        }
        for (auto& inst : m_activeInstances) {
            inst.scheduledThroughFrame = std::min(inst.scheduledThroughFrame, currentFrame);
        }
        invalidateQueuedEventsLocked();
        for (const auto& ev : deletedOffs) {
            pushScheduledLocked(ev);
        }
    }

    m_scratchEvents.clear();

    bool anyLate = false;
    uint64_t maxLate = 0;
    uint64_t missed = 0;
    for (auto& inst : m_activeInstances) {
        // Skip cancelled instances
        if (m_instanceCancelled[inst.instanceId].load(std::memory_order_acquire)) {
            continue;
        }

        // Get pattern events
        auto* pattern = m_patternManager->getPattern(inst.patternId);
        if (!pattern) {
            continue;
        }
        if (!pattern->isMidi()) {
            continue;
        }

        auto& midi = std::get<MidiPayload>(pattern->payload);

        const uint64_t previousScheduledThroughFrame = inst.scheduledThroughFrame;
        const uint64_t scheduleFromFrame = std::max(currentFrame, previousScheduledThroughFrame);
        // The playhead passed the last queued frame: this refill came later than the lookahead
        // (a main-loop stall). Hits in [frontier, playhead) are gone; count them below.
        const bool lateRefill = previousScheduledThroughFrame > 0 && currentFrame > previousScheduledThroughFrame;
        if (lateRefill) {
            anyLate = true;
            maxLate = std::max(maxLate, currentFrame - previousScheduledThroughFrame);
        }
        if (scheduleFromFrame >= windowEnd) {
            continue;
        }

        // Looped pattern mode works in a monotonic frame domain: iterate every
        // loop pass that intersects the window so the next iteration's events
        // (downbeat included) are queued BEFORE the wrap, sample-accurately.
        uint64_t iterBegin = 0;
        uint64_t iterEnd = 0;
        if (loop.active()) {
            iterBegin = loop.passOf(scheduleFromFrame);
            iterEnd = loop.passOf(windowEnd - 1);
        }

        // A clip longer than its pattern plays it again every pattern length (V82-T3). Only the repeats that
        // can sound in this window are walked; repeat r sits r * length later, in source and on the timeline.
        // Without repeats the range is the single repeat 0 and nothing below moves.
        const double repeatLength = inst.repeatPattern ? pattern->lengthBeats : 0.0;
        const double repeatReach = inst.repeatPattern ? repeatReachBeats(midi.notes, repeatLength) : 0.0;
        for (uint64_t loopIter = iterBegin; loopIter <= iterEnd; ++loopIter) {
        const uint64_t loopBase = loop.baseOf(loopIter);
        const PatternRepeat::Range repeatsHere = PatternRepeat::inWindow(
            repeatLength, inst.sourceStartBeat, inst.sourceEndBeat, inst.startBeat,
            m_clock->beatAtSampleFrame(scheduleFromFrame > loopBase ? scheduleFromFrame - loopBase : 0, sampleRate),
            m_clock->beatAtSampleFrame(windowEnd - loopBase, sampleRate), repeatReach);
        for (int64_t repeat = repeatsHere.first; repeat <= repeatsHere.last; ++repeat) {
        const double repeatOffset = static_cast<double>(repeat) * repeatLength;
        const double repeatStartBeat = inst.startBeat + repeatOffset;
        std::vector<MidiNote> expandedNotes;
        const bool expressive = needsExpression(midi.notes, pattern->swing);
        if (expressive) expandedNotes = expandForPlayback(midi.notes, pattern->swing, pattern->id.value, inst.instanceId, PatternRepeat::passSeed(loopIter, repeat));
        for (const auto& note : (expressive ? expandedNotes : midi.notes)) {
            if (repeatLength > 0.0 && note.startBeat >= repeatLength) {
                continue; // past the pattern's end: a repeating clip wraps there, so the note never plays
            }
            const double noteStartInSource = repeatOffset + note.startBeat;
            const double noteEndInSource = noteStartInSource + note.durationBeats;
            if (noteEndInSource <= inst.sourceStartBeat || noteStartInSource >= inst.sourceEndBeat) {
                continue;
            }

            const UnitInfo* unit = m_unitManager->getUnit(note.unitId);
            const bool isPitchedSampler = unit && unit->type == UnitType::PitchedSampler;
            const auto sampler = unit ? std::dynamic_pointer_cast<Plugins::SamplerPlugin>(unit->plugin) : nullptr;
            const int resolvedMidiNote = isPitchedSampler
                                             ? resolvePitchedSamplerMidiNote(note,
                                                                             sampler ? sampler->getRootMidiNote() : 60)
                                             : std::clamp(note.pitch, 0, 127);
            double noteBeat = repeatStartBeat + note.startBeat;
            uint64_t noteFrame = loopBase + m_clock->sampleFrameAtBeat(noteBeat, sampleRate);
            // A one-shot sample may continue to its natural endpoint when no
            // note gate is present, but a Piano Roll note still owns an ADSR
            // gate. Always emit its note-off so the sampler can enter release
            // when the note ends. Pitched-sampler slides are the one explicit
            // exception because they intentionally hold the voice across the
            // next step.
            const NoteOffTiming offTiming =
                calculateNoteOffTiming(midi, note, repeatStartBeat, inst.startBeat + inst.sourceEndBeat, isPitchedSampler);
            const double offBeat = offTiming.offBeat;
            const bool suppressNoteOff = offTiming.suppressNoteOff;

            uint64_t offFrame = loopBase + m_clock->sampleFrameAtBeat(offBeat, sampleRate);
            if (!loop.placeInPass(loopIter, noteFrame, offFrame)) {
                continue; // a timeline loop pass this note does not sound in
            }
            const uint32_t noteId =
                noteOccurrenceId(inst.instanceId, noteFrame, offFrame, static_cast<uint8_t>(resolvedMidiNote));

            uint16_t channelIdx = getChannelForUnit(note.unitId);

            // Off-centre notes ship their pan as a polyphonic-aftertouch event
            // (0xA0, note, pan) at the same frame with priority 1, so it lands
            // after note-offs but before every note-on of that frame. Keyed by
            // note number, simultaneous chord notes each latch their own pan.
            // Centred notes emit nothing — the sampler defaults to centre.
            // Encoded 1..127 (64 = centre); 0 is the sampler's "no pan" slot.
            const bool hasPan = std::abs(note.pan) > 0.004f;
            const uint8_t panByte =
                static_cast<uint8_t>(std::clamp<long>(64 + std::lround(note.pan * 64.0f), 1, 127));

            if (noteFrame >= scheduleFromFrame && noteFrame < windowEnd) {
                if (hasPan) {
                    ScheduledEvent panEvent{};
                    panEvent.sampleFrame = noteFrame;
                    panEvent.instanceId = inst.instanceId;
                    panEvent.unitId = note.unitId;
                    panEvent.channelIdx = channelIdx;
                    panEvent.statusByte = 0xA0;
                    panEvent.data1 = static_cast<uint8_t>(resolvedMidiNote);
                    panEvent.data2 = panByte;
                    panEvent.priority = 1;
                    m_scratchEvents.push_back(panEvent);
                }
                ScheduledEvent onEvent{};
                onEvent.sampleFrame = noteFrame;
                onEvent.instanceId = inst.instanceId;
                onEvent.unitId = note.unitId;
                onEvent.channelIdx = channelIdx;
                onEvent.statusByte = 0x90;
                onEvent.data1 = static_cast<uint8_t>(resolvedMidiNote);
                onEvent.data2 = toMidiVelocity(note.velocity);
                onEvent.priority = 2;
                onEvent.noteId = noteId;
                m_scratchEvents.push_back(onEvent);
                if (!suppressNoteOff) {
                    m_gatedNotes.push_back({inst.instanceId, note.unitId,
                                            static_cast<uint8_t>(resolvedMidiNote), channelIdx, offFrame, noteId, noteId});
                }
            } else if (lateRefill && noteFrame >= previousScheduledThroughFrame && noteFrame < scheduleFromFrame &&
                       offFrame <= scheduleFromFrame) {
                ++missed; // fell in the gap and already over (the branch below restarts the rest): never sounded
            } else if (noteFrame < scheduleFromFrame && offFrame > scheduleFromFrame &&
                       noteFrame >= previousScheduledThroughFrame) {
                // Playback entered while this note was already active; start it immediately at the buffer edge once.
                if (hasPan) {
                    ScheduledEvent panEvent{};
                    panEvent.sampleFrame = scheduleFromFrame;
                    panEvent.instanceId = inst.instanceId;
                    panEvent.unitId = note.unitId;
                    panEvent.channelIdx = channelIdx;
                    panEvent.statusByte = 0xA0;
                    panEvent.data1 = static_cast<uint8_t>(resolvedMidiNote);
                    panEvent.data2 = panByte;
                    panEvent.priority = 1;
                    m_scratchEvents.push_back(panEvent);
                }
                ScheduledEvent resumeOnEvent{};
                resumeOnEvent.sampleFrame = scheduleFromFrame;
                resumeOnEvent.instanceId = inst.instanceId;
                resumeOnEvent.unitId = note.unitId;
                resumeOnEvent.channelIdx = channelIdx;
                resumeOnEvent.statusByte = 0x90;
                resumeOnEvent.data1 = static_cast<uint8_t>(resolvedMidiNote);
                resumeOnEvent.data2 = toMidiVelocity(note.velocity);
                resumeOnEvent.priority = 2;
                resumeOnEvent.noteId = noteId;
                m_scratchEvents.push_back(resumeOnEvent);
                if (!suppressNoteOff) {
                    m_gatedNotes.push_back({inst.instanceId, note.unitId,
                                            static_cast<uint8_t>(resolvedMidiNote), channelIdx, offFrame, noteId, noteId});
                }
            }

            if (!suppressNoteOff && offFrame >= scheduleFromFrame && offFrame < windowEnd) {
                ScheduledEvent offEvent{};
                offEvent.sampleFrame = offFrame;
                offEvent.instanceId = inst.instanceId;
                offEvent.unitId = note.unitId;
                offEvent.channelIdx = channelIdx;
                offEvent.statusByte = 0x80;
                offEvent.data1 = static_cast<uint8_t>(resolvedMidiNote);
                offEvent.data2 = 0;
                offEvent.priority = 0;
                // An edited sounding note: its gate keeps the id its voice was started with.
                offEvent.noteId = noteId;
                for (const auto& gated : m_gatedNotes) {
                    if (gated.currentId == noteId && gated.instanceId == inst.instanceId &&
                        gated.unitId == note.unitId && gated.noteNumber == static_cast<uint8_t>(resolvedMidiNote)) {
                        offEvent.noteId = gated.noteId;
                        break;
                    }
                }
                m_scratchEvents.push_back(offEvent);
            }
        }
        } // repeat
        } // loopIter

        inst.scheduledThroughFrame = windowEnd;
    }
    if (anyLate) {
        m_lateRefills.fetch_add(1, std::memory_order_relaxed);
        m_missedNoteOns.fetch_add(missed, std::memory_order_relaxed);
        uint64_t prev = m_maxLateFrames.load(std::memory_order_relaxed);
        while (maxLate > prev && !m_maxLateFrames.compare_exchange_weak(prev, maxLate, std::memory_order_relaxed)) {
        }
    }

    std::sort(m_scratchEvents.begin(), m_scratchEvents.end(), eventComesBefore);
    for (const auto& event : m_scratchEvents) {
        pushScheduledLocked(event);
    }
    // Published last: once this equals the live epoch, the queue holds this playhead's notes.
    m_plannedEpoch.store(m_scheduledEpoch, std::memory_order_release);
}

void PatternPlaybackEngine::pushScheduledLocked(ScheduledEvent event) {
    event.epoch = m_scheduledEpoch;
    if (!m_rtQueue.push(event)) {
        m_overflowCounter.fetch_add(1, std::memory_order_relaxed);
    }
}

void PatternPlaybackEngine::processAudio(uint64_t currentFrame, int bufferSize, const UnitMidiRoute* routes,
                                         size_t routeCount) noexcept {
    if (!routes || routeCount == 0 || bufferSize <= 0) {
        return;
    }

    ScheduledEvent ev;
    uint16_t epoch = m_queueEpoch.load(std::memory_order_acquire);

    while (m_rtQueue.peek(ev)) {
        // Queued before the last rewind: drop it unplayed. A mismatch re-reads the epoch
        // once, because an event published after a bump on another thread can be newer
        // than the value loaded above.
        if (ev.epoch != epoch) {
            epoch = m_queueEpoch.load(std::memory_order_acquire);
            if (ev.epoch != epoch) {
                m_rtQueue.pop();
                continue;
            }
        }

        // Check if event is in current buffer
        if (ev.sampleFrame >= currentFrame + static_cast<uint64_t>(bufferSize)) {
            break; // Event is in the future
        }

        // Check cancellation flag (RT-safe)
        if (ev.instanceId < 256 && m_instanceCancelled[ev.instanceId].load(std::memory_order_acquire)) {
            m_rtQueue.pop();
            continue;
        }

        // Calculate offset in buffer
        int offset = static_cast<int>(ev.sampleFrame - currentFrame);
        offset = std::max(0, std::min(offset, bufferSize - 1));

        // Route to Unit
        if (ev.unitId != 0) {
            MidiBuffer* target = nullptr;
            for (size_t i = 0; i < routeCount; ++i) {
                if (routes[i].unitId == ev.unitId) {
                    target = routes[i].midiBuffer;
                    break;
                }
            }

            if (target) {
                uint8_t data[3] = {ev.statusByte, ev.data1, ev.data2};
                target->addEvent(static_cast<uint32_t>(offset), data, 3, ev.noteId);
                m_processedCounter.fetch_add(1, std::memory_order_relaxed);
            }
        }

        m_rtQueue.pop();
    }
}

std::vector<PatternPlaybackEngine::InstanceSnapshot> PatternPlaybackEngine::snapshotInstances() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<InstanceSnapshot> out;
    out.reserve(m_activeInstances.size());
    for (const auto& inst : m_activeInstances) {
        out.push_back({inst.patternId, inst.instanceId, inst.startBeat, inst.sourceStartBeat, inst.sourceEndBeat});
    }
    return out;
}

void PatternPlaybackEngine::clearScheduledInstances() {
    // Control thread only — takes the scheduler mutex. processAudio() consumes m_rtQueue
    // and never reads m_activeInstances, so erasing here cannot race the audio thread.
    if (reportRealtimeMisuse("PatternPlaybackEngine::clearScheduledInstances")) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_activeInstances.clear();
        m_instanceCount.store(0, std::memory_order_release);
        m_lastRefillFrame = 0;
        // Discards every queued event; adopting the new epoch also retires any pending rewind.
        invalidateQueuedEventsLocked();
    }
    m_gatedNotes.clear();

    for (auto& flag : m_instanceCancelled) {
        flag.store(false, std::memory_order_release);
    }
}

PatternPlaybackEngine::SchedulingStats PatternPlaybackEngine::getSchedulingStats() const {
    SchedulingStats stats;
    stats.lateRefills = m_lateRefills.load(std::memory_order_relaxed);
    stats.missedNoteOns = m_missedNoteOns.load(std::memory_order_relaxed);
    stats.maxLateFrames = m_maxLateFrames.load(std::memory_order_relaxed);
    return stats;
}

uint64_t PatternPlaybackEngine::contentFingerprint(const PatternSource& pattern) {
    // FNV-1a over the fields that change what plays. Never returns 0 (0 = "not seen yet").
    uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](const void* data, size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i) {
            h ^= bytes[i];
            h *= 1099511628211ull;
        }
    };
    mix(&pattern.lengthBeats, sizeof(pattern.lengthBeats));
    mix(&pattern.swing, sizeof(pattern.swing));
    if (pattern.isMidi()) {
        for (const auto& n : std::get<MidiPayload>(pattern.payload).notes) {
            mix(&n.pitch, sizeof(n.pitch));
            mix(&n.startBeat, sizeof(n.startBeat));
            mix(&n.durationBeats, sizeof(n.durationBeats));
            mix(&n.velocity, sizeof(n.velocity));
            mix(&n.pan, sizeof(n.pan));
            mix(&n.unitId, sizeof(n.unitId));
            mix(&n.pitchOffset, sizeof(n.pitchOffset));
            mix(&n.gate, sizeof(n.gate));
            mix(&n.slide, sizeof(n.slide));
            mix(&n.chance, sizeof(n.chance));
            mix(&n.ratchet, sizeof(n.ratchet));
        }
    }
    return h == 0 ? 1 : h;
}

size_t PatternPlaybackEngine::getActiveInstanceCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeInstances.size();
}

void PatternPlaybackEngine::rewindScheduledInstances() {
    // Callable from any thread, the audio thread included. Bumping the epoch both
    // discards every queued event (processAudio drops older epochs unplayed) and asks
    // the next refillWindow() to rewind the scheduling frontiers. It used to reset the
    // queue's write index from here, which made the audio thread a second writer of an
    // index the scheduler owns: a reset racing a push resurrected events it meant to drop.
    m_queueEpoch.fetch_add(1, std::memory_order_acq_rel);
}

void PatternPlaybackEngine::patternContentEdited() {
    // Producer-side flag only — same safety contract as rewindScheduledInstances().
    // Deliberately NOT a rewind: refillWindow() handles it by pulling the
    // scheduling frontier back to the playhead with entry catch-up left dormant.
    m_contentEditRequested.store(true, std::memory_order_release);
}

} // namespace Audio
} // namespace Aestra
