// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Hardware MIDI input — RtMidi-backed, feeding the live-note path.
 *
 * Opens every available MIDI input port ("all inputs" default, like most
 * DAWs) and translates channel-voice messages into live events posted through
 * the sink (AudioEngine::postHardwareMidiEvent). The RtMidi callback thread is
 * the single producer of the engine's hardware queue.
 *
 * The target unit is an atomic the UI updates (same semantics as musical
 * typing: captured per event at callback time, so note-offs follow their
 * note-ons to the unit that received them... at the queue level each event
 * simply carries the unit current at arrival).
 *
 * Threading: start()/stop()/refreshPorts() are control-thread only. The sink
 * runs on RtMidi's callback thread and must be lock-free (posting to an SPSC
 * queue qualifies). No RT-audio-thread involvement here.
 */
class MidiInputService {
public:
    /// unitId, status, data1, data2 — called on the MIDI callback thread.
    using EventSink = std::function<void(uint64_t, uint8_t, uint8_t, uint8_t)>;

    struct TranslatedEvent {
        uint8_t status{0};
        uint8_t data1{0};
        uint8_t data2{0};
    };

    MidiInputService();
    ~MidiInputService();

    MidiInputService(const MidiInputService&) = delete;
    MidiInputService& operator=(const MidiInputService&) = delete;

    /// Open all available input ports and start delivering events to the sink.
    /// Returns the number of ports opened (0 = none available or backend absent).
    size_t start(EventSink sink);
    void stop();

    /// Re-scan and open any newly connected ports (control thread).
    size_t refreshPorts();

    /// Names of currently open ports (control thread).
    std::vector<std::string> openPortNames() const;

    /// The unit live hardware input plays. 0 = no target (events dropped).
    void setTargetUnit(uint64_t unitId) { m_targetUnit.store(unitId, std::memory_order_relaxed); }
    uint64_t targetUnit() const { return m_targetUnit.load(std::memory_order_relaxed); }

    /**
     * @brief Pure translation of a raw MIDI message to a live event.
     *
     * Accepts channel-voice note messages; normalizes note-on with velocity 0
     * to note-off (common controller behavior). Returns nullopt for anything
     * the live path does not carry yet (CC, pitch bend, sysex, realtime).
     * Header-inline and side-effect free: unit-testable without hardware and
     * without linking the RtMidi-backed platform library.
     */
    static std::optional<TranslatedEvent> translateMessage(const uint8_t* bytes, size_t size) {
        if (bytes == nullptr || size < 3) {
            return std::nullopt;
        }
        const uint8_t status = bytes[0] & 0xF0;
        if (status != 0x90 && status != 0x80) {
            return std::nullopt; // only note messages ride the live path for now
        }
        const uint8_t note = bytes[1] & 0x7F;
        const uint8_t velocity = bytes[2] & 0x7F;
        uint8_t outStatus = bytes[0];
        // Note-on with velocity 0 is a note-off in disguise (very common on
        // controllers). Normalize so downstream never needs the special case.
        if (status == 0x90 && velocity == 0) {
            outStatus = static_cast<uint8_t>(0x80 | (bytes[0] & 0x0F));
        }
        return TranslatedEvent{outStatus, note, velocity};
    }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::atomic<uint64_t> m_targetUnit{0};
    EventSink m_sink;
};

} // namespace Audio
} // namespace Aestra
