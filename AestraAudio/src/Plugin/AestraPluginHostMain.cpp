// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "HelperEditorHost.h"
#include "HelperLineReader.h"
#include "HelperParamCommands.h"
#include "SharedAudioBlock.h"
#include "Plugin/HostTransport.h"
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

#include "Plugin/PluginHostTestHooks.h"
#ifdef AESTRA_HAS_VST3
#include "Plugin/VST3Host.h"
#endif

// CLAP hosting in this child: Linux, macOS and Windows (HelperDynLib.h).
#include "HelperClapModule.h"

#if defined(AESTRA_ENABLE_TEST_HOOKS) && !defined(_WIN32)
#if defined(__linux__)
#include <dirent.h>
#else
#include <libproc.h>
#endif
#include <unistd.h>
#endif

namespace {

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

    /// IAudioProcessor::getLatencySamples(): the plugin's own delay, valid after setup and activation.
    uint32_t latencySamples() const { return plugin ? plugin->getLatencySamples() : 0; }

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
    std::vector<float> processed; // kept across blocks so a block does not allocate
    Aestra::Audio::SharedAudioBlock sharedBlock;
    std::array<std::string, 10> transportFields; // kept across blocks so a block does not allocate
    std::string line;
#ifdef AESTRA_ENABLE_TEST_HOOKS
    // The __aestra_test_echo__ fake plugin has no real module; it records applied
    // parameters so tests can prove host->child parameter delivery end to end.
    // Parameter 0 is treated as an output gain applied to the echoed audio, so a
    // test can observe the change behaviorally rather than trusting a log (#238).
    bool echoPlugin = false;
    // Like LSP's limiters, the echo fake only knows its own latency after it has processed a block (LATENCY command).
    bool echoHasProcessed = false;
    std::array<float, 16> echoParams{};
    echoParams.fill(0.0f);
    echoParams[0] = 1.0f; // unity gain until set
#endif
    ClapModule clapModule;
#ifdef AESTRA_HAS_VST3
    Vst3Module vst3Module;
#endif
#if defined(AESTRA_HELPER_HAS_EDITORS)
    AestraHelper::EditorHost editorHost;
#endif
    AestraHelper::EditLog editLog;

    AestraHelper::LineReader lineReader;
    // The next command. The same thread runs the plugin's timers and an open editor between commands
    // (HelperLineReader.h).
    auto readCommand = [&](std::string& out) -> bool {
#ifdef _WIN32
        return lineReader.next(
            out,
            [&](int& timeoutMs) {
                clapModule.hostContext.limitWait(timeoutMs);
                editorHost.limitWait(timeoutMs);
            },
            [&] {
                clapModule.hostContext.service();
                editorHost.service();
            });
#else
        return lineReader.next(
            out,
            [&](std::vector<pollfd>& fds, int& timeoutMs) {
                clapModule.hostContext.pollFds(fds, timeoutMs);
#if defined(AESTRA_HELPER_HAS_EDITORS)
                editorHost.pollFds(fds, timeoutMs);
#endif
            },
            [&] {
                clapModule.hostContext.service();
#if defined(AESTRA_HELPER_HAS_EDITORS)
                editorHost.service();
#endif
            }
        );
#endif
    };

