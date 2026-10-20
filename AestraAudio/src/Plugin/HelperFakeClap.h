// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#ifdef AESTRA_ENABLE_TEST_HOOKS
#ifndef _WIN32

// Test-only: the fake CLAP plugin the sandbox helper can be told to "load" (the #244 note-conversion tests). It lives in
// its own header so the helper's main file stays small; nothing here ships in a release build.

#include "ClapAbiMirror.h"
#include "Plugin/ClapNoteConversion.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace AestraHelper {
// A fake CLAP plugin used by the #244 headless tests. It advertises a
// configurable note-input dialect and records every event actually delivered
// through process.in_events, so a test can drive the REAL ClapModule::process
// conversion boundary and assert exactly what dialect/payload the plugin received.
namespace FakeClap {

using namespace AestraHelper::ClapAbi;
namespace ClapNote = Aestra::Audio::ClapNote;

struct Recorded {
    uint16_t type{0}; // kClapEventNoteOn / kClapEventNoteOff / kClapEventMidi
    uint32_t time{0};
    int16_t portIndex{0};
    int16_t channel{0};
    int16_t key{0};
    double velocity{0.0};
    uint8_t midi[3]{0, 0, 0};
};

struct State {
    bool exposeNotePorts{true};
    uint32_t supportedDialects{ClapNote::kDialectMidi};
    uint32_t preferredDialect{0};
    std::vector<Recorded> events;
};
inline State g_state;

inline uint32_t notePortsCount(const ClapPlugin*, bool isInput) { return isInput ? 1u : 0u; }

inline bool notePortsGet(const ClapPlugin*, uint32_t index, bool isInput, ClapNotePortInfo* info) {
    if (!isInput || index != 0 || !info) {
        return false;
    }
    *info = {};
    info->id = 0;
    info->supportedDialects = g_state.supportedDialects;
    info->preferredDialect = g_state.preferredDialect;
    std::snprintf(info->name, sizeof(info->name), "%s", "fake-note-in");
    return true;
}

inline ClapPluginNotePorts g_notePorts = {notePortsCount, notePortsGet};

inline bool init(const ClapPlugin*) { return true; }
inline void destroy(const ClapPlugin*) {}
inline bool activate(const ClapPlugin*, double, uint32_t, uint32_t) { return true; }
inline void deactivate(const ClapPlugin*) {}
inline bool startProcessing(const ClapPlugin*) { return true; }
inline void stopProcessing(const ClapPlugin*) {}
inline void reset(const ClapPlugin*) {}

inline ClapProcessStatus process(const ClapPlugin*, const ClapProcess* proc) {
    if (proc && proc->inEvents && proc->inEvents->size && proc->inEvents->get) {
        const uint32_t n = proc->inEvents->size(proc->inEvents);
        for (uint32_t i = 0; i < n; ++i) {
            const ClapEventHeader* h = proc->inEvents->get(proc->inEvents, i);
            if (!h) {
                continue;
            }
            Recorded rec{};
            rec.type = h->type;
            rec.time = h->time;
            if (h->type == kClapEventNoteOn || h->type == kClapEventNoteOff) {
                const auto* note = reinterpret_cast<const ClapEventNote*>(h);
                rec.portIndex = note->portIndex;
                rec.channel = note->channel;
                rec.key = note->key;
                rec.velocity = note->velocity;
            } else if (h->type == kClapEventMidi) {
                const auto* midi = reinterpret_cast<const ClapEventMidi*>(h);
                rec.portIndex = static_cast<int16_t>(midi->portIndex);
                std::memcpy(rec.midi, midi->data, 3);
            }
            g_state.events.push_back(rec);
        }
    }
    return kClapProcessError + 1; // CLAP_PROCESS_CONTINUE (non-error)
}

inline const void* getExtension(const ClapPlugin*, const char* id) {
    if (g_state.exposeNotePorts && id && std::strcmp(id, kClapExtNotePorts) == 0) {
        return &g_notePorts;
    }
    return nullptr;
}

inline void onMainThread(const ClapPlugin*) {}

inline ClapPluginDescriptor g_descriptor = {};
inline ClapPlugin g_plugin = {&g_descriptor, nullptr,        init,
                       destroy,       activate,       deactivate,
                       startProcessing, stopProcessing, reset,
                       process,       getExtension,   onMainThread};

} // namespace FakeClap
} // namespace AestraHelper

#endif // !_WIN32
#endif // AESTRA_ENABLE_TEST_HOOKS
