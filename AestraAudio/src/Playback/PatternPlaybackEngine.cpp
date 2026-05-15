// © 2025 Aestra Studios — All Rights Reserved.
#include "PatternPlaybackEngine.h"

#include "RealtimeThreadGuard.h"
#include "../../AestraCore/include/AestraLog.h"
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

int resolveSamplerRootMidiNote(const UnitInfo* unit) {
    if (!unit) {
        return 60;
    }
    auto sampler = std::dynamic_pointer_cast<Plugins::SamplerPlugin>(unit->plugin);
    return sampler ? sampler->getRootMidiNote() : 60;
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
                                                    double sourceStartBeat, double durationBeats) {
    if (instanceId >= 256) {
        Aestra::Log::error("[PatternPlayback] Instance ID must be < 256");
        return;
    }

    // Reset cancellation flag
    m_instanceCancelled[instanceId].store(false, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // Add to active instances
        PatternInstance inst;
        inst.patternId = pid;
        inst.startBeat = startBeat;
        inst.instanceId = instanceId;
        inst.sourceStartBeat = std::max(0.0, sourceStartBeat);
        inst.sourceEndBeat =
            durationBeats > 0.0 ? (inst.sourceStartBeat + durationBeats) : std::numeric_limits<double>::infinity();
        inst.scheduledThroughFrame = 0;

        m_activeInstances.push_back(inst);
    }

    Aestra::Log::info("[PatternPlayback] Scheduled instance " + std::to_string(instanceId) + " pattern " +
                      std::to_string(pid.value) + " at beat " + std::to_string(startBeat) +
                      " sourceStart=" + std::to_string(sourceStartBeat) +
                      " duration=" + std::to_string(durationBeats));
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
    }
}

uint16_t PatternPlaybackEngine::getChannelForUnit(UnitID unitId) const {
    auto* unit = m_unitManager->getUnit(unitId);
    if (!unit || unit->targetMixerRoute < 0) {
        return 0; // Fallback to channel 0
    }
    return static_cast<uint16_t>(std::max(0, std::min(unit->targetMixerRoute, 15)));
}

