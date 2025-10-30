// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "../Core/NUIComponent.h"
#include "../Core/NUICustomWindow.h"
#include "../Core/NUIContextMenu.h"
#include "../Core/NUIThemeSystem.h"
#include "../Graphics/NUIRenderer.h"
#include "../Graphics/OpenGL/NUIRendererGL.h"
#include "../Platform/NUIPlatformBridge.h"
#include "../External/glad/include/glad/glad.h"

// Compatibility typedef for existing code
using NUIWindowWin32 = NomadUI::NUIPlatformBridge;
#include <iostream>
#include <memory>

using namespace NomadUI;

class CustomWindowContent : public NUIComponent {
private:
    NUICustomWindow* customWindow_ = nullptr;
    std::shared_ptr<NUIContextMenu> contextMenu_;
    
public:
    CustomWindowContent() {
        createContextMenu();
    }
    
    void setCustomWindow(NUICustomWindow* window) { customWindow_ = window; }
    
    void createContextMenu() {
        contextMenu_ = std::make_shared<NUIContextMenu>();
        
        // Apply Nomad theme colors to context menu
        auto& themeManager = NUIThemeManager::getInstance();
        contextMenu_->setBackgroundColor(themeManager.getColor("surfaceTertiary"));
        contextMenu_->setBorderColor(themeManager.getColor("borderActive"));
        contextMenu_->setTextColor(themeManager.getColor("textPrimary"));
        contextMenu_->setHoverColor(themeManager.getColor("primary"));
        contextMenu_->setSeparatorColor(themeManager.getColor("borderSubtle"));
        contextMenu_->setShortcutColor(themeManager.getColor("textSecondary"));
        
        // Add menu items showcasing different features
        contextMenu_->addItem("Cut", []() { 
            std::cout << "Cut selected" << std::endl; 
        });
        
        contextMenu_->addItem("Copy", []() { 
            std::cout << "Copy selected" << std::endl; 
        });
        
        contextMenu_->addItem("Paste", []() { 
            std::cout << "Paste selected" << std::endl; 
        });
        
        contextMenu_->addSeparator();
        
        // Theme options (without submenu for now)
        contextMenu_->addRadioItem("Nomad Dark Theme", "theme", true, []() {
            NUIThemeManager::getInstance().setActiveTheme("nomad-dark");
            std::cout << "Switched to Nomad Dark theme" << std::endl;
        });
        
        contextMenu_->addRadioItem("Nomad Light Theme", "theme", false, []() {
            NUIThemeManager::getInstance().setActiveTheme("nomad-light");
            std::cout << "Switched to Nomad Light theme" << std::endl;
        });
        
        contextMenu_->addSeparator();
        
        contextMenu_->addCheckbox("Show Grid", false, [](bool checked) {
            std::cout << "Show Grid: " << (checked ? "ON" : "OFF") << std::endl;
        });
        
        contextMenu_->addCheckbox("Snap to Grid", true, [](bool checked) {
            std::cout << "Snap to Grid: " << (checked ? "ON" : "OFF") << std::endl;
        });
        
        contextMenu_->addSeparator();
        
        contextMenu_->addItem("Settings", []() {
            std::cout << "Settings selected" << std::endl;
        });
        
        contextMenu_->addItem("About", []() {
            std::cout << "About Nomad UI" << std::endl;
        });
        
        addChild(contextMenu_);
    }
    
