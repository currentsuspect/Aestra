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

// clap_event_transport ABI (clap/events.h): what process() is told about tempo and the playhead. Times are fixed point
// (CLAP_BEATTIME_FACTOR, CLAP_SECTIME_FACTOR = 2^31).
constexpr uint16_t kClapEventTransport = 9;
struct ClapEventTransport {
    ClapEventHeader header;
    uint32_t flags;
    int64_t songPosBeats;
    int64_t songPosSeconds;
    double tempo;
    double tempoInc;
    int64_t loopStartBeats;
    int64_t loopEndBeats;
    int64_t loopStartSeconds;
    int64_t loopEndSeconds;
    int64_t barStart;
    int32_t barNumber;
    uint16_t tsigNum;
    uint16_t tsigDenom;
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
constexpr uint16_t kClapEventParamGestureBegin = 7;
constexpr uint16_t kClapEventParamGestureEnd = 8;

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

// clap_param_info ABI (clap/ext/params.h). Layout checked against the SDK header: id, flags, cookie, name[256],
// module[1024], then three doubles. (An earlier copy of this struct carried fields the real one does not have.)
struct ClapParamInfo {
    uint32_t id;
    uint32_t flags;
    void* cookie;
    char name[256];
    char module[1024];
    double minValue;
    double maxValue;
    double defaultValue;
};

constexpr uint32_t kClapParamIsStepped = (1u << 0);
constexpr uint32_t kClapParamIsPeriodic = (1u << 1);
constexpr uint32_t kClapParamIsHidden = (1u << 2);
constexpr uint32_t kClapParamIsReadOnly = (1u << 3);
constexpr uint32_t kClapParamIsBypass = (1u << 4);
constexpr uint32_t kClapParamIsAutomatable = (1u << 5);

struct ClapPluginParams {
    uint32_t (*count)(const ClapPlugin* plugin);
    bool (*getInfo)(const ClapPlugin* plugin, uint32_t paramIndex, ClapParamInfo* info);
    bool (*getValue)(const ClapPlugin* plugin, uint32_t paramId, double* outValue);
    bool (*valueToText)(const ClapPlugin* plugin, uint32_t paramId, double value, char* outBuffer, uint32_t capacity);
    bool (*textToValue)(const ClapPlugin* plugin, uint32_t paramId, const char* text, double* outValue);
    void (*flush)(const ClapPlugin* plugin, const ClapInputEvents* in, const ClapOutputEvents* out);
};

// Host side of clap.params: the plugin tells us its parameters changed or wants a flush.
struct ClapHostParams {
    void (*rescan)(const ClapHost* host, uint32_t flags);
    void (*clear)(const ClapHost* host, uint32_t paramId, uint32_t flags);
    void (*requestFlush)(const ClapHost* host);
};
constexpr uint32_t kClapParamRescanValues = (1u << 0);
constexpr uint32_t kClapParamRescanAll = (1u << 3);

// clap.thread-check, clap.latency, clap.state: what a plugin asks the host (host side).
struct ClapHostThreadCheck {
    bool (*isMainThread)(const ClapHost* host);
    bool (*isAudioThread)(const ClapHost* host);
};
struct ClapHostLatency {
    void (*changed)(const ClapHost* host);
};
struct ClapHostState {
    void (*markDirty)(const ClapHost* host);
};
constexpr const char* kClapExtThreadCheck = "clap.thread-check";
constexpr const char* kClapExtHostLatency = "clap.latency";
constexpr const char* kClapExtHostState = "clap.state";
constexpr const char* kClapExtHostParams = "clap.params";

// Plugin editors (V82-H3, CLAP side): clap.gui with the X11 window API, and the two extensions an X11 editor needs the
// host to run for it: timers and file descriptors.
constexpr const char* kClapExtGui = "clap.gui";
constexpr const char* kClapExtTimerSupport = "clap.timer-support";
constexpr const char* kClapExtPosixFdSupport = "clap.posix-fd-support";
constexpr const char* kClapWindowApiX11 = "x11";

struct ClapWindow {
    const char* api;
    unsigned long x11; // the union's x11 member; every member is pointer-sized or smaller, so this is the whole union
};

struct ClapGuiResizeHints {
    bool canResizeHorizontally;
    bool canResizeVertically;
    bool preserveAspectRatio;
    uint32_t aspectRatioWidth;
    uint32_t aspectRatioHeight;
};

struct ClapPluginGui {
    bool (*isApiSupported)(const ClapPlugin* plugin, const char* api, bool isFloating);
    bool (*getPreferredApi)(const ClapPlugin* plugin, const char** api, bool* isFloating);
    bool (*create)(const ClapPlugin* plugin, const char* api, bool isFloating);
    void (*destroy)(const ClapPlugin* plugin);
    bool (*setScale)(const ClapPlugin* plugin, double scale);
    bool (*getSize)(const ClapPlugin* plugin, uint32_t* width, uint32_t* height);
    bool (*canResize)(const ClapPlugin* plugin);
    bool (*getResizeHints)(const ClapPlugin* plugin, ClapGuiResizeHints* hints);
    bool (*adjustSize)(const ClapPlugin* plugin, uint32_t* width, uint32_t* height);
    bool (*setSize)(const ClapPlugin* plugin, uint32_t width, uint32_t height);
    bool (*setParent)(const ClapPlugin* plugin, const ClapWindow* window);
    bool (*setTransient)(const ClapPlugin* plugin, const ClapWindow* window);
    void (*suggestTitle)(const ClapPlugin* plugin, const char* title);
    bool (*show)(const ClapPlugin* plugin);
    bool (*hide)(const ClapPlugin* plugin);
};

struct ClapHostGui {
    void (*resizeHintsChanged)(const ClapHost* host);
    bool (*requestResize)(const ClapHost* host, uint32_t width, uint32_t height);
    bool (*requestShow)(const ClapHost* host);
    bool (*requestHide)(const ClapHost* host);
    void (*closed)(const ClapHost* host, bool wasDestroyed);
};

struct ClapPluginTimerSupport {
    void (*onTimer)(const ClapPlugin* plugin, uint32_t timerId);
};
struct ClapHostTimerSupport {
    bool (*registerTimer)(const ClapHost* host, uint32_t periodMs, uint32_t* timerId);
    bool (*unregisterTimer)(const ClapHost* host, uint32_t timerId);
};

constexpr uint32_t kClapPosixFdRead = (1u << 0);
constexpr uint32_t kClapPosixFdWrite = (1u << 1);
constexpr uint32_t kClapPosixFdError = (1u << 2);
struct ClapPluginPosixFdSupport {
    void (*onFd)(const ClapPlugin* plugin, int fd, uint32_t flags);
};
struct ClapHostPosixFdSupport {
    bool (*registerFd)(const ClapHost* host, int fd, uint32_t flags);
    bool (*modifyFd)(const ClapHost* host, int fd, uint32_t flags);
    bool (*unregisterFd)(const ClapHost* host, int fd);
};

constexpr const char* kClapExtState = "clap.state";
constexpr const char* kClapExtParams = "clap.params";

} // namespace ClapAbi
} // namespace AestraHelper
