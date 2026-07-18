// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file HeadlessExportMain.cpp
 * @brief CLI tool for headless music generation and export
 * 
 * Usage:
 *   ./AestraHeadlessExport --template Techno --duration 16 --output song.wav
 *   ./AestraHeadlessExport --tempo 130 --pattern "C-4,C-4,G-4,G-4" --output bassline.wav
 */

#include "../AestraAudio/include/Core/AudioEngine.h"
#include "../AestraAudio/include/Models/TrackManager.h"
#include "../AestraAudio/include/Headless/HeadlessMusicGenerator.h"
#include "../AestraCore/include/AestraLog.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstring>

using namespace Aestra;
using namespace Aestra::Audio;

struct CommandLineArgs {
    std::string templateName = "Techno";
    std::string outputPath = "output.wav";
    uint32_t durationBars = 8;
    double tempo = 130.0;
    uint32_t sampleRate = 48000;
    std::string bitDepth = "24";
    bool verbose = false;
    bool listTemplates = false;
};

void printUsage(const char* programName) {
    std::cout << "Aestra Headless Export - Programmatic Music Generator\n";
    std::cout << "Usage: " << programName << " [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --template <name>     Template name (Techno, House, Minimal, HipHop)\n";
    std::cout << "  --duration <bars>     Duration in bars (default: 8)\n";
    std::cout << "  --tempo <bpm>         Tempo in BPM (default: 130)\n";
    std::cout << "  --output <path>       Output file path (default: output.wav)\n";
    std::cout << "  --sample-rate <hz>    Sample rate (default: 48000)\n";
    std::cout << "  --bit-depth <bits>    Bit depth: 16, 24, or 32 (default: 24)\n";
    std::cout << "  --list-templates      List available templates\n";
    std::cout << "  --verbose             Enable verbose logging\n";
    std::cout << "  --help                Show this help message\n";
}

const char* requireValue(int argc, char* argv[], int& index, const std::string& option) {
    if (index + 1 >= argc || argv[index + 1] == nullptr || argv[index + 1][0] == '-') {
        throw std::invalid_argument("Missing value for " + option);
    }
    return argv[++index];
}

uint32_t parseUint32Option(const char* value, const std::string& option, uint32_t minValue, uint32_t maxValue) {
    size_t consumed = 0;
    unsigned long parsed = 0;
    try {
        parsed = std::stoul(value, &consumed, 10);
    } catch (const std::exception&) {
        throw std::invalid_argument("Invalid numeric value for " + option + ": " + value);
    }
    if (consumed != std::strlen(value) || parsed < minValue || parsed > maxValue) {
        throw std::invalid_argument("Value out of range for " + option + ": " + value);
    }
    return static_cast<uint32_t>(parsed);
}

double parseDoubleOption(const char* value, const std::string& option, double minValue, double maxValue) {
    size_t consumed = 0;
    double parsed = 0.0;
    try {
        parsed = std::stod(value, &consumed);
    } catch (const std::exception&) {
        throw std::invalid_argument("Invalid numeric value for " + option + ": " + value);
    }
    if (consumed != std::strlen(value) || !std::isfinite(parsed) || parsed < minValue || parsed > maxValue) {
        throw std::invalid_argument("Value out of range for " + option + ": " + value);
    }
    return parsed;
}

CommandLineArgs parseArgs(int argc, char* argv[]) {
    CommandLineArgs args;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            exit(0);
        } else if (arg == "--list-templates") {
            args.listTemplates = true;
        } else if (arg == "--verbose" || arg == "-v") {
            args.verbose = true;
        } else if (arg == "--template") {
            args.templateName = requireValue(argc, argv, i, arg);
        } else if (arg == "--duration") {
            args.durationBars = parseUint32Option(requireValue(argc, argv, i, arg), arg, 1, 100000);
        } else if (arg == "--tempo") {
            args.tempo = parseDoubleOption(requireValue(argc, argv, i, arg), arg, 20.0, 999.0);
        } else if (arg == "--output") {
            args.outputPath = requireValue(argc, argv, i, arg);
        } else if (arg == "--sample-rate") {
            args.sampleRate = parseUint32Option(requireValue(argc, argv, i, arg), arg, 8000, 384000);
        } else if (arg == "--bit-depth") {
            args.bitDepth = requireValue(argc, argv, i, arg);
            if (args.bitDepth != "16" && args.bitDepth != "24" && args.bitDepth != "32") {
                throw std::invalid_argument("Invalid value for --bit-depth: " + args.bitDepth);
            }
        } else {
            throw std::invalid_argument("Unknown option: " + arg);
        }
    }
    
    return args;
}