    void onRender(NUIRenderer& renderer) override {
        // Get window dimensions for responsive layout
        NUIRect bounds = getBounds();
        float centerX = bounds.width * 0.5f;
        float centerY = bounds.height * 0.5f;
        
        // Calculate responsive font sizes
        float baseFontSize = std::min(bounds.width, bounds.height) * 0.02f;
        float titleFontSize = std::max(24.0f, baseFontSize * 1.5f);
        float textFontSize = std::max(16.0f, baseFontSize);
        
        // Get theme colors
        auto& themeManager = NUIThemeManager::getInstance();
        NUIColor textColor = themeManager.getColor("text");
        NUIColor accentColor = themeManager.getColor("accent");
        
        // Perfect responsive text centering using measureText
        std::string titleText = "Custom Window Demo";
        NUISize titleSize = renderer.measureText(titleText, titleFontSize);
        NUIPoint textPos(centerX - titleSize.width * 0.5f, centerY - titleSize.height * 2.5f);
        renderer.drawText(titleText, textPos, titleFontSize, textColor);
        
        // Add full-screen mode indicator
        textPos.y += titleSize.height + textFontSize * 0.5f;
        std::string modeText;
        NUIColor modeColor;
        if (customWindow_ && customWindow_->isFullScreen()) {
            modeText = "Full-Screen Mode - Press F11 to restore window";
            modeColor = themeManager.getColor("warning");
        } else {
            modeText = "Window Mode - Title bar with controls visible";
            modeColor = themeManager.getColor("accent");
        }
        NUISize modeSize = renderer.measureText(modeText, textFontSize * 0.8f);
        textPos.x = centerX - modeSize.width * 0.5f;
        renderer.drawText(modeText, textPos, textFontSize * 0.8f, modeColor);
        
        // Draw instructions - perfectly centered with dynamic spacing
        textPos.y += titleSize.height + textFontSize * 0.8f;
        std::string instruction1 = "This is a custom window with title bar";
        NUISize instr1Size = renderer.measureText(instruction1, textFontSize);
        textPos.x = centerX - instr1Size.width * 0.5f;
        renderer.drawText(instruction1, textPos, textFontSize, textColor);
        
        textPos.y += textFontSize * 1.3f;
        std::string instruction2 = "Press F11 to toggle full screen";
        NUISize instr2Size = renderer.measureText(instruction2, textFontSize);
        textPos.x = centerX - instr2Size.width * 0.5f;
        renderer.drawText(instruction2, textPos, textFontSize, textColor);
        
        textPos.y += textFontSize * 1.3f;
        std::string instruction3 = "Right-click to open context menu";
        NUISize instr3Size = renderer.measureText(instruction3, textFontSize);
        textPos.x = centerX - instr3Size.width * 0.5f;
        renderer.drawText(instruction3, textPos, textFontSize, themeManager.getColor("primary"));
        
        textPos.y += textFontSize * 1.3f;
        std::string instruction4 = "Drag the title bar to move the window";
        NUISize instr4Size = renderer.measureText(instruction4, textFontSize);
        textPos.x = centerX - instr4Size.width * 0.5f;
        renderer.drawText(instruction4, textPos, textFontSize, textColor);
        
        // Draw window size info
        textPos.y += textFontSize * 2.5f;
        int windowWidth = static_cast<int>(bounds.width);
        int windowHeight = static_cast<int>(bounds.height);
        if (customWindow_ && !customWindow_->isFullScreen()) {
            windowHeight += 32;
        }
        std::string sizeInfo = "Window Size: " + std::to_string(windowWidth) + " x " + std::to_string(windowHeight);
        NUISize sizeInfoSize = renderer.measureText(sizeInfo, textFontSize * 0.9f);
        textPos.x = centerX - sizeInfoSize.width * 0.5f;
        renderer.drawText(sizeInfo, textPos, textFontSize * 0.9f, accentColor);
        
        // Draw color palette showcase at the bottom
        float paletteY = bounds.height - 120.0f;
        float paletteX = 40.0f;
        float swatchSize = 40.0f;
        float swatchSpacing = 50.0f;
        
        // Title for color showcase
        std::string paletteTitle = "Nomad Color Palette";
        renderer.drawText(paletteTitle, NUIPoint(paletteX, paletteY - 20.0f), 12.0f, themeManager.getColor("textSecondary"));
        
        // Core structure colors
        renderer.fillRect(NUIRect(paletteX, paletteY, swatchSize, swatchSize), themeManager.getColor("backgroundPrimary"));
        renderer.strokeRect(NUIRect(paletteX, paletteY, swatchSize, swatchSize), 1.0f, themeManager.getColor("borderSubtle"));
        renderer.drawText("BG1", NUIPoint(paletteX + 5, paletteY + swatchSize + 12), 10.0f, themeManager.getColor("textSecondary"));
        
        paletteX += swatchSpacing;
        renderer.fillRect(NUIRect(paletteX, paletteY, swatchSize, swatchSize), themeManager.getColor("backgroundSecondary"));
        renderer.strokeRect(NUIRect(paletteX, paletteY, swatchSize, swatchSize), 1.0f, themeManager.getColor("borderSubtle"));
        renderer.drawText("BG2", NUIPoint(paletteX + 5, paletteY + swatchSize + 12), 10.0f, themeManager.getColor("textSecondary"));
        
        paletteX += swatchSpacing;
        renderer.fillRect(NUIRect(paletteX, paletteY, swatchSize, swatchSize), themeManager.getColor("surfaceTertiary"));
        renderer.strokeRect(NUIRect(paletteX, paletteY, swatchSize, swatchSize), 1.0f, themeManager.getColor("borderSubtle"));
        renderer.drawText("Surf", NUIPoint(paletteX + 5, paletteY + swatchSize + 12), 10.0f, themeManager.getColor("textSecondary"));
        
        paletteX += swatchSpacing;
        renderer.fillRect(NUIRect(paletteX, paletteY, swatchSize, swatchSize), themeManager.getColor("surfaceRaised"));
        renderer.strokeRect(NUIRect(paletteX, paletteY, swatchSize, swatchSize), 1.0f, themeManager.getColor("borderSubtle"));
        renderer.drawText("Card", NUIPoint(paletteX + 5, paletteY + swatchSize + 12), 10.0f, themeManager.getColor("textSecondary"));
        
        // Accent colors
        paletteX += swatchSpacing * 1.5f;
        renderer.fillRect(NUIRect(paletteX, paletteY, swatchSize, swatchSize), themeManager.getColor("primary"));
        renderer.strokeRect(NUIRect(paletteX, paletteY, swatchSize, swatchSize), 1.0f, themeManager.getColor("borderActive"));
        renderer.drawText("Accent", NUIPoint(paletteX, paletteY + swatchSize + 12), 10.0f, themeManager.getColor("textSecondary"));
        
        paletteX += swatchSpacing;
        renderer.fillRect(NUIRect(paletteX, paletteY, swatchSize, swatchSize), themeManager.getColor("primaryHover"));
        renderer.strokeRect(NUIRect(paletteX, paletteY, swatchSize, swatchSize), 1.0f, themeManager.getColor("borderSubtle"));
        renderer.drawText("Hover", NUIPoint(paletteX, paletteY + swatchSize + 12), 10.0f, themeManager.getColor("textSecondary"));
        
        // Status colors
        paletteX += swatchSpacing * 1.5f;
        renderer.fillRect(NUIRect(paletteX, paletteY, swatchSize, swatchSize), themeManager.getColor("success"));
        renderer.strokeRect(NUIRect(paletteX, paletteY, swatchSize, swatchSize), 1.0f, themeManager.getColor("borderSubtle"));
        renderer.drawText("OK", NUIPoint(paletteX + 8, paletteY + swatchSize + 12), 10.0f, themeManager.getColor("textSecondary"));
        
        paletteX += swatchSpacing;
        renderer.fillRect(NUIRect(paletteX, paletteY, swatchSize, swatchSize), themeManager.getColor("warning"));
        renderer.strokeRect(NUIRect(paletteX, paletteY, swatchSize, swatchSize), 1.0f, themeManager.getColor("borderSubtle"));
        renderer.drawText("Warn", NUIPoint(paletteX + 2, paletteY + swatchSize + 12), 10.0f, themeManager.getColor("textSecondary"));
        
        paletteX += swatchSpacing;
        renderer.fillRect(NUIRect(paletteX, paletteY, swatchSize, swatchSize), themeManager.getColor("error"));
        renderer.strokeRect(NUIRect(paletteX, paletteY, swatchSize, swatchSize), 1.0f, themeManager.getColor("borderSubtle"));
        renderer.drawText("Error", NUIPoint(paletteX + 2, paletteY + swatchSize + 12), 10.0f, themeManager.getColor("textSecondary"));
        
        paletteX += swatchSpacing;
        renderer.fillRect(NUIRect(paletteX, paletteY, swatchSize, swatchSize), themeManager.getColor("info"));
        renderer.strokeRect(NUIRect(paletteX, paletteY, swatchSize, swatchSize), 1.0f, themeManager.getColor("borderSubtle"));
        renderer.drawText("Info", NUIPoint(paletteX + 5, paletteY + swatchSize + 12), 10.0f, themeManager.getColor("textSecondary"));
        
        // Render context menu if visible
        if (contextMenu_ && contextMenu_->isVisible()) {
            contextMenu_->onRender(renderer);
        }
    }
    
