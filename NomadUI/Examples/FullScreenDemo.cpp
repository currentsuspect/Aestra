// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "../Core/NUIComponent.h"
#include "../Core/NUIThemeSystem.h"
#include "../Graphics/NUIRenderer.h"
#include "../Graphics/OpenGL/NUIRendererGL.h"
#include "../Platform/NUIPlatformBridge.h"
using NUIWindowWin32 = NomadUI::NUIPlatformBridge;
#include "../External/glad/include/glad/glad.h"
#include <iostream>
#include <memory>

using namespace NomadUI;

class FullScreenDemo : public NUIComponent {
public:
    FullScreenDemo() {
        // Apply Nomad theme
        auto& themeManager = NUIThemeManager::getInstance();
        themeManager.setActiveTheme("nomad-dark");
    }
    
    void setPlatformWindow(NUIWindowWin32* window) { platformWindow_ = window; }
    
    void onRender(NUIRenderer& renderer) override {
        // Draw background
        auto& themeManager = NUIThemeManager::getInstance();
        NUIColor bgColor = themeManager.getColor("background");
        renderer.fillRect(getBounds(), bgColor);
        
        // Get window dimensions for responsive layout
        NUIRect bounds = getBounds();
        float centerX = bounds.width * 0.5f;
        float centerY = bounds.height * 0.5f;
        
        
        // Calculate responsive font sizes based on window size
        float baseFontSize = std::min(bounds.width, bounds.height) * 0.02f; // 2% of smaller dimension
        float titleFontSize = std::max(24.0f, baseFontSize * 1.5f);
        float textFontSize = std::max(16.0f, baseFontSize);
        
        // Draw instructions centered
        NUIColor textColor = themeManager.getColor("text");
        
        // Perfect responsive text centering using measureText
        std::string titleText = "Nomad Full Screen Demo";
        NUISize titleSize = renderer.measureText(titleText, titleFontSize);
        NUIPoint textPos(centerX - titleSize.width * 0.5f, centerY - titleSize.height * 1.5f);
        renderer.drawText(titleText, textPos, titleFontSize, textColor);
        
        // Instructions - perfectly centered with dynamic spacing
        textPos.y += titleSize.height + textFontSize * 0.5f; // Dynamic spacing based on font size
        std::string instruction1 = "Press F11 to toggle full screen";
        NUISize instr1Size = renderer.measureText(instruction1, textFontSize);
        textPos.x = centerX - instr1Size.width * 0.5f;
        renderer.drawText(instruction1, textPos, textFontSize, textColor);
        
        textPos.y += textFontSize * 1.2f; // Dynamic line spacing
        std::string instruction2 = "Press Escape to exit full screen";
        NUISize instr2Size = renderer.measureText(instruction2, textFontSize);
        textPos.x = centerX - instr2Size.width * 0.5f;
        renderer.drawText(instruction2, textPos, textFontSize, textColor);
        
        textPos.y += textFontSize * 1.2f;
        std::string instruction3 = "Right-click for context menu";
        NUISize instr3Size = renderer.measureText(instruction3, textFontSize);
        textPos.x = centerX - instr3Size.width * 0.5f;
        renderer.drawText(instruction3, textPos, textFontSize, textColor);
        
        // Window size info - perfectly centered with dynamic spacing
        textPos.y += textFontSize * 2.0f; // More space before debug info
        std::string sizeInfo = "Window: " + std::to_string((int)bounds.width) + "x" + std::to_string((int)bounds.height);
        NUISize sizeInfoSize = renderer.measureText(sizeInfo, textFontSize * 0.8f);
        textPos.x = centerX - sizeInfoSize.width * 0.5f;
        renderer.drawText(sizeInfo, textPos, textFontSize * 0.8f, textColor);
    }
    
    bool onKeyEvent(const NUIKeyEvent& event) override {
        if (event.pressed) {
            switch (event.keyCode) {
                case NUIKeyCode::F11:
                    std::cout << "F11 pressed - toggling full screen" << std::endl;
                    if (platformWindow_) {
                        platformWindow_->toggleFullScreen();
                    }
                    return true;
                case NUIKeyCode::Escape:
                    std::cout << "Escape pressed - exiting full screen" << std::endl;
                    if (platformWindow_ && platformWindow_->isFullScreen()) {
                        platformWindow_->exitFullScreen();
                    }
                    return true;
            }
        }
        return NUIComponent::onKeyEvent(event);
    }

private:
    NUIWindowWin32* platformWindow_ = nullptr;
};

// Main demo function
int main() {
    std::cout << "Nomad Full Screen Demo" << std::endl;
    std::cout << "======================" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  F11 - Toggle full screen" << std::endl;
    std::cout << "  Escape - Exit full screen" << std::endl;
    std::cout << "  Right-click - Context menu" << std::endl;
    std::cout << std::endl;
    
    // Create window
    NUIWindowWin32 window;
    if (!window.create("Nomad Full Screen Demo", 800, 600)) {
        std::cerr << "Failed to create window" << std::endl;
        return -1;
    }
    
    // Create renderer
    NUIRendererGL renderer;
    if (!renderer.initialize(800, 600)) {
        std::cerr << "Failed to initialize renderer" << std::endl;
        return -1;
    }
    
    // Create demo component
    FullScreenDemo demo;
    demo.setBounds(NUIRect(0, 0, 800, 600));
    
    // Set up window
    window.setRootComponent(&demo);
    window.setRenderer(&renderer);
    demo.setPlatformWindow(&window);
    window.show();
    
    // Main loop
    while (window.processEvents()) {
        // Clear screen
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        // Render
        renderer.beginFrame();
        demo.onRender(renderer);
        renderer.endFrame();
        
        // Swap buffers
        window.swapBuffers();
    }
    
    std::cout << "Demo completed successfully!" << std::endl;
    return 0;
}
