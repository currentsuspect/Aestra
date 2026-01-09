// © 2025 Nomad Studios — All Rights Reserved.
#include "PatternPlaybackEngine.h"
#include "../../NomadCore/include/NomadLog.h"
#include <algorithm>

namespace Nomad {
namespace Audio {

PatternPlaybackEngine::PatternPlaybackEngine(TimelineClock* clock, PatternManager* patternMgr, UnitManager* unitMgr)
    : m_clock(clock)
    , m_patternManager(patternMgr)
    , m_unitManager(unitMgr)
    , m_overflowCounter(0)
    , m_processedCounter(0)
{
    // Initialize cancellation flags
    for (auto& flag : m_instanceCancelled) {
        flag.store(false, std::memory_order_relaxed);
    }
}

void PatternPlaybackEngine::schedulePatternInstance(PatternID pid, double startBeat, uint32_t instanceId) {
    if (instanceId >= 256) {
        Nomad::Log::error("[PatternPlayback] Instance ID must be < 256");
        return;
    }
    
    // Reset cancellation flag
    m_instanceCancelled[instanceId].store(false, std::memory_order_release);
    
    // Add to active instances
    PatternInstance inst;
    inst.patternId = pid;
    inst.startBeat = startBeat;
    inst.instanceId = instanceId;
    inst.nextEventIdx = 0;
    
    m_activeInstances.push_back(inst);
    
    Nomad::Log::info("[PatternPlayback] Scheduled instance " + std::to_string(instanceId) + 
                     " pattern " + std::to_string(pid.value) + " at beat " + std::to_string(startBeat));
}

void PatternPlaybackEngine::cancelPatternInstance(uint32_t instanceId) {
    if (instanceId >= 256) return;
    
    // Set cancellation flag (RT-safe)
    m_instanceCancelled[instanceId].store(true, std::memory_order_release);
    
    // Remove from active instances (non-RT)
    m_activeInstances.erase(
        std::remove_if(m_activeInstances.begin(), m_activeInstances.end(),
            [instanceId](const PatternInstance& inst) { return inst.instanceId == instanceId; }),
        m_activeInstances.end()
    );
}

uint16_t PatternPlaybackEngine::getChannelForUnit(UnitID unitId) const {
    auto* unit = m_unitManager->getUnit(unitId);
    if (!unit || unit->targetMixerRoute < 0) {
        return 0; // Fallback to channel 0
    }
    return static_cast<uint16_t>(std::max(0, std::min(unit->targetMixerRoute, 15)));
}

void PatternPlaybackEngine::refillWindow(uint64_t currentFrame, int sampleRate, int lookaheadSamples) {
    uint64_t windowEnd = currentFrame + lookaheadSamples;
    
    // Check for loop wrap (time moved backwards)
    if (currentFrame < m_lastRefillFrame) {
         // Reset all active instances to read from start
         for (auto& inst : m_activeInstances) {
             inst.nextEventIdx = 0;
         }
         // Optional: Clear queue? Generally not needed if timestamps are checked.
    }
    m_lastRefillFrame = currentFrame;
    
    // Debug log occassionally
    static int refillCounter = 0;
    bool shouldLog = (refillCounter++ % 100 == 0);

    for (auto& inst : m_activeInstances) {
        // Skip cancelled instances
        if (m_instanceCancelled[inst.instanceId].load(std::memory_order_acquire)) {
            continue;
        }
        
        // Get pattern events
        auto* pattern = m_patternManager->getPattern(inst.patternId);
        if (!pattern) {
             if (shouldLog) Nomad::Log::info("[PatternPlayback] Pattern not found: " + std::to_string(inst.patternId.value));
             continue;
        }
        if (!pattern->isMidi()) {
             if (shouldLog) Nomad::Log::info("[PatternPlayback] Pattern is not MIDI: " + std::to_string(inst.patternId.value));
             continue;
        }
        
        auto& midi = std::get<MidiPayload>(pattern->payload);
        if (shouldLog && !midi.notes.empty()) {
             Nomad::Log::info("[PatternPlayback] Processing Pattern " + std::to_string(inst.patternId.value) + 
                              " with " + std::to_string(midi.notes.size()) + " notes. NextEventIdx=" + std::to_string(inst.nextEventIdx));
        }
        
        // [FIX] Ensure notes are sorted by beat position to handle out-of-order note insertion
        // This is critical for correct playback when notes are added in reverse or random order
        if (inst.nextEventIdx == 0 && !midi.notes.empty()) {
            // Check if already sorted; if not, sort now
            bool isSorted = true;
            for (size_t i = 1; i < midi.notes.size(); ++i) {
                if (midi.notes[i].startBeat < midi.notes[i-1].startBeat) {
                    isSorted = false;
                    break;
                }
            }
            
            if (!isSorted) {
                // Make a mutable copy and sort it
                auto notesCopy = midi.notes;
                std::sort(notesCopy.begin(), notesCopy.end(), 
                    [](const MidiNote& a, const MidiNote& b) { return a.startBeat < b.startBeat; });
                
                // Replace the pattern's notes with sorted version
                const_cast<MidiPayload&>(midi).notes = std::move(notesCopy);
            }
        }
        
        // Schedule events in lookahead window
        while (inst.nextEventIdx < midi.notes.size()) {
            const auto& note = midi.notes[inst.nextEventIdx];
            
            // Convert beat to sample frame
            double noteBeat = inst.startBeat + note.startBeat;
            uint64_t noteFrame = m_clock->sampleFrameAtBeat(noteBeat, sampleRate);
            
            if (noteFrame >= windowEnd) {
                break; // Outside lookahead window
            }
            
            uint16_t channelIdx = getChannelForUnit(note.unitId);
            
            // Schedule note-on (priority = 1)
            ScheduledEvent onEvent{};
            onEvent.sampleFrame = noteFrame;
            onEvent.instanceId = inst.instanceId;
            onEvent.unitId = note.unitId; // [NEW]
            onEvent.channelIdx = channelIdx;
            onEvent.statusByte = 0x90; // Note-on
            onEvent.data1 = note.pitch;
            onEvent.data2 = note.velocity;
            onEvent.priority = 1; // Note-on comes after note-off at same sample
            
            if (!m_rtQueue.push(onEvent)) {
                m_overflowCounter.fetch_add(1, std::memory_order_relaxed);
            }
            
            // Schedule note-off (priority = 0)
            double offBeat = noteBeat + note.durationBeats;
            uint64_t offFrame = m_clock->sampleFrameAtBeat(offBeat, sampleRate);
            
            ScheduledEvent offEvent{};
            offEvent.sampleFrame = offFrame;
            offEvent.instanceId = inst.instanceId;
            offEvent.unitId = note.unitId; // [NEW]
            offEvent.channelIdx = channelIdx;
            offEvent.statusByte = 0x80; // Note-off
            offEvent.data1 = note.pitch;
            offEvent.data2 = 0;
            offEvent.priority = 0; // Note-off comes first at same sample
            
            if (!m_rtQueue.push(offEvent)) {
                m_overflowCounter.fetch_add(1, std::memory_order_relaxed);
            }
            
            inst.nextEventIdx++;
        }
    }
}

void PatternPlaybackEngine::processAudio(uint64_t currentFrame, int bufferSize, std::map<UnitID, MidiBuffer*>& unitMidiBuffers) {
    ScheduledEvent ev;
    
    while (m_rtQueue.peek(ev)) {
        // Check if event is in current buffer
        if (ev.sampleFrame >= currentFrame + static_cast<uint64_t>(bufferSize)) {
            break; // Event is in the future
        }
        
        // Check cancellation flag (RT-safe)
        if (m_instanceCancelled[ev.instanceId].load(std::memory_order_acquire)) {
            m_rtQueue.pop();
            continue; // Skip cancelled events
        }
        
        // Calculate offset in buffer
        int offset = static_cast<int>(ev.sampleFrame - currentFrame);
        offset = std::max(0, std::min(offset, bufferSize - 1));
        
        // Route to Unit
        if (ev.unitId != 0) {
            auto it = unitMidiBuffers.find(ev.unitId);
            if (it != unitMidiBuffers.end() && it->second) {
                uint8_t data[3] = { ev.statusByte, ev.data1, ev.data2 };
                it->second->addEvent(static_cast<uint32_t>(offset), data, 3);
                m_processedCounter.fetch_add(1, std::memory_order_relaxed);
            }
        }
        
        m_rtQueue.pop();
    }
}

void PatternPlaybackEngine::processAudio(uint64_t currentFrame, int bufferSize, const UnitMidiRoute* routes, size_t routeCount) noexcept {
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
                uint8_t data[3] = { ev.statusByte, ev.data1, ev.data2 };
                target->addEvent(static_cast<uint32_t>(offset), data, 3);
                m_processedCounter.fetch_add(1, std::memory_order_relaxed);
            }
        }

        m_rtQueue.pop();
    }
}

void PatternPlaybackEngine::flush() {
    ScheduledEvent ev;
    while (m_rtQueue.peek(ev)) {
        m_rtQueue.pop();
    }
}

} // namespace Audio
} // namespace Nomad