    bool onMouseEvent(const NUIMouseEvent& event) override {
        // Handle context menu interaction first
        if (contextMenu_ && contextMenu_->isVisible()) {
            if (contextMenu_->onMouseEvent(event)) {
                return true;
            }
        }
        
        // Show context menu on right-click
        if (event.pressed && event.button == NUIMouseButton::Right) {
            contextMenu_->showAt(event.position);
            return true;
        }
        
        // Hide context menu on left-click outside
        if (event.pressed && event.button == NUIMouseButton::Left) {
            if (contextMenu_ && contextMenu_->isVisible()) {
                contextMenu_->hide();
            }
        }
        
        return NUIComponent::onMouseEvent(event);
    }
};

class CustomWindowDemo : public NUIComponent {
private:
    std::shared_ptr<NUICustomWindow> customWindow_;
    std::shared_ptr<NUIComponent> contentArea_;

public:
    CustomWindowDemo() {
        // Apply Nomad theme
        auto& themeManager = NUIThemeManager::getInstance();
        themeManager.setActiveTheme("nomad-dark");
        
        // Create custom window
        customWindow_ = std::make_shared<NUICustomWindow>();
        customWindow_->setTitle("Nomad Custom Window Demo");
        
        // Create content area with responsive content
        contentArea_ = std::make_shared<CustomWindowContent>();
        customWindow_->setContent(contentArea_.get());
        
        addChild(customWindow_);
    }
    
