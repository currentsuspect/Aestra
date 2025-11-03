// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "../Core/NUIComponent.h"
#include "../Core/NUIIcon.h"
#include "../Core/NUIThemeSystem.h"
#include "../Graphics/NUIRenderer.h"
#include "../Graphics/OpenGL/NUIRendererGL.h"
#include "../Platform/NUIPlatformBridge.h"
using NUIWindowWin32 = NomadUI::NUIPlatformBridge;
#include "../External/glad/include/glad/glad.h"
#include <iostream>
#include <memory>

using namespace NomadUI;

class IconDemoContent : public NUIComponent {
private:
    std::vector<std::shared_ptr<NUIIcon>> icons_;
    
public:
    IconDemoContent() {
        // Create all predefined icons
        icons_.push_back(NUIIcon::createCutIcon());
        icons_.push_back(NUIIcon::createCopyIcon());
        icons_.push_back(NUIIcon::createPasteIcon());
        icons_.push_back(NUIIcon::createSettingsIcon());
        icons_.push_back(NUIIcon::createCloseIcon());
        icons_.push_back(NUIIcon::createMinimizeIcon());
        icons_.push_back(NUIIcon::createMaximizeIcon());
        icons_.push_back(NUIIcon::createCheckIcon());
        icons_.push_back(NUIIcon::createChevronRightIcon());
        icons_.push_back(NUIIcon::createChevronDownIcon());
        
        // Add custom pause icon from user's SVG file
        auto pauseIcon = std::make_shared<NUIIcon>();
        pauseIcon->loadSVGFile("NomadUI/Examples/test_pause.svg");
        pauseIcon->setColorFromTheme("textPrimary");
        pauseIcon->setIconSize(NUIIconSize::Large);
        icons_.push_back(pauseIcon);
        
        // Set icon sizes for predefined icons
        for (size_t i = 0; i < icons_.size() - 1; ++i) {
            icons_[i]->setIconSize(NUIIconSize::Large);
        }
    }
    
    void onRender(NUIRenderer& renderer) override {
        NUIRect bounds = getBounds();
        auto& themeManager = NUIThemeManager::getInstance();
        
        // Draw background
        renderer.fillRect(bounds, themeManager.getColor("backgroundPrimary"));
        
        // Draw title
        std::string title = "NomadUI Icon System Demo";
        renderer.drawText(title, NUIPoint(40, 40), 24.0f, themeManager.getColor("textPrimary"));
        
        std::string subtitle = "SVG-based icons with theme integration";
        renderer.drawText(subtitle, NUIPoint(40, 70), 14.0f, themeManager.getColor("textSecondary"));
        
        // Draw icons in a grid
        float startX = 40.0f;
        float startY = 120.0f;
        float spacing = 80.0f;
        int iconsPerRow = 5;
        
        std::vector<std::string> iconNames = {
            "Cut", "Copy", "Paste", "Settings", "Close",
            "Minimize", "Maximize", "Check", "Chevron Right", "Chevron Down",
            "Pause (Custom)"
        };
        
        for (size_t i = 0; i < icons_.size(); ++i) {
            int row = i / iconsPerRow;
            int col = i % iconsPerRow;
            
            float x = startX + col * spacing;
            float y = startY + row * spacing;
            
            // Draw icon background
            NUIRect iconBg(x - 8, y - 8, 48, 48);
            renderer.fillRoundedRect(iconBg, 6.0f, themeManager.getColor("surfaceTertiary"));
            
            // Position and render icon
            icons_[i]->setPosition(x, y);
            icons_[i]->onRender(renderer);
            
            // Draw icon name
            renderer.drawText(iconNames[i], NUIPoint(x - 8, y + 55), 11.0f, themeManager.getColor("textSecondary"));
        }
        
        // Draw color palette showcase
        float paletteY = bounds.height - 150.0f;
        renderer.drawText("Icon Colors:", NUIPoint(40, paletteY), 16.0f, themeManager.getColor("textPrimary"));
        
        // Show icons in different theme colors
        std::vector<std::string> colorNames = {"textPrimary", "primary", "success", "warning", "error", "info"};
        std::vector<std::string> colorLabels = {"Text", "Primary", "Success", "Warning", "Error", "Info"};
        
        for (size_t i = 0; i < colorNames.size(); ++i) {
            float x = 40.0f + i * 100.0f;
            float y = paletteY + 40.0f;
            
            // Create a check icon with the color
            auto icon = NUIIcon::createCheckIcon();
            icon->setIconSize(NUIIconSize::Medium);
            icon->setColorFromTheme(colorNames[i]);
            icon->setPosition(x, y);
            icon->onRender(renderer);
            
            // Draw label
            renderer.drawText(colorLabels[i], NUIPoint(x - 5, y + 35), 10.0f, themeManager.getColor("textSecondary"));
        }
    }
};

int main() {
    std::cout << "NomadUI Icon System Demo" << std::endl;
    std::cout << "========================" << std::endl;
    std::cout << "Showcasing SVG-based icons with theme integration" << std::endl;
    std::cout << "Note: Icons are simple line drawings" << std::endl;
    std::cout << std::endl;
    
    // Create window with standard title bar for now
    NUIWindowWin32 window;
    if (!window.create("NomadUI Icon Demo - SVG Icons with Theme Integration", 800, 600)) {
        std::cerr << "Failed to create window" << std::endl;
        return -1;
    }
    
    // Create renderer
    NUIRendererGL renderer;
    if (!renderer.initialize(800, 600)) {
        std::cerr << "Failed to initialize renderer" << std::endl;
        return -1;
    }
    
    // Apply Nomad theme
    auto& themeManager = NUIThemeManager::getInstance();
    themeManager.setActiveTheme("nomad-dark");
    
    // Create demo component
    IconDemoContent demo;
    demo.setBounds(NUIRect(0, 0, 800, 600));
    
    // Set up window
    window.setRootComponent(&demo);
    window.setRenderer(&renderer);
    window.show();
    
    std::cout << "Window created. If icons don't show, the SVG parser needs debugging." << std::endl;
    
    // Main loop
    while (window.processEvents()) {
        // Clear screen
        auto& theme = themeManager.getCurrentTheme();
        NUIColor bgColor = theme.backgroundPrimary;
        glClearColor(bgColor.r, bgColor.g, bgColor.b, bgColor.a);
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