    while (readCommand(line)) {
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
        } else if (command == "SCANALL") {
            // Like SCAN, but every plugin in the bundle: records "id,name,vendor,version,features" (each hex), joined by ';'.
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
            std::vector<const ClapPluginDescriptor*> descriptors;
            std::string error;
            if (!clapModule.listDescriptors(path, descriptors, error)) {
                reply("ERR " + error);
                continue;
            }
            std::string records;
            for (const ClapPluginDescriptor* d : descriptors) {
                std::string features;
                if (d->features) {
                    for (const char* const* f = d->features; *f; ++f) {
                        if (!features.empty()) features += "|";
                        features += *f;
                    }
                }
                if (!records.empty()) records += ";";
                records += hexEncodeString(d->id) + "," + hexEncodeString(d->name ? d->name : "") + "," +
                           hexEncodeString(d->vendor ? d->vendor : "") + "," + hexEncodeString(d->version ? d->version : "") + "," +
                           hexEncodeString(features);
            }
            reply("OK " + records);
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
        } else if (command == "LOAD") {
#if defined(AESTRA_HAS_VST3) && defined(AESTRA_HELPER_HAS_EDITORS)
            editorHost.close(); // the old plugin's window must go before its module does
#endif
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
            }
            if (format == "clap" && id != "__aestra_test_echo__") {
#ifdef AESTRA_HAS_VST3
                vst3Module.close();
#endif
                std::string error;
                if (!clapModule.load(path, id, error)) {
                    reply("ERR " + error);
                    continue;
                }
                clapModule.onParamOut = [&editLog](uint32_t paramId, double v) { editLog.add(paramId, v); };
            } else {
                clapModule.close();
            }
#ifdef AESTRA_HAS_VST3
            if (format == "vst3" && id != "__aestra_test_echo__") {
                clapModule.close();
                std::string error;
                if (!vst3Module.load(path, id, error)) {
                    reply("ERR " + error);
                    continue;
                }
                vst3Module.plugin->setEditListener([&editLog](uint32_t paramId, double v) { editLog.add(paramId, v); });
            } else {
                vst3Module.close();
            }
#endif
            loaded = true;
#ifdef AESTRA_ENABLE_TEST_HOOKS
            echoPlugin = (id == "__aestra_test_echo__");
            echoHasProcessed = false;
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
            if (clapModule.plugin) {
                std::string error;
                if (!clapModule.initialize(sampleRate, maxBlockSize, error)) {
                    reply("ERR " + error);
                    continue;
                }
            }
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
        } else if (command == "EDITOR") {
            // V82-H3: EDITOR OPEN <titleHex> -> OK EDITOR <w> <h> | ERR; EDITOR CLOSE; EDITOR STATE -> OK EDITOR OPEN|CLOSED
            std::string sub;
            input >> sub;
#if defined(AESTRA_HELPER_HAS_EDITORS)
            if (sub == "OPEN") {
                std::string titleHex;
                input >> titleHex;
                int w = 0, h = 0;
                AestraHelper::EditorBackend backend;
#ifdef AESTRA_HAS_VST3
                if (vst3Module.plugin) backend = AestraHelper::vst3EditorBackend(vst3Module.plugin);
#endif
                if (clapModule.plugin) backend = clapEditorBackend(clapModule);
                if (!editorHost.open(std::move(backend), hexDecode(titleHex), w, h)) reply("ERR no-editor");
                else reply("OK EDITOR " + std::to_string(w) + " " + std::to_string(h));
            } else if (sub == "CLOSE") {
                editorHost.close();
                reply("OK EDITOR CLOSED");
            } else if (sub == "STATE") {
                reply(editorHost.isOpen() ? "OK EDITOR OPEN " + std::to_string(editorHost.width()) + " " + std::to_string(editorHost.height())
                                          : "OK EDITOR CLOSED");
            } else {
                reply("ERR invalid-editor-request");
            }
#else
            reply(sub == "OPEN" ? "ERR no-editor" : "OK EDITOR CLOSED"); // no X11: nothing to open, so nothing is ever open
#endif
        // One branch per command for both formats. A VST3-only copy above this used
        // to answer first whenever VST3 was built in, so every CLAP plugin reported
        // no parameters through the sandbox (V82-H7).
        } else if (command == "PARAMS") {
            std::string list = "OK 0";
#ifdef AESTRA_HAS_VST3
            if (vst3Module.plugin) list = AestraHelper::paramsReply(*vst3Module.plugin);
#endif
            if (clapModule.plugin) list = clapModule.paramsReply();
            reply(list);
        } else if (command == "GETPARAM") {
            uint32_t paramId = 0;
            std::string answer = "ERR no-plugin";
            if (input >> paramId) {
#ifdef AESTRA_HAS_VST3
                if (vst3Module.plugin) answer = AestraHelper::getParamReply(*vst3Module.plugin, paramId);
#endif
                if (clapModule.plugin) answer = clapModule.getParamReply(paramId);
            }
            reply(answer);
        } else if (command == "EDITS") {
            reply(editLog.take());
        } else if (command == "LATENCY") {
            // The plugin's own latency in samples (V82-H2). The app adds the sandbox's one block of transport delay.
            uint32_t pluginLatency = 0;
#ifdef AESTRA_ENABLE_TEST_HOOKS
            if (echoPlugin) pluginLatency = echoHasProcessed ? Aestra::Audio::kEchoTestLatencySamples : 0;
#endif
            if (clapModule.plugin) pluginLatency = clapModule.latencySamples();
#ifdef AESTRA_HAS_VST3
            if (vst3Module.plugin) pluginLatency = vst3Module.latencySamples();
#endif
            reply("OK LATENCY " + std::to_string(pluginLatency));
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
            clapModule.deactivate();
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
            if (clapModule.plugin) {
                reply(clapModule.setParameter(paramId, value) ? "OK SETPARAM" : "ERR setparam-failed");
                continue;
            }
#ifdef AESTRA_HAS_VST3
            if (vst3Module.plugin) {
                vst3Module.plugin->setParameter(paramId, static_cast<float>(value));
                reply("OK SETPARAM");
                continue;
            }
#endif
            reply("ERR no-plugin");
#if defined(AESTRA_ENABLE_TEST_HOOKS)
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
#if defined(AESTRA_ENABLE_TEST_HOOKS) && !defined(_WIN32)
        } else if (command == "TESTFDS") {
            // Report own open descriptors for the #710 regression test:
            // "OK <n>" followed by the sorted fd numbers. This runs post-exec
            // in a normal process, so allocation here is fine (unlike the
            // fork-child close path, which must stay async-signal-safe).
            std::vector<int> fds;
#if defined(__linux__)
            if (DIR* dir = opendir("/proc/self/fd")) {
                const int dirFd = dirfd(dir);
                dirent* entry = nullptr;
                while ((entry = readdir(dir)) != nullptr) {
                    if (entry->d_name[0] == '.') {
                        continue;
                    }
                    const int fd = atoi(entry->d_name);
                    if (fd != dirFd) {
                        fds.push_back(fd);
                    }
                }
                closedir(dir);
            }
#else
            const int needed =
                proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, nullptr, 0);
            if (needed > 0) {
                std::vector<char> buffer(static_cast<size_t>(needed));
                const int got = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, buffer.data(),
                                             static_cast<int>(buffer.size()));
                const size_t count = got > 0 ? static_cast<size_t>(got) / sizeof(proc_fdinfo) : 0;
                const auto* infos = reinterpret_cast<const proc_fdinfo*>(buffer.data());
                for (size_t i = 0; i < count; ++i) {
                    fds.push_back(static_cast<int>(infos[i].proc_fd));
                }
            }