    void onRender(NUIRenderer& renderer) override {
        // Draw background
        auto& themeManager = NUIThemeManager::getInstance();
        NUIColor bgColor = themeManager.getColor("background");
        renderer.fillRect(getBounds(), bgColor);
        
        // Render children
        renderChildren(renderer);
    }
    
    void onResize(int width, int height) override {
        // Update custom window size
        customWindow_->setBounds(NUIRect(0, 0, width, height));
        NUIComponent::onResize(width, height);
    }
    
    bool onKeyEvent(const NUIKeyEvent& event) override {
        if (event.pressed) {
            switch (event.keyCode) {
                case NUIKeyCode::F11:
                    std::cout << "F11 pressed - toggling full screen" << std::endl;
                    customWindow_->toggleFullScreen();
                    return true;
                case NUIKeyCode::Escape:
                    if (customWindow_->isFullScreen()) {
                        std::cout << "Escape pressed - exiting full screen" << std::endl;
                        customWindow_->exitFullScreen();
                        return true;
                    }
                    break;
            }
        }
        return NUIComponent::onKeyEvent(event);
    }
};

// Main demo function
int main() {
    std::cout << "Nomad Custom Window Demo" << std::endl;
    std::cout << "========================" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  F11 - Toggle full screen" << std::endl;
    std::cout << "  Escape - Exit full screen" << std::endl;
    std::cout << "  Right-click - Open context menu (showcases Nomad theme)" << std::endl;
    std::cout << "  Custom title bar with window controls" << std::endl;
    std::cout << std::endl;
    std::cout << "Features:" << std::endl;
    std::cout << "  - Complete Nomad color palette showcase" << std::endl;
    std::cout << "  - Context menu with theme colors" << std::endl;
    std::cout << "  - Layered background system" << std::endl;
    std::cout << "  - Status colors (success, warning, error, info)" << std::endl;
    std::cout << std::endl;
    
    // Create borderless window for custom title bar
    // The custom window will handle the title bar internally
    NUIWindowWin32 window;
    Nomad::WindowDesc desc;
    desc.title = "Nomad Custom Window Demo";
    desc.width = 1000;
    desc.height = 700;
    desc.decorated = false;  // Borderless for custom title bar!
    desc.resizable = true;
    
    if (!window.create(desc)) {
        std::cerr << "Failed to create window" << std::endl;
        return -1;
    }
    
    // Create renderer with matching window dimensions
    NUIRendererGL renderer;
    if (!renderer.initialize(1000, 700)) {
        std::cerr << "Failed to initialize renderer" << std::endl;
        return -1;
    }
    
    // Create demo component
    CustomWindowDemo demo;
    demo.setBounds(NUIRect(0, 0, 1000, 700));
    
    // Set up window
    window.setRootComponent(&demo);
    window.setRenderer(&renderer);
    window.show();
    
    // Connect custom window to platform window
    auto children = demo.getChildren();
    if (!children.empty()) {
        auto customWindow = std::dynamic_pointer_cast<NUICustomWindow>(children[0]);
        if (customWindow) {
            customWindow->setWindowHandle(&window);
            
            // Connect custom window to content for full-screen state checking
            auto contentChildren = customWindow->getChildren();
            if (contentChildren.size() > 1) { // title bar + content
                auto content = std::dynamic_pointer_cast<CustomWindowContent>(contentChildren[1]);
                if (content) {
                    content->setCustomWindow(customWindow.get());
                }
            }
        }
    }
    
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
