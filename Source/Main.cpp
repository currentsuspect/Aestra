// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#define NOMAD_BUILD_ID "Nomad-2025-Core"

/**
 * @file Main.cpp
 * @brief NOMAD DAW - Main Application Entry Point
 */

#include "NomadApp.h"
#include "../NomadCore/include/NomadLog.h"
#include <iostream>
#include <objbase.h>

using namespace Nomad;

// =============================================================================
// Main Entry Point
// =============================================================================
int main(int argc, char* argv[]) {
    // Initialize COM as STA (Single-Threaded Apartment) for ASIO support
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    try {
        NomadApp app;
        
        if (!app.initialize()) {
            std::cerr << "Failed to initialize NOMAD DAW" << std::endl;
            return 1;
        }

        app.run();

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    catch (...) {
        std::cerr << "Unknown fatal error" << std::endl;
        return 1;
    }
}
