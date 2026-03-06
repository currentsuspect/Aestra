// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraLog.h"
#include "AestraPlatform.h"

#include <iostream>

using namespace Aestra;

int main() {
    // Set logging level
    Log::setLevel(LogLevel::Info);

    // Initialize platform (this will set DPI awareness)
    if (!Platform::initialize()) {
        AESTRA_LOG_ERROR("Failed to initialize platform");
        return 1;
    }

    // Create window
    IPlatformWindow* window = Platform::createWindow();
    if (!window) {
        AESTRA_LOG_ERROR("Failed to create window");
        Platform::shutdown();
        return 1;
    }

    // Create window with default settings
    WindowDesc desc;
    desc.title = "DPI Test Window";
    desc.width = 800;
    desc.height = 600;

    if (!window->create(desc)) {
        AESTRA_LOG_ERROR("Failed to create window");
        delete window;
        Platform::shutdown();
        return 1;
    }

    // Get and display DPI scale
    float dpiScale = window->getDPIScale();
    AESTRA_LOG_INFO("Window DPI Scale: " + std::to_string(dpiScale));
    AESTRA_LOG_INFO("Logical size: 800x600");
    AESTRA_LOG_INFO("Physical size: " + std::to_string(static_cast<int>(800 * dpiScale)) + "x" +
                    std::to_string(static_cast<int>(600 * dpiScale)));

    // Set up DPI change callback
    window->setDPIChangeCallback(
        [](float newScale) { AESTRA_LOG_INFO("DPI changed! New scale: " + std::to_string(newScale)); });

    // Show window
    window->show();

    AESTRA_LOG_INFO("Window created successfully!");
    AESTRA_LOG_INFO("Try moving the window to a monitor with different DPI scaling");
    AESTRA_LOG_INFO("Press ESC or close the window to exit");

    // Main loop
    while (window->pollEvents()) {
        // Just process events - no rendering needed for this test
        Platform::getUtils()->sleep(16); // ~60 FPS
    }

    // Cleanup
    window->destroy();
    delete window;
    Platform::shutdown();

    AESTRA_LOG_INFO("Test completed successfully");
    return 0;
}
