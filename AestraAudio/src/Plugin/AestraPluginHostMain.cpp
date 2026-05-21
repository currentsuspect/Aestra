// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include <algorithm>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace {

#ifndef _WIN32
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
constexpr uint16_t kClapEventMidi = 0;

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

    ~ClapModule() { close(); }

    ClapModule() = default;
    ClapModule(const ClapModule&) = delete;
    ClapModule& operator=(const ClapModule&) = delete;

    void close() {
        deactivate();
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
        }
        return true;
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
        midiEvents.reserve(1024);
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

        midiEvents.clear();
        if (midiData && midiBytes <= midiData->size() && (midiBytes % 8) == 0) {
            for (size_t offset = 0; offset < midiBytes; offset += 8) {
                const uint8_t size = (*midiData)[offset + 4];
                if (size != 3) {
                    continue;
                }
                uint32_t sampleOffset = 0;
                std::memcpy(&sampleOffset, midiData->data() + offset, sizeof(sampleOffset));
                ClapEventMidi event{};
                event.header.size = sizeof(event);
                event.header.time = std::min<uint32_t>(sampleOffset, frames > 0 ? frames - 1 : 0);
                event.header.spaceId = kClapCoreEventSpaceId;
                event.header.type = kClapEventMidi;
                event.header.flags = 0;
                event.portIndex = 0;
                std::memcpy(event.data, midiData->data() + offset + 5, 3);
                midiEvents.push_back(event);
            }
        }
        ClapInputEvents midiInputEvents = {&midiEvents, midiInputEventsSize, midiInputEventsGet};

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
        processData.inEvents = midiEvents.empty() ? &emptyInputEvents : &midiInputEvents;
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

    installFatalSignalHandlers();

    bool loaded = false;
    bool active = false;
    uint32_t maxBlockSize = 0;
    std::vector<float> processBuffer;
    std::vector<uint8_t> midiBuffer;
    std::vector<uint8_t> stateBuffer;
    std::string line;
#ifndef _WIN32
    ClapModule clapModule;
#endif

    while (std::getline(std::cin, line)) {
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
#endif
            if (format != "vst3" && format != "clap") {
                reply("ERR unsupported-format");
                continue;
            }
            if (id.empty() || path.empty()) {
                reply("ERR invalid-load-request");
                continue;
            }
#ifndef _WIN32
            if (format == "clap" && id != "__aestra_test_echo__") {
                std::string error;
                if (!clapModule.load(path, id, error)) {
                    reply("ERR " + error);
                    continue;
                }
            } else {
                clapModule.close();
            }
#endif
            loaded = true;
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
            reply("OK INIT");
        } else if (command == "ACTIVATE") {
            if (!loaded) {
                reply("ERR not-loaded");
                continue;
            }
            active = true;
            reply("OK ACTIVE");
        } else if (command == "DEACTIVATE") {
            active = false;
#ifndef _WIN32
            clapModule.deactivate();
#endif
            reply("OK INACTIVE");
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
            reply(stateBuffer.empty() ? "OK STATE" : "ERR state-unavailable");
        } else if (command == "SHUTDOWN") {
            active = false;
            loaded = false;
#ifndef _WIN32
            clapModule.close();
#endif
            reply("OK SHUTDOWN");
        } else if (command == "EXIT") {
#ifndef _WIN32
            clapModule.close();
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

            const size_t byteCount = static_cast<size_t>(channels) * frames * sizeof(float);
            reply("OK " + hexEncodeBytes(processBuffer.data(), byteCount));
        } else {
            reply("ERR unknown-command");
        }
    }

    return 0;
}
