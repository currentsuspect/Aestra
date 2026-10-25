// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The sandbox helper's CLAP host: loading a module, the parameter / note / state paths and the host callbacks it hands a
// plugin, on Linux, macOS and Windows (HelperDynLib.h). Moved out of AestraPluginHostMain.cpp so that file stays a command loop; included by that
// one translation unit only, so the helpers below live in an anonymous namespace.


#include "ClapAbiMirror.h"
#include "HelperFakeClap.h"
#include "HelperClapHostSupport.h"
#include "HelperDynLib.h"
#include "HelperEditorHost.h"
#include "HelperParamCommands.h"
#include "Plugin/ClapNoteConversion.h"
#include "Plugin/HostTransport.h"


#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

// The shared CLAP note-conversion rules live in Aestra::Audio::ClapNote; this
// file is in the global namespace, so alias it for brevity (#244).
namespace ClapNote = Aestra::Audio::ClapNote;

using namespace AestraHelper::ClapAbi; // the hand-mirrored CLAP ABI (ClapAbiMirror.h)

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

// What a plugin reports from inside process(): its own parameter moves (the knobs in its window), collected so they can
// be told to the app. Only parameter values are kept.
struct ClapOutCollector {
    std::vector<std::pair<uint32_t, double>> values; // (id, plain value)
};

bool collectOutputEvent(const ClapOutputEvents* list, const ClapEventHeader* event) {
    auto* out = list ? static_cast<ClapOutCollector*>(list->ctx) : nullptr;
    if (out && event && event->spaceId == kClapCoreEventSpaceId && event->type == kClapEventParamValue &&
        event->size >= sizeof(ClapEventParamValue) && out->values.size() < out->values.capacity()) {
        const auto* e = reinterpret_cast<const ClapEventParamValue*>(event);
        out->values.emplace_back(e->paramId, e->value);
    }
    return true;
}

#ifdef AESTRA_ENABLE_TEST_HOOKS
namespace fakeclap = AestraHelper::FakeClap; // the test fake CLAP plugin (HelperFakeClap.h)
#endif

struct ClapModule {
    void* library = nullptr;
    const ClapPluginEntry* entry = nullptr;
    const ClapPlugin* plugin = nullptr;
    ClapHostContext hostContext; // what the plugin's host callbacks reach (host.hostData)
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
    ClapOutCollector outCollector;
    ClapOutputEvents outputEvents = {&outCollector, collectOutputEvent};
    const ClapPluginParams* paramsExt = nullptr;
    const ClapPluginGui* guiExt = nullptr;
    bool guiOpen = false;

    // Parameters (V82-H5, CLAP side). The app speaks normalized 0..1; CLAP speaks plain values, so every parameter's
    // range is read once at load. Values for the plugin wait in pendingParams (any thread pushes) and go in with the
    // next block's events: CLAP only allows params.flush() while the plugin is NOT processing.
    struct ParamMeta {
        uint32_t id;
        uint32_t flags;
        double minValue, maxValue, defaultValue;
        std::string name;
    };
    std::vector<ParamMeta> paramMeta;
    std::mutex paramMutex;
    std::vector<std::pair<uint32_t, double>> pendingParams; // (id, plain value)
    static constexpr size_t kMaxParamEventsPerBlock = 128;
    std::vector<ClapEventParamValue> paramEvents;
    /// Called from process() for each parameter the plugin moved on its own, with the normalized value.
    std::function<void(uint32_t, double)> onParamOut;
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

    ClapModule() { host.hostData = &hostContext; }
    ClapModule(const ClapModule&) = delete;
    ClapModule& operator=(const ClapModule&) = delete;

