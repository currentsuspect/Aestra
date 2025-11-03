// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include <glad/glad.h>  // MUST be first to avoid conflicts
#include "../Platform/NUIPlatformBridge.h"
using NUIWindowWin32 = NomadUI::NUIPlatformBridge;
#include <Windows.h>  // For Sleep()
#include <iostream>
#include <chrono>
#include <cmath>

using namespace NomadUI;

int main() {
    std::cout << "==================================" << std::endl;
    std::cout << "  Nomad UI - Window Demo" << std::endl;
    std::cout << "==================================" << std::endl;
    
    // Create window
    NUIWindowWin32 window;
    if (!window.create("Nomad UI Demo - Windows Platform Layer", 800, 600)) {
        std::cerr << "Failed to create window!" << std::endl;
        return 1;
    }
    
    // Show window
    window.show();
    
    std::cout << "\nWindow created successfully!" << std::endl;
    std::cout << "You should see a window with animated colors!" << std::endl;
    std::cout << "Close the window to exit." << std::endl;
    std::cout << "\n==================================" << std::endl;
    
    // Main loop
    auto lastTime = std::chrono::high_resolution_clock::now();
    int frameCount = 0;
    float time = 0.0f;
    
    while (window.processEvents()) {
        // Calculate delta time
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        time += deltaTime;
        
        // Make context current
        window.makeContextCurrent();
        
        // Clear background with animated color
        float r = 0.1f + 0.05f * std::sin(time * 0.5f);
        float g = 0.1f + 0.05f * std::sin(time * 0.7f);
        float b = 0.15f + 0.05f * std::sin(time * 0.3f);
        glClearColor(r, g, b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        // Swap buffers
        window.swapBuffers();
        
        // FPS counter
        frameCount++;
        if (frameCount % 60 == 0) {
            float fps = 1.0f / deltaTime;
            std::cout << "FPS: " << static_cast<int>(fps) << std::endl;
        }
        
        // Limit frame rate to ~60 FPS
        Sleep(16);
    }
    
    std::cout << "\n==================================" << std::endl;
    std::cout << "  Demo closed successfully!" << std::endl;
    std::cout << "==================================" << std::endl;
    
    return 0;
}
