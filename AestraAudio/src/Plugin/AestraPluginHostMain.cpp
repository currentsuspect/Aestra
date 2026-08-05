// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#ifdef AESTRA_HAS_VST3
#include "Plugin/VST3Host.h"
#endif

// CLAP hosting in this child is POSIX-only (dlopen). The shared note-conversion
// rules are only needed there, so keep the include off the Windows build, where
// this header is not on the include path.
#ifndef _WIN32
#include "Plugin/ClapNoteConversion.h"
#include <dlfcn.h>
#endif

namespace {

#ifndef _WIN32
// The shared CLAP note-conversion rules live in Aestra::Audio::ClapNote; this
// file is in the global namespace, so alias it for brevity (#244).
namespace ClapNote = Aestra::Audio::ClapNote;

struct ClapVersion {
    uint32_t major;
    uint32_t minor;
    uint32_t revision;
};

struct ClapPluginDescriptor {
    ClapVersion clapVersion;
    const char* id;
    const char* name;
    const char* vendor;
    const char* url;
    const char* manualUrl;
    const char* supportUrl;
    const char* version;
    const char* description;
    const char* const* features;
};

struct ClapHost;
struct ClapPlugin;
struct ClapProcess;
struct ClapInputEvents;
struct ClapOutputEvents;
struct ClapEventHeader;

using ClapProcessStatus = int32_t;
constexpr ClapProcessStatus kClapProcessError = 0;

// CLAP core constants (from clap/include/clap/)
constexpr const char* kClapPluginFactoryId = "clap.plugin-factory";
constexpr uint16_t kClapCoreEventSpaceId = 0;
constexpr uint16_t kClapEventMidi = 10;

struct ClapHost {
    ClapVersion clapVersion;
    void* hostData;
    const char* name;
    const char* vendor;
    const char* url;
    const char* version;
    const void* (*getExtension)(const ClapHost* host, const char* extensionId);
    void (*requestRestart)(const ClapHost* host);
    void (*requestProcess)(const ClapHost* host);
    void (*requestCallback)(const ClapHost* host);
};

struct ClapPluginFactory {
    uint32_t (*getPluginCount)(const ClapPluginFactory* factory);
    const ClapPluginDescriptor* (*getPluginDescriptor)(const ClapPluginFactory* factory, uint32_t index);
    const ClapPlugin* (*createPlugin)(const ClapPluginFactory* factory, const ClapHost* host, const char* pluginId);
};

struct ClapPluginEntry {
    ClapVersion clapVersion;
    bool (*init)(const char* pluginPath);
    void (*deinit)();
    const void* (*getFactory)(const char* factoryId);
};

struct ClapPlugin {
    const ClapPluginDescriptor* descriptor;
    void* pluginData;
    bool (*init)(const ClapPlugin* plugin);
    void (*destroy)(const ClapPlugin* plugin);
    bool (*activate)(const ClapPlugin* plugin, double sampleRate, uint32_t minFramesCount, uint32_t maxFramesCount);
    void (*deactivate)(const ClapPlugin* plugin);
    bool (*startProcessing)(const ClapPlugin* plugin);
    void (*stopProcessing)(const ClapPlugin* plugin);
    void (*reset)(const ClapPlugin* plugin);
    ClapProcessStatus (*process)(const ClapPlugin* plugin, const ClapProcess* process);
    const void* (*getExtension)(const ClapPlugin* plugin, const char* extensionId);
    void (*onMainThread)(const ClapPlugin* plugin);
};

struct ClapAudioBuffer {
    float** data32;
    double** data64;
    uint32_t channelCount;
    uint32_t latency;
    uint64_t constantMask;
};

struct ClapProcess {
    int64_t steadyTime;
    uint32_t framesCount;
    const void* transport;
    const ClapAudioBuffer* audioInputs;
    ClapAudioBuffer* audioOutputs;
    uint32_t audioInputsCount;
    uint32_t audioOutputsCount;
    const ClapInputEvents* inEvents;
    const ClapOutputEvents* outEvents;
};

struct ClapEventHeader {
    uint32_t size;
    uint32_t time;
    uint16_t spaceId;
    uint16_t type;
    uint32_t flags;
};

struct ClapInputEvents {
    void* ctx;
    uint32_t (*size)(const ClapInputEvents* list);
    const ClapEventHeader* (*get)(const ClapInputEvents* list, uint32_t index);
};

struct ClapOutputEvents {
    void* ctx;
    bool (*tryPush)(const ClapOutputEvents* list, const ClapEventHeader* event);
};

struct ClapEventMidi {
    ClapEventHeader header;
    uint16_t portIndex;
    uint8_t data[3];
};

// clap_event_param_value ABI (clap/events.h). Host-driven parameter changes are
// delivered to a CLAP plugin as this event through params.flush() (#238).
struct ClapEventParamValue {
    ClapEventHeader header;
    uint32_t paramId;
    void* cookie;
    int32_t noteId; // clap_event_param_value_t::note_id is int32_t (not int16_t)
    int16_t portIndex;
    int16_t channel;
    int16_t key;
    double value;
};

constexpr uint16_t kClapEventParamValue = 5;

// clap_event_note ABI (clap/events.h) — native CLAP note-on/off events (#244).
struct ClapEventNote {
    ClapEventHeader header;
    int32_t noteId;    // -1 if unspecified
    int16_t portIndex; // note input port
    int16_t channel;   // 0..15
    int16_t key;       // 0..127
    double velocity;   // 0..1
};

constexpr uint16_t kClapEventNoteOn = 0;
constexpr uint16_t kClapEventNoteOff = 1;

// clap.note-ports extension ABI (clap/ext/note-ports.h) — lets the host learn a
// plugin's note input ports and which note dialects each accepts (#244).
struct ClapNotePortInfo {
    uint32_t id;
    uint32_t supportedDialects;
    uint32_t preferredDialect;
    char name[256];
};

struct ClapPluginNotePorts {
    uint32_t (*count)(const ClapPlugin* plugin, bool isInput);
    bool (*get)(const ClapPlugin* plugin, uint32_t index, bool isInput, ClapNotePortInfo* info);
};

constexpr const char* kClapExtNotePorts = "clap.note-ports";

struct ClapOstream {
    void* ctx;
    int64_t (*write)(const ClapOstream* stream, const void* buffer, uint64_t size);
};

struct ClapIstream {
    void* ctx;
    int64_t (*read)(const ClapIstream* stream, void* buffer, uint64_t size);
};

struct ClapPluginState {
    bool (*save)(const ClapPlugin* plugin, const ClapOstream* stream);
    bool (*load)(const ClapPlugin* plugin, const ClapIstream* stream);
};

struct ClapParamInfo {
    uint32_t id;
    uint32_t flags;
    const void* cookie;
    char name[256];
    char module[256];
    double minValue;
    double maxValue;
    double defaultValue;
    char unit[32];
    uint32_t valueCount;
    int32_t stepCount;
};

constexpr uint32_t kClapParamIsAutomatable = (1u << 0);
constexpr uint32_t kClapParamIsReadOnly = (1u << 1);
constexpr uint32_t kClapParamIsBypass = (1u << 2);
constexpr uint32_t kClapParamIsHidden = (1u << 3);
constexpr uint32_t kClapParamIsAdvanced = (1u << 4);

struct ClapPluginParams {
    uint32_t (*count)(const ClapPlugin* plugin);
    bool (*getParamInfo)(const ClapPlugin* plugin, uint32_t paramIndex, ClapParamInfo* info);
    double (*getParamValue)(const ClapPlugin* plugin, uint32_t paramId);
    bool (*getParamValueById)(const ClapPlugin* plugin, const ClapParamInfo* paramInfo, double* value);
    bool (*valueToText)(const ClapPlugin* plugin, uint32_t paramId, double value, char* display, uint32_t size);
    bool (*textToValue)(const ClapPlugin* plugin, uint32_t paramId, const char* display, double* value);
    void (*flush)(const ClapPlugin* plugin, const void* inChanges, const void* outChanges);
};

constexpr const char* kClapExtState = "clap.state";
constexpr const char* kClapExtParams = "clap.params";
constexpr const char* kProbeFirstClapPluginId = "__aestra_probe_first__";

bool isClapVersionCompatible(const ClapVersion& version) {
    return version.major == 1;
}

uint32_t emptyInputEventsSize(const ClapInputEvents* list) {
    (void)list;
    return 0;
}

const ClapEventHeader* emptyInputEventsGet(const ClapInputEvents* list, uint32_t index) {
    (void)list;
    (void)index;
    return nullptr;
}

uint32_t midiInputEventsSize(const ClapInputEvents* list) {
    const auto* events = list ? static_cast<const std::vector<ClapEventMidi>*>(list->ctx) : nullptr;
    return events ? static_cast<uint32_t>(events->size()) : 0;
}

const ClapEventHeader* midiInputEventsGet(const ClapInputEvents* list, uint32_t index) {
    const auto* events = list ? static_cast<const std::vector<ClapEventMidi>*>(list->ctx) : nullptr;
    if (!events || index >= events->size()) {
        return nullptr;
    }
    return &(*events)[index].header;
}

bool dropOutputEvent(const ClapOutputEvents* list, const ClapEventHeader* event) {
    (void)list;
    (void)event;
    return true;
}

// Single clap_event_param_value input list, used to deliver a host-driven
// parameter change to a CLAP plugin via params.flush() (#238).
uint32_t singleParamEventSize(const ClapInputEvents* list) {
    return (list && list->ctx) ? 1u : 0u;
}

const ClapEventHeader* singleParamEventGet(const ClapInputEvents* list, uint32_t index) {
    if (!list || !list->ctx || index != 0) {
        return nullptr;
    }
    return &static_cast<const ClapEventParamValue*>(list->ctx)->header;
}

// Heterogeneous, order-preserving input-event list: ctx is a vector of event
// header pointers into stable backing storage. Lets note-on/off (CLAP dialect)
// and raw MIDI events (CC etc.) be delivered in their original stream order (#244).
uint32_t orderedEventsSize(const ClapInputEvents* list) {
    const auto* order =
        list ? static_cast<const std::vector<const ClapEventHeader*>*>(list->ctx) : nullptr;
    return order ? static_cast<uint32_t>(order->size()) : 0;
}

const ClapEventHeader* orderedEventsGet(const ClapInputEvents* list, uint32_t index) {
    const auto* order =
        list ? static_cast<const std::vector<const ClapEventHeader*>*>(list->ctx) : nullptr;
    if (!order || index >= order->size()) {
        return nullptr;
    }
    return (*order)[index];
}

int64_t writeStateStream(const ClapOstream* stream, const void* buffer, uint64_t size) {
    auto* out = stream ? static_cast<std::vector<uint8_t>*>(stream->ctx) : nullptr;
    if (!out || (!buffer && size != 0)) {
        return -1;
    }
    const auto* bytes = static_cast<const uint8_t*>(buffer);
    out->insert(out->end(), bytes, bytes + size);
    return static_cast<int64_t>(size);
}

struct StateReadContext {
    const std::vector<uint8_t>* data{nullptr};
    size_t offset{0};
};

int64_t readStateStream(const ClapIstream* stream, void* buffer, uint64_t size) {
    auto* ctx = stream ? static_cast<StateReadContext*>(stream->ctx) : nullptr;
    if (!ctx || !ctx->data || (!buffer && size != 0)) {
        return -1;
    }
    const size_t available = ctx->offset < ctx->data->size() ? ctx->data->size() - ctx->offset : 0;
    const size_t toRead = std::min<size_t>(available, static_cast<size_t>(size));
    if (toRead > 0) {
        std::memcpy(buffer, ctx->data->data() + ctx->offset, toRead);
        ctx->offset += toRead;
    }
    return static_cast<int64_t>(toRead);
}

const void* hostGetExtension(const ClapHost* host, const char* extensionId) {
    (void)host;
    (void)extensionId;
    return nullptr;
}

void hostRequestRestart(const ClapHost* host) {
    (void)host;
}

void hostRequestProcess(const ClapHost* host) {
    (void)host;
}

void hostRequestCallback(const ClapHost* host) {
    (void)host;
}

#ifdef AESTRA_ENABLE_TEST_HOOKS
// A fake CLAP plugin used by the #244 headless tests. It advertises a
// configurable note-input dialect and records every event actually delivered
// through process.in_events, so a test can drive the REAL ClapModule::process
// conversion boundary and assert exactly what dialect/payload the plugin received.
namespace fakeclap {

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
State g_state;

uint32_t notePortsCount(const ClapPlugin*, bool isInput) { return isInput ? 1u : 0u; }

bool notePortsGet(const ClapPlugin*, uint32_t index, bool isInput, ClapNotePortInfo* info) {
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

ClapPluginNotePorts g_notePorts = {notePortsCount, notePortsGet};

bool init(const ClapPlugin*) { return true; }
void destroy(const ClapPlugin*) {}
bool activate(const ClapPlugin*, double, uint32_t, uint32_t) { return true; }
void deactivate(const ClapPlugin*) {}
bool startProcessing(const ClapPlugin*) { return true; }
void stopProcessing(const ClapPlugin*) {}
void reset(const ClapPlugin*) {}

ClapProcessStatus process(const ClapPlugin*, const ClapProcess* proc) {
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

const void* getExtension(const ClapPlugin*, const char* id) {
    if (g_state.exposeNotePorts && id && std::strcmp(id, kClapExtNotePorts) == 0) {
        return &g_notePorts;
    }
    return nullptr;
}

void onMainThread(const ClapPlugin*) {}

ClapPluginDescriptor g_descriptor = {};
ClapPlugin g_plugin = {&g_descriptor, nullptr,        init,
                       destroy,       activate,       deactivate,
                       startProcessing, stopProcessing, reset,
                       process,       getExtension,   onMainThread};

} // namespace fakeclap
#endif // AESTRA_ENABLE_TEST_HOOKS

struct ClapModule {
    void* library = nullptr;
    const ClapPluginEntry* entry = nullptr;
    const ClapPlugin* plugin = nullptr;
    ClapHost host = {ClapVersion{1, 1, 0},   nullptr,
                     "AestraPluginHost",     "Aestra Studios",
                     "https://Aestra.audio", "1.0.0",
                     hostGetExtension,       hostRequestRestart,
                     hostRequestProcess,     hostRequestCallback};
    bool active = false;
    uint32_t maxBlockSize = 0;
    std::vector<float> inputStorage[2];
    std::vector<float> outputStorage[2];
    std::vector<ClapEventMidi> midiEvents;
    float* inputPlanes[2] = {nullptr, nullptr};
    float* outputPlanes[2] = {nullptr, nullptr};
    ClapInputEvents emptyInputEvents = {nullptr, emptyInputEventsSize, emptyInputEventsGet};
    ClapOutputEvents outputEvents = {nullptr, dropOutputEvent};
    const ClapPluginParams* paramsExt = nullptr;
    const ClapPluginNotePorts* notePortsExt = nullptr;
    // Note-delivery decision for input port 0, computed once at load (#244).
    //  - noteDialect: kDialectClap / kDialectMidi, or 0 meaning "no mutually
    //    supported dialect" → deliver no notes.
    //  - rawMidiAllowed: may we send a raw CLAP_EVENT_MIDI on this port? True only
    //    when the port advertises the MIDI dialect (or under the legacy fallback).
    //    Non-note MIDI (CC etc.) on a CLAP-only port is dropped, never emitted as a
    //    MIDI dialect the port did not advertise.
    //  - noteDialectFromLegacyFallback: the plugin exposes no clap.note-ports
    //    extension. Per the CLAP contract that means "no note ports"; Aestra
    //    nonetheless sends raw MIDI to avoid regressing plugins that worked before
    //    note-ports negotiation existed. This is a named legacy compatibility
    //    fallback, NOT a standards-compliant negotiation result.
    uint32_t noteDialect = ClapNote::kDialectMidi;
    bool rawMidiAllowed = true;
    bool noteDialectFromLegacyFallback = true;
    // One capacity governs all note-event storage: the midi/note backing vectors,
    // the heterogeneous event-order view, and the per-block accept cap. The
    // backing vectors are reserved to this once (initialize), so the header
    // pointers recorded in noteEventOrder stay valid for the whole process() call.
    static constexpr size_t kMaxNoteEventsPerBlock = 1024;
    std::vector<ClapEventNote> noteEvents;
    // Heterogeneous in-order view over midiEvents/noteEvents for process.in_events.
    std::vector<const ClapEventHeader*> noteEventOrder;

    ~ClapModule() { close(); }

    ClapModule() = default;
    ClapModule(const ClapModule&) = delete;
    ClapModule& operator=(const ClapModule&) = delete;

    void close() {
        deactivate();
        paramsExt = nullptr;
        notePortsExt = nullptr;
        noteDialect = ClapNote::kDialectMidi;
        rawMidiAllowed = true;
        noteDialectFromLegacyFallback = true;
        if (plugin && plugin->destroy) {
            plugin->destroy(plugin);
        }
        plugin = nullptr;
        if (entry && entry->deinit) {
            entry->deinit();
        }
        entry = nullptr;
        if (library) {
            dlclose(library);
            library = nullptr;
        }
        maxBlockSize = 0;
    }

    bool load(const std::string& path, const std::string& pluginId, std::string& error) {
        close();
        library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!library) {
            error = "clap-dlopen-failed";
            return false;
        }

        entry = reinterpret_cast<const ClapPluginEntry*>(dlsym(library, "clap_entry"));
        if (!entry || !isClapVersionCompatible(entry->clapVersion) || !entry->init || !entry->getFactory) {
            error = "invalid-clap-entry";
            close();
            return false;
        }

        if (!entry->init(path.c_str())) {
            error = "clap-init-failed";
            close();
            return false;
        }

        const auto* factory = static_cast<const ClapPluginFactory*>(entry->getFactory(kClapPluginFactoryId));
        if (!factory || !factory->getPluginCount || !factory->getPluginDescriptor) {
            error = "missing-clap-factory";
            close();
            return false;
        }

        const uint32_t count = factory->getPluginCount(factory);
        if (count == 0) {
            error = "empty-clap-factory";
            close();
            return false;
        }

        const ClapPluginDescriptor* selected = nullptr;
        if (pluginId == kProbeFirstClapPluginId) {
            selected = factory->getPluginDescriptor(factory, 0);
            if (!selected || !selected->id || selected->id[0] == '\0') {
                error = "invalid-clap-descriptor";
                close();
                return false;
            }
        } else {
            for (uint32_t index = 0; index < count; ++index) {
                const ClapPluginDescriptor* descriptor = factory->getPluginDescriptor(factory, index);
                if (descriptor && descriptor->id && pluginId == descriptor->id) {
                    selected = descriptor;
                    break;
                }
            }
            if (!selected) {
                error = "clap-plugin-id-not-found";
                close();
                return false;
            }
        }

        if (!factory->createPlugin) {
            error = "missing-clap-create-plugin";
            close();
            return false;
        }
        plugin = factory->createPlugin(factory, &host, selected->id);
        if (!plugin || !plugin->init || !plugin->destroy || !plugin->activate || !plugin->deactivate ||
            !plugin->startProcessing || !plugin->stopProcessing || !plugin->process) {
            error = "invalid-clap-plugin";
            close();
            return false;
        }
        if (!plugin->init(plugin)) {
            error = "clap-plugin-init-failed";
            close();
            return false;
        }
        if (plugin->getExtension) {
            paramsExt = static_cast<const ClapPluginParams*>(plugin->getExtension(plugin, kClapExtParams));
            notePortsExt =
                static_cast<const ClapPluginNotePorts*>(plugin->getExtension(plugin, kClapExtNotePorts));
        }
        resolveNoteDialect();
        return true;
    }

    // Decide, once (on the main thread, while deactivated — never on the RT path),
    // how to deliver notes on input port 0. Advertised note-ports drive real
    // negotiation; a missing extension is the named legacy compatibility fallback
    // (Aestra sends raw MIDI as it always has, which is NOT the CLAP-compliant
    // result — absence of the extension means "no note ports") (#244). Assumes
    // plugin and notePortsExt are already resolved.
    void resolveNoteDialect() {
        const bool hasNotePort = plugin && notePortsExt && notePortsExt->count && notePortsExt->get &&
                                 notePortsExt->count(plugin, /*isInput=*/true) > 0;
        if (hasNotePort) {
            noteDialectFromLegacyFallback = false;
            ClapNotePortInfo info{};
            if (notePortsExt->get(plugin, 0, /*isInput=*/true, &info)) {
                noteDialect = ClapNote::selectDialect(info.supportedDialects, info.preferredDialect);
                rawMidiAllowed = (info.supportedDialects & ClapNote::kDialectMidi) != 0;
                if (noteDialect == 0) {
                    std::cerr << "[AestraPluginHost] note port 0 advertises no host-supported "
                                 "dialect; delivering no notes\n";
                }
            } else {
                // Extension present but the port could not be read — deliver nothing
                // rather than guess a dialect the plugin did not advertise.
                noteDialect = 0;
                rawMidiAllowed = false;
            }
        } else {
            noteDialectFromLegacyFallback = true;
            noteDialect = ClapNote::kDialectMidi;
            rawMidiAllowed = true;
        }
    }

#ifdef AESTRA_ENABLE_TEST_HOOKS
    // Wire the fake CLAP plugin in place of a dlopen'd module and run the SAME
    // note-ports resolution the real path uses, so tests exercise the production
    // conversion boundary (#244). The fake's note-ports configuration is set on
    // fakeclap::g_state before calling this.
    void loadFakeForTest() {
        close();
        plugin = &fakeclap::g_plugin;
        paramsExt = nullptr;
        notePortsExt = plugin->getExtension
                           ? static_cast<const ClapPluginNotePorts*>(
                                 plugin->getExtension(plugin, kClapExtNotePorts))
                           : nullptr;
        resolveNoteDialect();
    }
#endif

    bool initialize(double sampleRate, uint32_t blockSize, std::string& error) {
        if (!plugin || sampleRate <= 0.0 || blockSize == 0 || blockSize > 65536) {
            error = "invalid-clap-init";
            return false;
        }
        maxBlockSize = blockSize;
        for (auto& storage : inputStorage) {
            storage.assign(maxBlockSize, 0.0f);
        }
        for (auto& storage : outputStorage) {
            storage.assign(maxBlockSize, 0.0f);
        }
        midiEvents.reserve(kMaxNoteEventsPerBlock);
        noteEvents.reserve(kMaxNoteEventsPerBlock);
        noteEventOrder.reserve(kMaxNoteEventsPerBlock);
        inputPlanes[0] = inputStorage[0].data();
        inputPlanes[1] = inputStorage[1].data();
        outputPlanes[0] = outputStorage[0].data();
        outputPlanes[1] = outputStorage[1].data();
        if (!plugin->activate(plugin, sampleRate, 1, maxBlockSize)) {
            error = "clap-activate-failed";
            return false;
        }
        active = true;
        if (!plugin->startProcessing(plugin)) {
            active = false;
            plugin->deactivate(plugin);
            error = "clap-start-processing-failed";
            return false;
        }
        return true;
    }

    void deactivate() {
        if (active && plugin) {
            if (plugin->stopProcessing) {
                plugin->stopProcessing(plugin);
            }
            if (plugin->deactivate) {
                plugin->deactivate(plugin);
            }
        }
        active = false;
    }

    // Deliver a host-driven parameter change to the plugin. CLAP has no direct
    // setter; the host queues a param-value event and calls params.flush(), which
    // the plugin applies on its main thread (#238). Returns false if the plugin
    // has no params extension or the id is out of range.
    bool setParameter(uint32_t paramId, double value) {
        if (!plugin || !paramsExt || !paramsExt->flush) {
            return false;
        }
        if (paramsExt->count && paramId >= paramsExt->count(plugin)) {
            return false;
        }
        ClapEventParamValue event{};
        event.header.size = sizeof(event);
        event.header.time = 0;
        event.header.spaceId = kClapCoreEventSpaceId;
        event.header.type = kClapEventParamValue;
        event.header.flags = 0;
        event.paramId = paramId;
        event.cookie = nullptr;
        event.noteId = -1;
        event.portIndex = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;

        ClapInputEvents inEvents = {&event, singleParamEventSize, singleParamEventGet};
        ClapOutputEvents outEvents = {nullptr, dropOutputEvent};
        paramsExt->flush(plugin, &inEvents, &outEvents);
        return true;
    }

    bool process(const std::vector<float>& interleavedInput, uint32_t channels, uint32_t frames,
                 const std::vector<uint8_t>* midiData, size_t midiBytes, std::vector<float>& interleavedOutput) {
        if (!active || !plugin || channels == 0 || channels > 2 || frames == 0 || frames > maxBlockSize) {
            return false;
        }
        for (uint32_t ch = 0; ch < 2; ++ch) {
            std::fill(outputStorage[ch].begin(), outputStorage[ch].begin() + frames, 0.0f);
            for (uint32_t frame = 0; frame < frames; ++frame) {
                inputStorage[ch][frame] =
                    (ch < channels) ? interleavedInput[static_cast<size_t>(frame) * channels + ch] : 0.0f;
            }
        }

        // Convert incoming raw MIDI to the note dialect selected for this plugin's
        // input port (#244): native CLAP note-on/off when the port prefers/only
        // supports CLAP, else raw CLAP_EVENT_MIDI. Non-note messages stay raw MIDI.
        // noteEventOrder preserves original stream order across both event types;
        // backing vectors are reserved so the recorded header pointers stay valid.
        midiEvents.clear();
        noteEvents.clear();
        noteEventOrder.clear();
        if (midiData && midiBytes <= midiData->size() && (midiBytes % 8) == 0) {
            for (size_t offset = 0; offset < midiBytes && noteEventOrder.size() < kMaxNoteEventsPerBlock;
                 offset += 8) {
                const uint8_t size = (*midiData)[offset + 4];
                if (size != 3) {
                    continue;
                }
                uint32_t sampleOffset = 0;
                std::memcpy(&sampleOffset, midiData->data() + offset, sizeof(sampleOffset));
                const uint32_t time = std::min<uint32_t>(sampleOffset, frames > 0 ? frames - 1 : 0);
                const uint8_t* bytes = midiData->data() + offset + 5;
                const ClapNote::DecodedNote note = ClapNote::decodeMidiMessage(bytes);
                const bool isNote = note.kind == ClapNote::MidiNoteKind::NoteOn ||
                                    note.kind == ClapNote::MidiNoteKind::NoteOff;

                if (isNote) {
                    // A note goes out in the selected dialect. noteDialect==0 means
                    // the port shares no dialect with us — drop rather than guess.
                    if (noteDialect == ClapNote::kDialectClap) {
                        ClapEventNote event{};
                        event.header.size = sizeof(event);
                        event.header.time = time;
                        event.header.spaceId = kClapCoreEventSpaceId;
                        event.header.type = note.kind == ClapNote::MidiNoteKind::NoteOn ? kClapEventNoteOn
                                                                                       : kClapEventNoteOff;
                        event.header.flags = 0;
                        event.noteId = -1;
                        event.portIndex = 0;
                        event.channel = static_cast<int16_t>(note.channel);
                        event.key = static_cast<int16_t>(note.key);
                        event.velocity = ClapNote::normalizedVelocity(note.velocity);
                        noteEvents.push_back(event);
                        noteEventOrder.push_back(&noteEvents.back().header);
                    } else if (noteDialect == ClapNote::kDialectMidi) {
                        ClapEventMidi event{};
                        event.header.size = sizeof(event);
                        event.header.time = time;
                        event.header.spaceId = kClapCoreEventSpaceId;
                        event.header.type = kClapEventMidi;
                        event.header.flags = 0;
                        event.portIndex = 0;
                        std::memcpy(event.data, bytes, 3);
                        midiEvents.push_back(event);
                        noteEventOrder.push_back(&midiEvents.back().header);
                    }
                    // else noteDialect == 0: no mutually supported dialect, drop.
                } else if (rawMidiAllowed) {
                    // Non-note MIDI (CC, pitch bend…) rides the raw-MIDI dialect only
                    // when the port advertises it. On a CLAP-only port it is dropped
                    // deliberately — never emitted as a dialect the port did not accept.
                    ClapEventMidi event{};
                    event.header.size = sizeof(event);
                    event.header.time = time;
                    event.header.spaceId = kClapCoreEventSpaceId;
                    event.header.type = kClapEventMidi;
                    event.header.flags = 0;
                    event.portIndex = 0;
                    std::memcpy(event.data, bytes, 3);
                    midiEvents.push_back(event);
                    noteEventOrder.push_back(&midiEvents.back().header);
                }
            }
        }
        ClapInputEvents midiInputEvents = {&noteEventOrder, orderedEventsSize, orderedEventsGet};

        ClapAudioBuffer inputBuffer = {};
        inputBuffer.data32 = inputPlanes;
        inputBuffer.channelCount = channels;
        inputBuffer.latency = 0;
        inputBuffer.constantMask = 0;

        ClapAudioBuffer outputBuffer = {};
        outputBuffer.data32 = outputPlanes;
        outputBuffer.channelCount = channels;
        outputBuffer.latency = 0;
        outputBuffer.constantMask = 0;

        ClapProcess processData = {};
        processData.steadyTime = -1;
        processData.framesCount = frames;
        processData.transport = nullptr;
        processData.audioInputs = &inputBuffer;
        processData.audioOutputs = &outputBuffer;
        processData.audioInputsCount = 1;
        processData.audioOutputsCount = 1;
        processData.inEvents = noteEventOrder.empty() ? &emptyInputEvents : &midiInputEvents;
        processData.outEvents = &outputEvents;

        if (plugin->process(plugin, &processData) == kClapProcessError) {
            return false;
        }

        interleavedOutput.resize(static_cast<size_t>(channels) * frames);
        for (uint32_t frame = 0; frame < frames; ++frame) {
            for (uint32_t ch = 0; ch < channels; ++ch) {
                interleavedOutput[static_cast<size_t>(frame) * channels + ch] = outputStorage[ch][frame];
            }
        }
        return true;
    }

    bool saveState(std::vector<uint8_t>& stateData) {
        stateData.clear();
        if (!plugin || !plugin->getExtension) {
            return false;
        }
        auto* state = static_cast<const ClapPluginState*>(plugin->getExtension(plugin, kClapExtState));
        if (!state || !state->save) {
            return false;
        }
        ClapOstream stream = {&stateData, writeStateStream};
        return state->save(plugin, &stream);
    }

    bool loadState(const std::vector<uint8_t>& stateData) {
        if (stateData.empty()) {
            return true;
        }
        if (!plugin || !plugin->getExtension) {
            return false;
        }
        auto* state = static_cast<const ClapPluginState*>(plugin->getExtension(plugin, kClapExtState));
        if (!state || !state->load) {
            return false;
        }
        StateReadContext ctx{&stateData, 0};
        ClapIstream stream = {&ctx, readStateStream};
        return state->load(plugin, &stream);
    }

    bool scanMetadata(const std::string& path, const ClapPluginDescriptor*& descriptor, std::string& error) {
        descriptor = nullptr;
        close();
        library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!library) {
            error = "clap-dlopen-failed";
            return false;
        }

        entry = reinterpret_cast<const ClapPluginEntry*>(dlsym(library, "clap_entry"));
        if (!entry || !isClapVersionCompatible(entry->clapVersion) || !entry->init || !entry->getFactory) {
            error = "invalid-clap-entry";
            close();
            return false;
        }
        if (!entry->init(path.c_str())) {
            error = "clap-init-failed";
            close();
            return false;
        }

        const auto* factory = static_cast<const ClapPluginFactory*>(entry->getFactory(kClapPluginFactoryId));
        if (!factory || !factory->getPluginCount || !factory->getPluginDescriptor) {
            error = "missing-clap-factory";
            close();
            return false;
        }
        if (factory->getPluginCount(factory) == 0) {
            error = "empty-clap-factory";
            close();
            return false;
        }
        descriptor = factory->getPluginDescriptor(factory, 0);
        if (!descriptor || !descriptor->id || descriptor->id[0] == '\0') {
            error = "invalid-clap-descriptor";
            close();
            return false;
        }
        return true;
    }
};
#endif

#ifdef AESTRA_HAS_VST3
struct Vst3Module {
    std::shared_ptr<Aestra::Audio::VST3PluginInstance> plugin;
    uint32_t maxBlockSize = 0;
    std::vector<float> inputStorage[2];
    std::vector<float> outputStorage[2];
    Aestra::Audio::MidiBuffer midiInput;