#endif
            std::sort(fds.begin(), fds.end());
            std::ostringstream out;
            out << "OK " << fds.size();
            for (int fd : fds) {
                out << ' ' << fd;
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
            if (clapModule.plugin) {
                stateBuffer.clear();
                if (!clapModule.saveState(stateBuffer)) {
                    reply("ERR state-unavailable");
                    continue;
                }
                reply("OK " + hexEncodeBytes(stateBuffer.data(), stateBuffer.size()));
                continue;
            }
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
            if (clapModule.plugin) {
                if (!clapModule.loadState(stateBuffer)) {
                    reply("ERR state-load-failed");
                    continue;
                }
                reply("OK STATE");
                continue;
            }
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
#if defined(AESTRA_HAS_VST3) && defined(AESTRA_HELPER_HAS_EDITORS)
            editorHost.close();
#endif
            active = false;
            loaded = false;
            clapModule.close();
#ifdef AESTRA_HAS_VST3
            vst3Module.close();
#endif
            reply("OK SHUTDOWN");
        } else if (command == "EXIT") {
            clapModule.close();
#ifdef AESTRA_HAS_VST3
            vst3Module.close();
#endif
            reply("OK EXIT");
            return 0;
        } else if (command == "STATUS") {
            reply(active ? "OK ACTIVE" : (loaded ? "OK LOADED" : "OK EMPTY"));
        } else if (command == "SHM") {
            // The app's shared block for audio (SharedAudioBlock.h). Saying no is safe: the app stays on hex.
            std::string nameHex;
            size_t maxSamples = 0;
            size_t maxMidiBytes = 0;
            input >> nameHex >> maxSamples >> maxMidiBytes;
            const std::string name = hexDecode(nameHex);
            if (name.empty() || maxSamples == 0 || maxSamples > 2u * 65536u || maxMidiBytes > (1u << 20) ||
                !sharedBlock.open(name, maxSamples, maxMidiBytes)) {
                reply("ERR shm");
                continue;
            }
            reply("OK SHM");
        } else if (command == "PROCESS" || command == "PROCESSMIDI" || command == "PROCESSSHM") {
            if (!loaded || !active) {
                reply("ERR inactive");
                continue;
            }
            // PROCESSSHM carries only the sizes: the samples and the MIDI are in the shared block, and so is the answer.
            const bool viaShm = command == "PROCESSSHM";
            uint32_t channels = 0;
            uint32_t frames = 0;
            input >> channels >> frames;
            if (channels == 0 || channels > 2 || frames == 0 || frames > maxBlockSize) {
                reply("ERR invalid-process-size");
                continue;
            }
            const size_t sampleCount = static_cast<size_t>(channels) * frames;
            midiBuffer.clear();
            if (viaShm) {
                size_t midiBytes = 0;
                input >> midiBytes;
                if (!sharedBlock.isOpen() || sampleCount > sharedBlock.maxSamples() ||
                    midiBytes > sharedBlock.maxMidiBytes()) {
                    reply("ERR invalid-shm-process");
                    continue;
                }
                processBuffer.assign(sharedBlock.input(), sharedBlock.input() + sampleCount);
                midiBuffer.assign(sharedBlock.midi(), sharedBlock.midi() + midiBytes);
            } else {
                std::string payloadHex;
                std::string midiHex;
                input >> payloadHex;
                if (command == "PROCESSMIDI") {
                    input >> midiHex;
                }
                if (!hexDecodeFloats(payloadHex, processBuffer) || processBuffer.size() < sampleCount) {
                    reply("ERR invalid-process-payload");
                    continue;
                }
                if (command == "PROCESSMIDI" && !hexDecodeRaw(midiHex, midiBuffer)) {
                    reply("ERR invalid-midi-payload");
                    continue;
                }
            }
            // The app's tempo and playhead for this block ("T ..." after the payload, HostTransport.h). The plugin hosts
            // below read it from the clock; a line without one, or a malformed one, means no transport this block.
            {
                using Aestra::Audio::HostTransport;
                std::string marker;
                size_t fieldCount = 0;
                if (input >> marker && marker == "T") {
                    while (fieldCount < transportFields.size() && input >> transportFields[fieldCount])
                        ++fieldCount;
                    std::string extra;
                    if (input >> extra)
                        fieldCount = 0;
                }
                Aestra::Audio::HostTransportClock::publish(
                    marker == "T" ? HostTransport::fromWire(transportFields.data(), fieldCount) : HostTransport{});
            }
            auto answer = [&](std::vector<float>& out) {
                if (out.size() < sampleCount) {
                    out.resize(sampleCount, 0.0f);
                }
                if (viaShm) {
                    std::copy_n(out.data(), sampleCount, sharedBlock.output());
                    reply("OK");
                } else {
                    reply("OK " + hexEncodeBytes(out.data(), sampleCount * sizeof(float)));
                }
            };

            if (clapModule.plugin) {
                const std::vector<uint8_t>* midiData = midiBuffer.empty() ? nullptr : &midiBuffer;
                if (!clapModule.process(processBuffer, channels, frames, midiData, midiBuffer.size(), processed)) {
                    reply("ERR clap-process-failed");
                    continue;
                }
                answer(processed);
                continue;
            }
#ifdef AESTRA_HAS_VST3
            if (vst3Module.plugin) {
                const std::vector<uint8_t>* midiData = midiBuffer.empty() ? nullptr : &midiBuffer;
                if (!vst3Module.process(processBuffer, channels, frames, midiData, midiBuffer.size(), processed)) {
                    reply("ERR vst3-process-failed");
                    continue;
                }
                answer(processed);
                continue;
            }
#endif

#ifdef AESTRA_ENABLE_TEST_HOOKS
            // Echo fake plugin: apply parameter 0 as an output gain so a test can
            // observe host->child parameter delivery through the audio itself (#238).
            if (echoPlugin) echoHasProcessed = true;
            if (echoPlugin && echoParams[0] != 1.0f) {
                for (size_t i = 0; i < sampleCount && i < processBuffer.size(); ++i) {
                    processBuffer[i] *= echoParams[0];
                }
            }
#endif
            answer(processBuffer);
        } else {
            reply("ERR unknown-command");
        }
    }

    return 0;
}
