// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// MuseRepl — the stdin/stdout entry point to Aestra's agentic runtime.
//
// One JSON request per input line, one JSON response per output line
// (JSONL both ways). Everything goes through MuseService: the same schema
// validation, command factories, and undo history the UI uses. This is how
// an external agent (or a human with a pipe) drives a headless session:
//
//   $ MuseRepl <<'EOF'
//   {"id": 1, "verb": "set_bpm", "args": {"value": 142}}
//   {"id": 2, "verb": "add_track", "args": {"name": "Drums"}}
//   {"id": 3, "verb": "list_tracks"}
//   EOF
//
// Flags:
//   --schema      print the MuseGrammar command schema as JSON and exit
//                 (the agent's tool manifest)
//   --sample-rate <hz>  engine sample rate (default 48000)

#include "AudioEngine.h"
#include "Commands/CommandRegistry.h"
#include "Commands/MuseGrammar.h"
#include "Commands/MuseService.h"
#include "Commands/MuseSocketServer.h"
#include "Plugin/PluginManager.h"
#include "TrackManager.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

using namespace Aestra::Audio;

int main(int argc, char** argv) {
    uint32_t sampleRate = 48000;

    int socketPort = -1; // -1 = stdin/stdout mode

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            const long parsed = std::strtol(argv[++i], nullptr, 10);
            if (parsed >= 0 && parsed <= 65535) {
                socketPort = static_cast<int>(parsed);
            } else {
                std::cerr << "invalid --port\n";
                return 2;
            }
            continue;
        }
        if (arg == "--schema") {
            std::cout << MuseGrammar::schemaToJsonString();
            return 0;
        }
        if (arg == "--sample-rate" && i + 1 < argc) {
            const long parsed = std::strtol(argv[++i], nullptr, 10);
            if (parsed >= 8000 && parsed <= 192000) {
                sampleRate = static_cast<uint32_t>(parsed);
            } else {
                std::cerr << "invalid --sample-rate, using 48000\n";
            }
            continue;
        }
        if (arg == "--help" || arg == "-h") {
            std::cout << "MuseRepl: JSONL in on stdin, JSONL out on stdout.\n"
                         "  --schema             print command schema JSON and exit\n"
                         "  --sample-rate <hz>   engine sample rate (default 48000)\n"
                         "  --port <port>        serve JSONL on 127.0.0.1:<port> instead of\n"
                         "                       stdin/stdout (0 = ephemeral, printed)\n";
            return 0;
        }
        std::cerr << "unknown argument: " << arg << "\n";
        return 2;
    }

    // Session setup mirrors the app's wiring: TrackManager owns the model and
    // history, AudioEngine owns transport/tempo, the registry binds both.
    // Built-in plugins (the sampler load_sample instantiates) register inside
    // PluginManager::initialize(); without it units stay silent.
    if (!PluginManager::getInstance().initialize()) {
        std::cerr << "failed to initialize plugin manager\n";
        return 1;
    }

    auto trackManager = std::make_shared<TrackManager>();
    trackManager->setOutputSampleRate(static_cast<double>(sampleRate));
    trackManager->setInputSampleRate(static_cast<double>(sampleRate));
    trackManager->setInputChannelCount(0);
    // The app wires this in AestraContent; without it, creating a unit does
    // not create its default MIDI pattern.
    trackManager->getUnitManager().setPatternManager(&trackManager->getPatternManager());

    AudioEngine engine;
    engine.setSampleRate(sampleRate);
    // Must cover AudioExporter's 4096-frame render blocks (render_song):
    // processBlock does not split blocks larger than the configured maximum.
    engine.setBufferConfig(4096, 2);
    MuseService::wireHeadlessEngine(trackManager, engine);
    if (!engine.initialize()) {
        std::cerr << "failed to initialize audio engine\n";
        return 1;
    }

    CommandRegistry::initialize(trackManager.get());
    CommandRegistry::setAudioEngine(&engine);

    MuseService service(trackManager.get(), &engine);

    // Socket mode: the same headless session served over localhost — what
    // muse-agent connects to when no GUI is running.
    if (socketPort >= 0) {
        MuseSocketServer server;
        std::string error;
        if (!server.start(static_cast<uint16_t>(socketPort), error)) {
            std::cerr << "failed to start socket server: " << error << "\n";
            return 1;
        }
        std::cout << "muse-port " << server.port() << std::endl;
        while (server.isRunning()) {
            if (server.processPending(service) == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        return 0;
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        // Skip blank lines so hand-driven sessions can space things out.
        bool blank = true;
        for (char c : line) {
            if (!std::isspace(static_cast<unsigned char>(c))) {
                blank = false;
                break;
            }
        }
        if (blank) continue;

        std::cout << service.handleRequest(line) << "\n";
        std::cout.flush();
    }

    return 0;
}