    ~Vst3Module() { close(); }

    Vst3Module() = default;
    Vst3Module(const Vst3Module&) = delete;
    Vst3Module& operator=(const Vst3Module&) = delete;

    void close() {
        if (plugin) {
            plugin->shutdown();
            plugin->unload();
            plugin.reset();
        }
        maxBlockSize = 0;
        for (auto& storage : inputStorage) {
            storage.clear();
        }
        for (auto& storage : outputStorage) {
            storage.clear();
        }
        midiInput.clear();
    }

    bool load(const std::string& path, const std::string& pluginId, std::string& error) {
        close();

        Aestra::Audio::PluginInfo info;
        info.id = pluginId;
        info.name = pluginId;
        info.vendor = "Unknown";
        info.version = {};
        info.category = {};
        info.format = Aestra::Audio::PluginFormat::VST3;
        info.type = Aestra::Audio::PluginType::Effect;
        info.path = path;
        info.numAudioInputs = 2;
        info.numAudioOutputs = 2;

        plugin = Aestra::Audio::VST3PluginFactory::createInstance(info);
        if (!plugin || !plugin->isLoaded()) {
            plugin.reset();
            error = "vst3-load-failed";
            return false;
        }
        return true;
    }

    bool initialize(double sampleRate, uint32_t blockSize, std::string& error) {
        if (!plugin || sampleRate <= 0.0 || blockSize == 0 || blockSize > 65536) {
            error = "invalid-vst3-init";
            return false;
        }
        if (!plugin->initialize(sampleRate, blockSize)) {
            error = "vst3-initialize-failed";
            return false;
        }
        maxBlockSize = blockSize;
        for (auto& storage : inputStorage) {
            storage.assign(maxBlockSize, 0.0f);
        }
        for (auto& storage : outputStorage) {
            storage.assign(maxBlockSize, 0.0f);
        }
        return true;
    }