    void close() {
        closeGui();
        deactivate();
        paramsExt = nullptr;
        guiExt = nullptr;
        paramMeta.clear();
        {
            std::lock_guard<std::mutex> lock(paramMutex);
            pendingParams.clear();
        }
        hostContext.clear();
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
            AestraHelper::closeLibrary(library);
            library = nullptr;
        }
        maxBlockSize = 0;
    }

    bool load(const std::string& path, const std::string& pluginId, std::string& error) {
        close();
        library = AestraHelper::openLibrary(path);
        if (!library) {
            error = "clap-load-failed";
            return false;
        }

        entry = reinterpret_cast<const ClapPluginEntry*>(AestraHelper::librarySymbol(library, "clap_entry"));
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
        hostContext.plugin = nullptr;
        plugin = factory->createPlugin(factory, &host, selected->id);
        hostContext.plugin = plugin; // before init(): a plugin may register timers there
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
            guiExt = static_cast<const ClapPluginGui*>(plugin->getExtension(plugin, kClapExtGui));
        }
        readParamRanges();
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

    /// The plugin's own latency in samples (clap.latency), 0 when it has none or is not active yet.
    uint32_t latencySamples() const {
        if (!plugin || !active || !plugin->getExtension) return 0;
        const auto* latency = static_cast<const ClapPluginLatency*>(plugin->getExtension(plugin, kClapExtLatency));
        return latency && latency->get ? latency->get(plugin) : 0;
    }

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
        paramEvents.reserve(kMaxParamEventsPerBlock);
        outCollector.values.reserve(256);
        midiEvents.reserve(kMaxNoteEventsPerBlock);
        noteEvents.reserve(kMaxNoteEventsPerBlock);
        noteEventOrder.reserve(kMaxNoteEventsPerBlock + kMaxParamEventsPerBlock);
        inputPlanes[0] = inputStorage[0].data();
        inputPlanes[1] = inputStorage[1].data();
        outputPlanes[0] = outputStorage[0].data();
        outputPlanes[1] = outputStorage[1].data();
        if (!plugin->activate(plugin, sampleRate, 1, maxBlockSize)) {
            error = "clap-activate-failed";
            return false;
        }
        active = true;
        // start/stop_processing belong to the audio thread, and a plugin that checks says so: for us that is the
        // thread that also calls process().
        ClapHostContext::inProcess() = true;
        const bool started = plugin->startProcessing(plugin);
        ClapHostContext::inProcess() = false;
        if (!started) {
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
                ClapHostContext::inProcess() = true;
                plugin->stopProcessing(plugin);
                ClapHostContext::inProcess() = false;
            }
            if (plugin->deactivate) {
                plugin->deactivate(plugin);
            }
        }
        active = false;
    }

    void readParamRanges() {
        paramMeta.clear();
        if (!plugin || !paramsExt || !paramsExt->count || !paramsExt->getInfo) return;
        const uint32_t count = paramsExt->count(plugin);
        paramMeta.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            ClapParamInfo info{};
            if (!paramsExt->getInfo(plugin, i, &info)) continue;
            paramMeta.push_back({info.id, info.flags, info.minValue, info.maxValue, info.defaultValue, info.name});
        }
    }

    const ParamMeta* metaOf(uint32_t id) const {
        for (const auto& m : paramMeta)
            if (m.id == id) return &m;
        return nullptr;
    }
    static double normalize(const ParamMeta& m, double plain) {
        return m.maxValue > m.minValue ? std::clamp((plain - m.minValue) / (m.maxValue - m.minValue), 0.0, 1.0) : 0.0;
    }
    static double denormalize(const ParamMeta& m, double normalized) {
        return m.minValue + std::clamp(normalized, 0.0, 1.0) * (m.maxValue - m.minValue);
    }

    // Deliver a host-driven parameter change (normalized) to the plugin. CLAP has no direct setter: the value rides in
    // with the next block's input events (never params.flush() while processing: the plugin forbids it, and the old
    // call here did exactly that, through a mis-copied function table). False when the id is unknown.
    bool setParameter(uint32_t paramId, double value) {
        const ParamMeta* meta = plugin ? metaOf(paramId) : nullptr;
        if (!meta) return false;
        const double plain = denormalize(*meta, value);
        std::lock_guard<std::mutex> lock(paramMutex);
        for (auto& p : pendingParams)
            if (p.first == paramId) {
                p.second = plain;
                return true;
            }
        pendingParams.emplace_back(paramId, plain);
        return true;
    }

    /// PARAMS reply: same format as the VST3 one. Hidden parameters are left out.
    std::string paramsReply() const {
        std::string out;
        size_t n = 0;
        char buf[96];
        for (const auto& m : paramMeta) {
            if (m.flags & kClapParamIsHidden) continue;
            const int flags = ((m.flags & kClapParamIsAutomatable) ? 1 : 0) | ((m.flags & kClapParamIsBypass) ? 2 : 0) |
                              ((m.flags & kClapParamIsReadOnly) ? 4 : 0);
            const uint32_t steps = (m.flags & kClapParamIsStepped) ? static_cast<uint32_t>(m.maxValue - m.minValue) : 0;
            std::snprintf(buf, sizeof buf, " %u:%d:%u:%.9g:", m.id, flags, steps, normalize(m, m.defaultValue));
            out += buf;
            out += AestraHelper::hexOf(m.name) + ":" + AestraHelper::hexOf(m.name) + ":" + AestraHelper::hexOf("");
            ++n;
        }
        return "OK " + std::to_string(n) + out;
    }

    /// GETPARAM reply: the live normalized value and the plugin's own text for it.
    std::string getParamReply(uint32_t paramId) const {
        const ParamMeta* meta = plugin ? metaOf(paramId) : nullptr;
        if (!meta || !paramsExt || !paramsExt->getValue) return "ERR no-param";
        double plain = 0.0;
        if (!paramsExt->getValue(plugin, paramId, &plain)) return "ERR no-param";
        char text[256] = {};
        if (!paramsExt->valueToText || !paramsExt->valueToText(plugin, paramId, plain, text, sizeof text)) text[0] = '\0';
        char buf[48];
        std::snprintf(buf, sizeof buf, "OK GETPARAM %.9g ", normalize(*meta, plain));
        return std::string(buf) + AestraHelper::hexOf(text);
    }

    // ---- The plugin's window (V82-H3, CLAP side) ----

    bool openGuiX11(const std::function<unsigned long(int, int, bool)>& makeWindow,
                    Aestra::Audio::EditorLoopPump& pump, std::function<void(int, int)> requestResize) {
        if (!plugin || !guiExt || guiOpen || !makeWindow || !guiExt->isApiSupported || !guiExt->create ||
            !guiExt->getSize || !guiExt->setParent || !guiExt->show)
            return false;
        if (!guiExt->isApiSupported(plugin, kClapWindowApiX11, false)) return false;
        (void)pump; // a CLAP plugin's timers and descriptors run on the module's own pump, serviced with the window
        {
            std::lock_guard<std::mutex> lock(hostContext.mutex);
            hostContext.requestResize = std::move(requestResize);
        }
        if (!guiExt->create(plugin, kClapWindowApiX11, false)) return false;
        uint32_t width = 0, height = 0;
        if (!guiExt->getSize(plugin, &width, &height) || width == 0 || height == 0) {
            width = 800;
            height = 600;
        }
        const bool resizable = guiExt->canResize && guiExt->canResize(plugin);
        const unsigned long window = makeWindow(static_cast<int>(width), static_cast<int>(height), resizable);
        const ClapWindow parent = {kClapWindowApiX11, window};
        if (window == 0 || !guiExt->setParent(plugin, &parent)) {
            guiExt->destroy(plugin);
            return false;
        }
        guiExt->show(plugin);
        guiOpen = true;
        return true;
    }

    void closeGui() {
        if (!guiOpen || !plugin || !guiExt) return;
        if (guiExt->hide) guiExt->hide(plugin);
        guiExt->destroy(plugin);
        {
            std::lock_guard<std::mutex> lock(hostContext.mutex);
            hostContext.requestResize = nullptr;
        }
        guiOpen = false;
    }

    bool canResizeGui() const { return guiOpen && guiExt && guiExt->canResize && guiExt->canResize(plugin); }
    void resizeGui(int width, int height) {
        if (!guiOpen || !guiExt || !guiExt->setSize) return;
        guiExt->setSize(plugin, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
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
        // Parameter values waiting for the plugin ride in first, at the start of the block. A producer holding the lock
        // just means they go in next block.
        paramEvents.clear();
        {
            std::unique_lock<std::mutex> lock(paramMutex, std::try_to_lock);
            if (lock.owns_lock()) {
                while (!pendingParams.empty() && paramEvents.size() < kMaxParamEventsPerBlock) {
                    ClapEventParamValue event{};
                    event.header.size = sizeof(event);
                    event.header.time = 0;
                    event.header.spaceId = kClapCoreEventSpaceId;
                    event.header.type = kClapEventParamValue;
                    event.header.flags = 0;
                    event.paramId = pendingParams.front().first;
                    event.cookie = nullptr;
                    event.noteId = -1;
                    event.portIndex = -1;
                    event.channel = -1;
                    event.key = -1;
                    event.value = pendingParams.front().second;
                    paramEvents.push_back(event);
                    pendingParams.erase(pendingParams.begin());
                }
            }
        }
        for (const auto& event : paramEvents) noteEventOrder.push_back(&event.header);
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
        // The app's tempo and playhead for this block, which the PROCESS line carried and main() published.
        ClapEventTransport transport = {};
        const auto tv = Aestra::Audio::ClapTransportValues::from(Aestra::Audio::HostTransportClock::current());
        if (tv.flags != 0) {
            transport.header = {sizeof(transport), 0, kClapCoreEventSpaceId, kClapEventTransport, 0};
            transport.flags = tv.flags;
            transport.songPosBeats = tv.songPosBeats;
            transport.songPosSeconds = tv.songPosSeconds;
            transport.tempo = tv.tempo;
            transport.loopStartBeats = tv.loopStartBeats;
            transport.loopEndBeats = tv.loopEndBeats;
            transport.loopStartSeconds = tv.loopStartSeconds;
            transport.loopEndSeconds = tv.loopEndSeconds;
            transport.barStart = tv.barStart;
            transport.barNumber = tv.barNumber;
            transport.tsigNum = tv.tsigNum;
            transport.tsigDenom = tv.tsigDenom;
        }
        processData.transport = tv.flags != 0 ? &transport : nullptr;
        processData.audioInputs = &inputBuffer;
        processData.audioOutputs = &outputBuffer;
        processData.audioInputsCount = 1;
        processData.audioOutputsCount = 1;
        processData.inEvents = noteEventOrder.empty() ? &emptyInputEvents : &midiInputEvents;
        processData.outEvents = &outputEvents;

        outCollector.values.clear();
        ClapHostContext::inProcess() = true;
        const ClapProcessStatus status = plugin->process(plugin, &processData);
        ClapHostContext::inProcess() = false;
        if (status == kClapProcessError) {
            return false;
        }
        // What the plugin moved itself (its window's knobs): told to whoever listens, normalized.
        if (onParamOut)
            for (const auto& v : outCollector.values)
                if (const ParamMeta* meta = metaOf(v.first)) onParamOut(v.first, normalize(*meta, v.second));
        if (hostContext.callbackRequested.exchange(false, std::memory_order_acq_rel) && plugin->onMainThread)
            plugin->onMainThread(plugin);

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

    /// Every plugin a bundle holds, not just the first: one .clap file can hold hundreds (LSP holds about 200). The
    /// descriptors stay valid until close(). Entries without an id are skipped.
    bool listDescriptors(const std::string& path, std::vector<const ClapPluginDescriptor*>& out, std::string& error) {
        out.clear();
        const ClapPluginDescriptor* first = nullptr;
        if (!scanMetadata(path, first, error)) return false;
        const auto* factory = static_cast<const ClapPluginFactory*>(entry->getFactory(kClapPluginFactoryId));
        const uint32_t count = factory->getPluginCount(factory);
        for (uint32_t i = 0; i < count; ++i) {
            const ClapPluginDescriptor* d = factory->getPluginDescriptor(factory, i);
            if (d && d->id && d->id[0] != '\0') out.push_back(d);
        }
        if (out.empty()) {
            error = "invalid-clap-descriptor";
            close();
            return false;
        }
        return true;
    }

    bool scanMetadata(const std::string& path, const ClapPluginDescriptor*& descriptor, std::string& error) {
        descriptor = nullptr;
        close();
        library = AestraHelper::openLibrary(path);
        if (!library) {
            error = "clap-load-failed";
            return false;
        }

        entry = reinterpret_cast<const ClapPluginEntry*>(AestraHelper::librarySymbol(library, "clap_entry"));
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

#if defined(AESTRA_HAS_X11)
/// The CLAP plugin's editor, for EditorHost. The module outlives the window (the helper closes the window first).
inline AestraHelper::EditorBackend clapEditorBackend(ClapModule& module) {
    AestraHelper::EditorBackend b;
    b.open = [&module](const AestraHelper::EditorBackend::MakeWindow& makeWindow, Aestra::Audio::EditorLoopPump& pump,
                       std::function<void(int, int)> resize) { return module.openGuiX11(makeWindow, pump, std::move(resize)); };
    b.close = [&module] { module.closeGui(); };
    b.canResize = [&module] { return module.canResizeGui(); };
    b.resize = [&module](int w, int h) { module.resizeGui(w, h); };
    return b;
}
#endif

} // namespace