void listTemplates() {
    std::cout << "Available Templates:\n";
    std::cout << "  Techno   - Four-on-floor kick, hi-hats, clap, bassline (130 BPM)\n";
    std::cout << "  House    - Off-beat bass, four-on-floor kick (125 BPM)\n";
    std::cout << "  Minimal  - Sparse kick, euclidean hi-hats (128 BPM)\n";
    std::cout << "\nUsage example:\n";
    std::cout << "  ./AestraHeadlessExport --template Techno --duration 16 --output song.wav\n";
}

AudioExporter::BitDepth parseBitDepth(const std::string& depth) {
    if (depth == "16") return AudioExporter::BitDepth::PCM_16;
    if (depth == "32") return AudioExporter::BitDepth::Float_32;
    return AudioExporter::BitDepth::PCM_24;  // Default
}

int main(int argc, char* argv[]) {
    CommandLineArgs args;
    try {
        args = parseArgs(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n\n";
        printUsage(argv[0]);
        return 2;
    }
    
    if (args.listTemplates) {
        listTemplates();
        return 0;
    }
    
    // Initialize logging
    auto consoleLogger = std::make_shared<ConsoleLogger>(args.verbose ? LogLevel::Debug : LogLevel::Info);
    Log::init(consoleLogger);
    
    Log::info("========================================");
    Log::info("Aestra Headless Export");
    Log::info("========================================");
    
    try {
        // Initialize audio engine. This tool owns its engine for the lifetime of
        // the export (mirroring HeadlessMain / MuseReplMain) rather than reaching
        // for a process-wide singleton — nothing else constructs one here.
        Log::info("Initializing audio engine...");
        AudioEngine engine;
        engine.setSampleRate(args.sampleRate);
        engine.setBPM(static_cast<float>(args.tempo));
        
        // Create track manager
        Log::info("Creating track manager...");
        TrackManager trackManager;
        
        // Create generator
        Log::info("Generating music from template: " + args.templateName);
        HeadlessMusicGenerator generator(engine, trackManager);
        
        generator.createProject("HeadlessExport")
               .setTempo(args.tempo)
               .setSampleRate(args.sampleRate)
               .generateFromTemplate(args.templateName, args.durationBars);
        
        if (args.verbose) {
            generator.printInfo();
        }
        
        // Export
        Log::info("Exporting to: " + args.outputPath);
        
        auto progressCallback = [](float percent) {
            int barWidth = 50;
            int pos = static_cast<int>(percent * barWidth);
            
            std::cout << "\r[";
            for (int i = 0; i < barWidth; ++i) {
                if (i < pos) std::cout << "=";
                else if (i == pos) std::cout << ">";
                else std::cout << " ";
            }
            std::cout << "] " << static_cast<int>(percent * 100) << "%" << std::flush;
        };
        
        bool success = generator.exportTo(args.outputPath, 
                                           progressCallback,
                                           args.sampleRate,
                                           parseBitDepth(args.bitDepth));
        
        std::cout << std::endl;
        
        if (success) {
            Log::info("Export complete!");
            Log::info("Output: " + args.outputPath);
            return 0;
        } else {
            Log::error("Export failed!");
            return 1;
        }
        
    } catch (const std::exception& e) {
        Log::error("Fatal error: " + std::string(e.what()));
        return 1;
    }
}
