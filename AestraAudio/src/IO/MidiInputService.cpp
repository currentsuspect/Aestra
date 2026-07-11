// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "IO/MidiInputService.h"

#include "AestraLog.h"

#if defined(AESTRA_HAS_RTMIDI)
#include "RtMidi.h"
#endif

#include <mutex>
#include <utility>

namespace Aestra {
namespace Audio {

#if defined(AESTRA_HAS_RTMIDI)

struct MidiInputService::Impl {
    struct Port {
        std::unique_ptr<RtMidiIn> in;
        std::string name;
    };
    std::vector<Port> ports;
    MidiInputService* owner{nullptr};
    // Each open port is its own RtMidiIn with its own callback thread, so with
    // multiple ports rtCallback runs concurrently. The engine's hardware queue
    // is SPSC; this mutex serializes sink invocations so the queue only ever
    // sees one producer at a time. MIDI callback threads only — the audio
    // thread never touches this lock.
    std::mutex sinkMutex;

    static void rtCallback(double /*deltaTime*/, std::vector<unsigned char>* message, void* userData) {
        auto* self = static_cast<MidiInputService*>(userData);
        if (self == nullptr || message == nullptr) {
            return;
        }
        const uint64_t unit = self->m_targetUnit.load(std::memory_order_relaxed);
        if (unit == 0 || !self->m_sink) {
            return; // no target instrument — drop silently
        }
        const auto translated = MidiInputService::translateMessage(message->data(), message->size());
        if (translated.has_value()) {
            const std::lock_guard<std::mutex> lock(self->m_impl->sinkMutex);
            self->m_sink(unit, translated->status, translated->data1, translated->data2);
        }
    }

    size_t openAllPorts(MidiInputService* service) {
        size_t opened = 0;
        try {
            RtMidiIn probe;
            const unsigned int count = probe.getPortCount();
            for (unsigned int i = 0; i < count; ++i) {
                std::string name;
                try {
                    name = probe.getPortName(i);
                } catch (const RtMidiError&) {
                    continue;
                }
                // Skip ports we already hold open.
                bool alreadyOpen = false;
                for (const auto& p : ports) {
                    if (p.name == name) {
                        alreadyOpen = true;
                        break;
                    }
                }
                if (alreadyOpen) {
                    ++opened;
                    continue;
                }
                auto in = std::make_unique<RtMidiIn>();
                try {
                    in->openPort(i, "Aestra In");
                    in->ignoreTypes(true, true, true); // sysex, timecode, active sensing
                    in->setCallback(&rtCallback, service);
                } catch (const RtMidiError& e) {
                    Log::warning("[MidiInput] Failed to open port '" + name + "': " + e.getMessage());
                    continue;
                }
                Log::info("[MidiInput] Opened MIDI input: " + name);
                ports.push_back(Port{std::move(in), name});
                ++opened;
            }
        } catch (const RtMidiError& e) {
            Log::warning(std::string("[MidiInput] MIDI backend unavailable: ") + e.getMessage());
        }
        return opened;
    }

    void closeAll() {
        for (auto& p : ports) {
            if (p.in) {
                p.in->cancelCallback();
                p.in->closePort();
            }
        }
        ports.clear();
    }
};

MidiInputService::MidiInputService() : m_impl(std::make_unique<Impl>()) {}

MidiInputService::~MidiInputService() {
    stop();
}

size_t MidiInputService::start(EventSink sink) {
    m_sink = std::move(sink);
    return m_impl->openAllPorts(this);
}

void MidiInputService::stop() {
    if (m_impl) {
        m_impl->closeAll();
    }
    m_sink = nullptr;
}

size_t MidiInputService::refreshPorts() {
    return m_impl->openAllPorts(this);
}

std::vector<std::string> MidiInputService::openPortNames() const {
    std::vector<std::string> names;
    names.reserve(m_impl->ports.size());
    for (const auto& p : m_impl->ports) {
        names.push_back(p.name);
    }
    return names;
}

#else // !AESTRA_HAS_RTMIDI — headless/core stub: API present, no backend.

struct MidiInputService::Impl {};

MidiInputService::MidiInputService() : m_impl(std::make_unique<Impl>()) {}
MidiInputService::~MidiInputService() = default;

size_t MidiInputService::start(EventSink sink) {
    m_sink = std::move(sink);
    return 0;
}

void MidiInputService::stop() {
    m_sink = nullptr;
}

size_t MidiInputService::refreshPorts() {
    return 0;
}

std::vector<std::string> MidiInputService::openPortNames() const {
    return {};
}

#endif

} // namespace Audio
} // namespace Aestra
