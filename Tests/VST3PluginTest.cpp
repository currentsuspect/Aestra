// VST3 Plugin Loading Test
// Tests the plugin scanning and loading functionality

#include "PluginManager.h"
#include "PluginScanner.h"
#include "NomadLog.h"

#ifdef NOMAD_HAS_VST3
#include "Plugin/VST3Host.h"
#endif

#include <iostream>
#include <iomanip>

using namespace Nomad::Audio;

void printPluginInfo(const PluginInfo& info) {
    std::cout << "  ID:       " << info.id << "\n";
    std::cout << "  Name:     " << info.name << "\n";
    std::cout << "  Vendor:   " << info.vendor << "\n";
    std::cout << "  Version:  " << info.version << "\n";
    std::cout << "  Category: " << info.category << "\n";
    std::cout << "  Type:     " << (info.type == PluginType::Effect ? "Effect" : 
                                   info.type == PluginType::Instrument ? "Instrument" : "Other") << "\n";
    std::cout << "  Path:     " << info.path.string() << "\n";
    std::cout << "  Editor:   " << (info.hasEditor ? "Yes" : "No") << "\n";
    std::cout << "\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "  NOMAD VST3 Plugin Loading Test\n";
    std::cout << "========================================\n\n";
    
#ifndef NOMAD_HAS_VST3
    std::cout << "ERROR: NOMAD_HAS_VST3 not defined - VST3 SDK not integrated\n";
    return 1;
#else
    // Initialize plugin manager
    std::cout << "[1] Initializing PluginManager...\n";
    auto& manager = PluginManager::getInstance();
    if (!manager.initialize()) {
        std::cout << "FAILED: Could not initialize PluginManager\n";
        return 1;
    }
    std::cout << "    OK\n\n";
    
    // Scan for plugins
    std::cout << "[2] Scanning for VST3 plugins...\n";
    auto& scanner = manager.getScanner();
    
    // Only add VST3 path for this test
    scanner.clearSearchPaths();
    scanner.addSearchPath("C:/Program Files/Common Files/VST3");
    
    auto plugins = scanner.scanBlocking();
    std::cout << "    Found " << plugins.size() << " plugins\n\n";
    
    if (plugins.empty()) {
        std::cout << "No plugins found. Check scan paths.\n";
        manager.shutdown();
        return 0;
    }
    
    // Print first 5 plugins
    std::cout << "[3] First 5 plugins found:\n";
    std::cout << "----------------------------------------\n";
    for (size_t i = 0; i < std::min(plugins.size(), size_t(5)); ++i) {
        std::cout << "[" << (i+1) << "] ";
        printPluginInfo(plugins[i]);
    }
    
    // Try to load the first effect plugin
    std::cout << "[4] Attempting to load first Effect plugin...\n";
    const PluginInfo* effectToLoad = nullptr;
    for (const auto& p : plugins) {
        if (p.type == PluginType::Effect) {
            effectToLoad = &p;
            break;
        }
    }
    
    if (!effectToLoad) {
        std::cout << "    No effect plugins found\n";
    } else {
        std::cout << "    Loading: " << effectToLoad->name << "\n";
        
        auto instance = manager.createInstance(*effectToLoad);
        if (instance) {
            std::cout << "    LOADED successfully!\n";
            
            // Initialize
            std::cout << "    Initializing with 44100 Hz, 512 samples...\n";
            if (instance->initialize(44100.0, 512)) {
                std::cout << "    Initialized OK\n";
                
                // Get parameters
                auto params = instance->getParameters();
                std::cout << "    Parameters: " << params.size() << "\n";
                if (!params.empty()) {
                    std::cout << "    First 3 parameters:\n";
                    for (size_t i = 0; i < std::min(params.size(), size_t(3)); ++i) {
                        std::cout << "      - " << params[i].name << " = " 
                                  << instance->getParameter(params[i].id) << "\n";
                    }
                }
                
                // Latency
                std::cout << "    Latency: " << instance->getLatencySamples() << " samples\n";
                
                // Cleanup
                instance->shutdown();
            } else {
                std::cout << "    FAILED to initialize\n";
            }
        } else {
            std::cout << "    FAILED to load plugin\n";
        }
    }
    
    std::cout << "\n[5] Shutting down...\n";
    manager.shutdown();
    std::cout << "    Done!\n";
    
    std::cout << "\n========================================\n";
    std::cout << "  Test Complete\n";
    std::cout << "========================================\n";
    
    return 0;
#endif
}