void PatternPlaybackEngine::refillWindow(uint64_t currentFrame, int sampleRate, int lookaheadSamples) {
    // RT safety: this method must NOT be called from the audio callback.
    // It acquires a mutex, allocates from scratch buffer, and calls pattern lookup.
    // See performNonRealtimeMaintenance() for the correct call site.
    if (reportRealtimeMisuse("PatternPlaybackEngine::refillWindow")) {
        return;
    }

    uint64_t windowEnd = currentFrame + lookaheadSamples;

    std::lock_guard<std::mutex> lock(m_mutex);

    // Process deferred flush from audio thread
    if (m_flushRequested.exchange(false, std::memory_order_acq_rel)) {
        for (auto& inst : m_activeInstances) {
            inst.scheduledThroughFrame = 0;
        }
        m_lastRefillFrame = 0;
    }

    if (currentFrame < m_lastRefillFrame) {
        for (auto& inst : m_activeInstances) {
            inst.scheduledThroughFrame = 0;
        }
    }
    m_lastRefillFrame = currentFrame;

    m_scratchEvents.clear();

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
        if (scheduleFromFrame >= windowEnd) {
            continue;
        }

        for (const auto& note : midi.notes) {
            const double noteStartInSource = note.startBeat;
            const double noteEndInSource = note.startBeat + note.durationBeats;
            if (noteEndInSource <= inst.sourceStartBeat || noteStartInSource >= inst.sourceEndBeat) {
                continue;
            }

            const UnitInfo* unit = m_unitManager->getUnit(note.unitId);
            const bool isPitchedSampler = unit && unit->type == UnitType::PitchedSampler;
            const int resolvedMidiNote = isPitchedSampler
                                             ? resolvePitchedSamplerMidiNote(note, resolveSamplerRootMidiNote(unit))
                                             : std::clamp(note.pitch, 0, 127);
            double noteBeat = inst.startBeat + note.startBeat;
            uint64_t noteFrame = m_clock->sampleFrameAtBeat(noteBeat, sampleRate);
            double offBeat = std::min(noteBeat + note.durationBeats, inst.startBeat + inst.sourceEndBeat);
            bool suppressNoteOff = false;

            if (isPitchedSampler) {
                const double gate = std::clamp(static_cast<double>(note.gate), 0.1, 2.0);
                offBeat = std::min(noteBeat + gate * kPitchedSamplerStepBeats, inst.startBeat + inst.sourceEndBeat);

                if (const auto* nextStep = findNextActiveStepForUnit(midi, note)) {
                    const double nextStepBeat = inst.startBeat + nextStep->startBeat;
                    if (note.slide) {
                        suppressNoteOff = true;
                    } else {
                        offBeat = std::min(offBeat, nextStepBeat);
                    }
                }
            }

            uint64_t offFrame = m_clock->sampleFrameAtBeat(offBeat, sampleRate);

            uint16_t channelIdx = getChannelForUnit(note.unitId);

            if (noteFrame >= scheduleFromFrame && noteFrame < windowEnd) {
                ScheduledEvent onEvent{};
                onEvent.sampleFrame = noteFrame;
                onEvent.instanceId = inst.instanceId;
                onEvent.unitId = note.unitId;
                onEvent.channelIdx = channelIdx;
                onEvent.statusByte = 0x90;
                onEvent.data1 = static_cast<uint8_t>(resolvedMidiNote);
                onEvent.data2 = toMidiVelocity(note.velocity);
                onEvent.priority = 1;
                m_scratchEvents.push_back(onEvent);
            } else if (noteFrame < scheduleFromFrame && offFrame > scheduleFromFrame &&
                       noteFrame >= previousScheduledThroughFrame) {
                // Playback entered while this note was already active; start it immediately at the buffer edge once.
                ScheduledEvent resumeOnEvent{};
                resumeOnEvent.sampleFrame = scheduleFromFrame;
                resumeOnEvent.instanceId = inst.instanceId;
                resumeOnEvent.unitId = note.unitId;
                resumeOnEvent.channelIdx = channelIdx;
                resumeOnEvent.statusByte = 0x90;
                resumeOnEvent.data1 = static_cast<uint8_t>(resolvedMidiNote);
                resumeOnEvent.data2 = toMidiVelocity(note.velocity);
                resumeOnEvent.priority = 1;
                m_scratchEvents.push_back(resumeOnEvent);
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
                m_scratchEvents.push_back(offEvent);
            }
        }

        inst.scheduledThroughFrame = windowEnd;
    }

    std::sort(m_scratchEvents.begin(), m_scratchEvents.end(), eventComesBefore);
    for (const auto& event : m_scratchEvents) {
        if (!m_rtQueue.push(event)) {
            m_overflowCounter.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void PatternPlaybackEngine::processAudio(uint64_t currentFrame, int bufferSize, const UnitMidiRoute* routes,
                                         size_t routeCount) noexcept {
    if (!routes || routeCount == 0 || bufferSize <= 0) {
        return;
    }

    ScheduledEvent ev;

    while (m_rtQueue.peek(ev)) {
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
                target->addEvent(static_cast<uint32_t>(offset), data, 3);
                m_processedCounter.fetch_add(1, std::memory_order_relaxed);
            }
        }

        m_rtQueue.pop();
    }
}

void PatternPlaybackEngine::flush() {
    // RT-safe: set atomic flag for deferred processing by non-RT maintenance.
    // The SPSC queue and m_activeInstances are only safely mutable from the
    // control thread, so actual drain/reset happens in refillWindow().
    m_flushRequested.store(true, std::memory_order_release);

    // Drain the RT queue lock-free from the producer side.
    // Resetting m_head to m_tail is safe because the consumer (audio thread)
    // only reads m_tail, and any events pushed after this reset are legitimate.
    m_rtQueue.forceDrain();
}

} // namespace Audio
} // namespace Aestra
