// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The part of the CLAP ABI the sandbox helper speaks, copied by hand so AestraPluginHost links no CLAP SDK (it stays a
// small, self-contained process). Layout only: every struct mirrors clap/*.h, field for field. A new extension the helper
// needs (clap.gui, clap.timer-support, clap.posix-fd-support for plugin editors) is mirrored here, not in the .cpp.

#include <cstdint>

namespace AestraHelper {
namespace ClapAbi {

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

// clap.latency: the plugin's own processing delay in samples (V82-H2). Valid once the plugin is activated.
struct ClapPluginLatency {
    uint32_t (*get)(const ClapPlugin* plugin);
};
constexpr const char* kClapExtLatency = "clap.latency";

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

} // namespace ClapAbi
} // namespace AestraHelper