    bool activate() {
        if (!plugin) {
            return false;
        }
        plugin->activate();
        return plugin->isActive();
    }

    void deactivate() {
        if (plugin) {
            plugin->deactivate();
        }
    }

    bool process(const std::vector<float>& interleavedInput, uint32_t channels, uint32_t frames,
                 const std::vector<uint8_t>* midiData, size_t midiBytes, std::vector<float>& interleavedOutput) {
        if (!plugin || !plugin->isActive() || channels == 0 || channels > 2 || frames == 0 || frames > maxBlockSize ||
            interleavedInput.size() < static_cast<size_t>(channels) * frames) {
            return false;
        }

        for (uint32_t ch = 0; ch < 2; ++ch) {
            std::fill(outputStorage[ch].begin(), outputStorage[ch].begin() + frames, 0.0f);
            for (uint32_t frame = 0; frame < frames; ++frame) {
                inputStorage[ch][frame] =
                    (ch < channels) ? interleavedInput[static_cast<size_t>(frame) * channels + ch] : 0.0f;
            }
        }

        midiInput.clear();
        if (midiData && midiBytes <= midiData->size() && (midiBytes % 8) == 0) {
            for (size_t offset = 0; offset < midiBytes; offset += 8) {
                const uint8_t size = (*midiData)[offset + 4];
                if (size == 0 || size > 3) {
                    continue;
                }
                uint32_t sampleOffset = 0;
                std::memcpy(&sampleOffset, midiData->data() + offset, sizeof(sampleOffset));
                midiInput.addEvent(std::min<uint32_t>(sampleOffset, frames - 1), midiData->data() + offset + 5, size);
            }
        }

        const float* inputPlanes[2] = {inputStorage[0].data(), inputStorage[1].data()};
        float* outputPlanes[2] = {outputStorage[0].data(), outputStorage[1].data()};
        plugin->process(inputPlanes, outputPlanes, channels, channels, frames,
                        midiInput.isEmpty() ? nullptr : &midiInput, nullptr);
        if (plugin->isCrashed()) {
            return false;
        }

        interleavedOutput.resize(static_cast<size_t>(channels) * frames);
        for (uint32_t frame = 0; frame < frames; ++frame) {
            for (uint32_t ch = 0; ch < channels; ++ch) {
                interleavedOutput[static_cast<size_t>(frame) * channels + ch] = outputStorage[ch][frame];
            }
        }
        return true;
    }

    std::vector<uint8_t> saveState() const {
        if (!plugin) {
            return {};
        }
        return plugin->saveState();
    }

    bool loadState(const std::vector<uint8_t>& stateData) {
        if (!plugin) {
            return false;
        }
        return plugin->loadState(stateData);
    }
};
#endif

int hexValue(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + c - 'a';
    if (c >= 'A' && c <= 'F')
        return 10 + c - 'A';
    return -1;
}

std::string hexDecode(const std::string& input) {
    std::string out;
    if ((input.size() % 2) != 0) {
        return out;
    }
    out.reserve(input.size() / 2);
    for (size_t i = 0; i < input.size(); i += 2) {
        const int hi = hexValue(input[i]);
        const int lo = hexValue(input[i + 1]);
        if (hi < 0 || lo < 0) {
            return {};
        }
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return out;
}

std::string hexEncodeBytes(const void* data, size_t size) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::string out;
    out.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out.push_back(kHex[bytes[i] >> 4]);
        out.push_back(kHex[bytes[i] & 0x0F]);
    }
    return out;
}

bool hexDecodeRaw(const std::string& input, std::vector<uint8_t>& output) {
    if ((input.size() % 2) != 0) {
        return false;
    }
    output.resize(input.size() / 2);
    for (size_t i = 0; i < output.size(); ++i) {
        const int hi = hexValue(input[i * 2]);
        const int lo = hexValue(input[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        output[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

bool hexDecodeFloats(const std::string& input, std::vector<float>& output) {
    if ((input.size() % 2) != 0) {
        return false;
    }
    const size_t byteCount = input.size() / 2;
    if ((byteCount % sizeof(float)) != 0) {
        return false;
    }
    output.resize(byteCount / sizeof(float));
    auto* bytes = reinterpret_cast<unsigned char*>(output.data());
    for (size_t i = 0; i < byteCount; ++i) {
        const int hi = hexValue(input[i * 2]);
        const int lo = hexValue(input[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        bytes[i] = static_cast<unsigned char>((hi << 4) | lo);
    }
    return true;
}

std::string hexEncodeString(const std::string& input) {
    return hexEncodeBytes(input.data(), input.size());
}

void fatalSignalHandler(int signal) noexcept {
    std::_Exit(128 + signal);
}

void installFatalSignalHandlers() noexcept {
    std::signal(SIGSEGV, fatalSignalHandler);
    std::signal(SIGABRT, fatalSignalHandler);
    std::signal(SIGILL, fatalSignalHandler);
    std::signal(SIGFPE, fatalSignalHandler);
#ifdef SIGBUS
    std::signal(SIGBUS, fatalSignalHandler);
#endif
}

void reply(const std::string& line) {
    std::cout << line << '\n';
    std::cout.flush();
}

} // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

#ifdef _WIN32
    // This is a binary protocol, not console text. In Windows' default text mode
    // every '\n' written to stdout becomes "\r\n", which put a stray '\r' at the end
    // of every reply. Commands that only checked for an "OK" prefix never noticed,
    // but PROCESS and SAVESTATE hex-decode the rest of the line, and the extra byte
    // made the payload an odd number of characters — rejected outright, so audio and
    // plugin state through a sandboxed plugin failed on Windows while LOAD,
    // INITIALIZE and ACTIVATE all looked healthy.
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
#endif

    installFatalSignalHandlers();

    bool loaded = false;
    bool active = false;
    uint32_t maxBlockSize = 0;
    std::vector<float> processBuffer;
    std::vector<uint8_t> midiBuffer;
    std::vector<uint8_t> stateBuffer;
    std::string line;
#ifdef AESTRA_ENABLE_TEST_HOOKS
    // The __aestra_test_echo__ fake plugin has no real module; it records applied
    // parameters so tests can prove host->child parameter delivery end to end.
    // Parameter 0 is treated as an output gain applied to the echoed audio, so a
    // test can observe the change behaviorally rather than trusting a log (#238).
    bool echoPlugin = false;
    std::array<float, 16> echoParams{};
    echoParams.fill(0.0f);
    echoParams[0] = 1.0f; // unity gain until set
#endif
#ifndef _WIN32
    ClapModule clapModule;
#endif
#ifdef AESTRA_HAS_VST3
    Vst3Module vst3Module;
#endif

    while (std::getline(std::cin, line)) {
        // Binary stdin (above) means getline no longer strips a '\r' for us. The
        // parent frames with '\n' alone, so this is belt-and-braces on an untrusted
        // parse boundary rather than a live case.
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::istringstream input(line);
        std::string command;
        input >> command;

        if (command == "PING") {
            reply("OK PONG");
        } else if (command == "SCAN") {
            std::string format;
            std::string pathHex;
            input >> format >> pathHex;
            const std::string path = hexDecode(pathHex);
            if (format != "clap") {
                reply("ERR unsupported-format");
                continue;
            }
            if (path.empty()) {
                reply("ERR invalid-scan-request");
                continue;
            }
#ifndef _WIN32
            const ClapPluginDescriptor* descriptor = nullptr;
            std::string error;
            if (!clapModule.scanMetadata(path, descriptor, error)) {
                reply("ERR " + error);
                continue;
            }

            std::string features;
            if (descriptor->features) {
                for (const char* const* f = descriptor->features; *f; ++f) {
                    if (!features.empty()) {
                        features += "|";
                    }
                    features += *f;
                }
            }

            reply("OK " + hexEncodeString(descriptor->id ? descriptor->id : "") + " " +
                  hexEncodeString(descriptor->name ? descriptor->name : "") + " " +
                  hexEncodeString(descriptor->vendor ? descriptor->vendor : "") + " " +
                  hexEncodeString(descriptor->version ? descriptor->version : "") + " " + hexEncodeString(features));
#else
            reply("ERR unsupported-platform");
#endif
        } else if (command == "LOAD") {
            std::string format;
            std::string idHex;
            std::string pathHex;
            input >> format >> idHex >> pathHex;
            const std::string id = hexDecode(idHex);
            const std::string path = hexDecode(pathHex);
#ifdef AESTRA_ENABLE_TEST_HOOKS
            if (id == "__aestra_test_crash__") {
                std::abort();
            }
#ifndef _WIN32
            // Fake CLAP note endpoints (#244): configure the fake's advertised note
            // dialect, then wire it through the REAL ClapModule so the test exercises
            // the production note-conversion boundary. g_state also records events.
            {
                bool isFakeClap = true;
                fakeclap::g_state = {}; // reset config + clear recorded events
                if (id == "__aestra_test_clap_note__") {
                    fakeclap::g_state.supportedDialects = ClapNote::kDialectClap;
                    fakeclap::g_state.preferredDialect = ClapNote::kDialectClap;
                } else if (id == "__aestra_test_clap_midi__") {
                    fakeclap::g_state.supportedDialects = ClapNote::kDialectMidi;
                    fakeclap::g_state.preferredDialect = ClapNote::kDialectMidi;
                } else if (id == "__aestra_test_clap_dual_pref_clap__") {
                    fakeclap::g_state.supportedDialects =
                        ClapNote::kDialectClap | ClapNote::kDialectMidi;
                    fakeclap::g_state.preferredDialect = ClapNote::kDialectClap;
                } else if (id == "__aestra_test_clap_legacy__") {
                    fakeclap::g_state.exposeNotePorts = false; // no note-ports extension
                } else {
                    isFakeClap = false;
                }
                if (isFakeClap) {
                    clapModule.loadFakeForTest();
                    loaded = true;
                    echoPlugin = false;
                    reply("OK LOADED");
                    continue;
                }
            }
#endif
#endif
            if (format != "vst3" && format != "clap") {
                reply("ERR unsupported-format");
                continue;
            }
            if (id.empty() || path.empty()) {
                reply("ERR invalid-load-request");
                continue;
            }

            // A real plugin is only "loaded" if a real backend loaded it. When this
            // build has no backend for the requested format the guarded blocks below
            // are compiled out entirely, so without this refusal control would fall
            // straight through to `loaded = true` and hand back a usable proxy for a
            // plugin nobody ever opened -- including one whose file does not exist.
            // SCAN already refuses this way ("ERR unsupported-platform"); LOAD did not.
            bool exemptFake = false;
#ifdef AESTRA_ENABLE_TEST_HOOKS
            exemptFake = (id == "__aestra_test_echo__"); // in-host fake, no module to load
#endif
            if (!exemptFake) {
#ifndef AESTRA_HAS_VST3
                if (format == "vst3") {
                    reply("ERR vst3-unsupported-in-this-build");
                    continue;
                }
#endif
#ifdef _WIN32
                if (format == "clap") {
                    reply("ERR clap-unsupported-on-this-platform");
                    continue;
                }
#endif
            }
#ifndef _WIN32
            if (format == "clap" && id != "__aestra_test_echo__") {
#ifdef AESTRA_HAS_VST3
                vst3Module.close();
#endif
                std::string error;
                if (!clapModule.load(path, id, error)) {
                    reply("ERR " + error);
                    continue;
                }
            } else {
                clapModule.close();
            }
#endif
#ifdef AESTRA_HAS_VST3
            if (format == "vst3" && id != "__aestra_test_echo__") {
#ifndef _WIN32
                clapModule.close();
#endif
                std::string error;
                if (!vst3Module.load(path, id, error)) {
                    reply("ERR " + error);
                    continue;
                }
            } else {
                vst3Module.close();
            }
#endif
            loaded = true;
#ifdef AESTRA_ENABLE_TEST_HOOKS
            echoPlugin = (id == "__aestra_test_echo__");
#endif
            reply("OK LOADED");
        } else if (command == "INIT") {
            if (!loaded) {
                reply("ERR not-loaded");
                continue;
            }
            double sampleRate = 0.0;
            input >> sampleRate >> maxBlockSize;
            if (sampleRate <= 0.0 || maxBlockSize == 0 || maxBlockSize > 65536) {
                reply("ERR invalid-init");
                continue;
            }
#ifndef _WIN32
            if (clapModule.plugin) {
                std::string error;
                if (!clapModule.initialize(sampleRate, maxBlockSize, error)) {
                    reply("ERR " + error);
                    continue;
                }
            }
#endif
#ifdef AESTRA_HAS_VST3
            if (vst3Module.plugin) {
                std::string error;
                if (!vst3Module.initialize(sampleRate, maxBlockSize, error)) {
                    reply("ERR " + error);
                    continue;
                }
            }
#endif
            reply("OK INIT");
        } else if (command == "ACTIVATE") {
            if (!loaded) {
                reply("ERR not-loaded");
                continue;
            }
#ifdef AESTRA_HAS_VST3
            if (vst3Module.plugin && !vst3Module.activate()) {
                reply("ERR vst3-activate-failed");
                continue;
            }
#endif
            active = true;
            reply("OK ACTIVE");
        } else if (command == "DEACTIVATE") {
            active = false;
#ifndef _WIN32
            clapModule.deactivate();
#endif
#ifdef AESTRA_HAS_VST3
            vst3Module.deactivate();
#endif
            reply("OK INACTIVE");
        } else if (command == "SETPARAM") {
            if (!loaded) {
                reply("ERR not-loaded");
                continue;
            }
            uint32_t paramId = 0;
            double value = 0.0;
            if (!(input >> paramId >> value)) {
                reply("ERR invalid-setparam");
                continue;
            }
            if (!std::isfinite(value)) {
                reply("ERR non-finite-value");
                continue;
            }
#ifdef AESTRA_ENABLE_TEST_HOOKS
            if (echoPlugin) {
                if (paramId >= echoParams.size()) {
                    reply("ERR param-id-out-of-range");
                    continue;
                }
                echoParams[paramId] = static_cast<float>(value);
                reply("OK SETPARAM");
                continue;
            }
#endif
#ifndef _WIN32
            if (clapModule.plugin) {
                reply(clapModule.setParameter(paramId, value) ? "OK SETPARAM" : "ERR setparam-failed");
                continue;
            }
#endif
#ifdef AESTRA_HAS_VST3
            if (vst3Module.plugin) {
                vst3Module.plugin->setParameter(paramId, static_cast<float>(value));
                reply("OK SETPARAM");
                continue;
            }
#endif
            reply("ERR no-plugin");
#if defined(AESTRA_ENABLE_TEST_HOOKS) && !defined(_WIN32)
        } else if (command == "TESTNOTES") {
            // Dump the events the fake CLAP plugin actually received through
            // process.in_events (#244). Format: "OK <n>" then one token per event:
            //   type:time:port:channel:key:velocity:m0:m1:m2
            // (channel/key/velocity are the note fields; m0..m2 the raw MIDI bytes).
            std::ostringstream out;
            out << "OK " << fakeclap::g_state.events.size();
            for (const auto& e : fakeclap::g_state.events) {
                out << ' ' << e.type << ':' << e.time << ':' << e.portIndex << ':' << e.channel << ':'
                    << e.key << ':' << e.velocity << ':' << static_cast<int>(e.midi[0]) << ':'
                    << static_cast<int>(e.midi[1]) << ':' << static_cast<int>(e.midi[2]);
            }
            reply(out.str());
#endif
#ifdef AESTRA_ENABLE_TEST_HOOKS
        } else if (command == "TESTSTALL") {
            // Stall part-way through a reply, then finish it after the parent has
            // certainly given up. This reproduces the one case that silently
            // corrupts the channel: the parent consumes "OK stalled", hits its
            // deadline mid-line, and (before the framing fix) discarded those bytes
            // and kept the channel. The "-completed\n" written below then arrives
            // first in the pipe and is returned as the reply to whatever command the
            // parent sends NEXT — a mismatched request/response pair that still
            // looks well-formed.
            //
            // The stall must outlast sendRawCommandForTest's 2s timeout; 3s leaves a
            // wide margin without making the test slow. Deliberately not
            // Windows-guarded: this exercises the transport, which both platforms
            // now have.
            std::cout << "OK stalled" << std::flush; // no '\n' yet, on purpose
            std::this_thread::sleep_for(std::chrono::milliseconds(3000));
            std::cout << "-completed\n" << std::flush;
        } else if (command == "TESTCRLF") {
            // Terminate a reply with CRLF explicitly. This is what Windows' default
            // text-mode stdout did to EVERY reply before _setmode above, and it broke
            // PROCESS and SAVESTATE while leaving prefix-checked commands looking
            // fine. Written literally so the parent's normalization is exercised on
            // every platform instead of only where the bug reproduced.
            std::cout << "OK 0badc0de\r\n" << std::flush;
#endif
        } else if (command == "SAVESTATE") {
            if (!loaded) {
                reply("ERR not-loaded");
                continue;
            }
#ifndef _WIN32
            if (clapModule.plugin) {
                stateBuffer.clear();
                if (!clapModule.saveState(stateBuffer)) {
                    reply("ERR state-unavailable");
                    continue;
                }
                reply("OK " + hexEncodeBytes(stateBuffer.data(), stateBuffer.size()));
                continue;
            }
#endif
#ifdef AESTRA_HAS_VST3
            if (vst3Module.plugin) {
                stateBuffer = vst3Module.saveState();
                reply("OK " + hexEncodeBytes(stateBuffer.data(), stateBuffer.size()));
                continue;
            }
#endif
            reply("OK ");
        } else if (command == "LOADSTATE") {
            if (!loaded) {
                reply("ERR not-loaded");
                continue;
            }
            std::string stateHex;
            input >> stateHex;
            if (!hexDecodeRaw(stateHex, stateBuffer)) {
                reply("ERR invalid-state");
                continue;
            }
#ifndef _WIN32
            if (clapModule.plugin) {
                if (!clapModule.loadState(stateBuffer)) {
                    reply("ERR state-load-failed");
                    continue;
                }
                reply("OK STATE");
                continue;
            }
#endif
#ifdef AESTRA_HAS_VST3
            if (vst3Module.plugin) {
                if (!vst3Module.loadState(stateBuffer)) {
                    reply("ERR state-load-failed");
                    continue;
                }
                reply("OK STATE");
                continue;
            }
#endif
            reply(stateBuffer.empty() ? "OK STATE" : "ERR state-unavailable");
        } else if (command == "SHUTDOWN") {
            active = false;
            loaded = false;
#ifndef _WIN32
            clapModule.close();
#endif
#ifdef AESTRA_HAS_VST3
            vst3Module.close();
#endif
            reply("OK SHUTDOWN");
        } else if (command == "EXIT") {
#ifndef _WIN32
            clapModule.close();
#endif
#ifdef AESTRA_HAS_VST3
            vst3Module.close();
#endif
            reply("OK EXIT");
            return 0;
        } else if (command == "STATUS") {
            reply(active ? "OK ACTIVE" : (loaded ? "OK LOADED" : "OK EMPTY"));
        } else if (command == "PROCESS" || command == "PROCESSMIDI") {
            if (!loaded || !active) {
                reply("ERR inactive");
                continue;
            }
            uint32_t channels = 0;
            uint32_t frames = 0;
            std::string payloadHex;
            std::string midiHex;
            input >> channels >> frames >> payloadHex;
            if (command == "PROCESSMIDI") {
                input >> midiHex;
            }
            if (channels == 0 || channels > 2 || frames == 0 || frames > maxBlockSize) {
                reply("ERR invalid-process-size");
                continue;
            }
            if (!hexDecodeFloats(payloadHex, processBuffer) ||
                processBuffer.size() < static_cast<size_t>(channels) * frames) {
                reply("ERR invalid-process-payload");
                continue;
            }
            midiBuffer.clear();
            if (command == "PROCESSMIDI" && !hexDecodeRaw(midiHex, midiBuffer)) {
                reply("ERR invalid-midi-payload");
                continue;
            }

#ifndef _WIN32
            if (clapModule.plugin) {
                std::vector<float> processed;
                const std::vector<uint8_t>* midiData = midiBuffer.empty() ? nullptr : &midiBuffer;
                if (!clapModule.process(processBuffer, channels, frames, midiData, midiBuffer.size(), processed)) {
                    reply("ERR clap-process-failed");
                    continue;
                }
                const size_t byteCount = static_cast<size_t>(channels) * frames * sizeof(float);
                reply("OK " + hexEncodeBytes(processed.data(), byteCount));
                continue;
            }
#endif
#ifdef AESTRA_HAS_VST3
            if (vst3Module.plugin) {
                std::vector<float> processed;
                const std::vector<uint8_t>* midiData = midiBuffer.empty() ? nullptr : &midiBuffer;
                if (!vst3Module.process(processBuffer, channels, frames, midiData, midiBuffer.size(), processed)) {
                    reply("ERR vst3-process-failed");
                    continue;
                }
                const size_t byteCount = static_cast<size_t>(channels) * frames * sizeof(float);
                reply("OK " + hexEncodeBytes(processed.data(), byteCount));
                continue;
            }
#endif

            const size_t byteCount = static_cast<size_t>(channels) * frames * sizeof(float);
#ifdef AESTRA_ENABLE_TEST_HOOKS
            // Echo fake plugin: apply parameter 0 as an output gain so a test can
            // observe host->child parameter delivery through the audio itself (#238).
            if (echoPlugin && echoParams[0] != 1.0f) {
                const size_t sampleCount = static_cast<size_t>(channels) * frames;
                for (size_t i = 0; i < sampleCount && i < processBuffer.size(); ++i) {
                    processBuffer[i] *= echoParams[0];
                }
            }
#endif
            reply("OK " + hexEncodeBytes(processBuffer.data(), byteCount));
        } else {
            reply("ERR unknown-command");
        }
    }

    return 0;
}
