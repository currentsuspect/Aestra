// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
/**
 * Simple Demo - Nomad UI Framework
 * 
 * This demonstrates the basic usage of the Nomad UI framework:
 * - Creating a window
 * - Setting up a root component
 * - Rendering with themes
 * - Handling mouse events
 */

#include "../Core/NUIApp.h"
#include "../Core/NUIComponent.h"
#include "../Core/NUITheme.h"
#include "../Graphics/NUIRenderer.h"
#include <iostream>

using namespace NomadUI;

// ============================================================================
// Demo Panel Component
// ============================================================================

class DemoPanel : public NUIComponent {
public:
    DemoPanel() {
        hoverAlpha_ = 0.0f;
        clickCount_ = 0;
    }
    
    void onRender(NUIRenderer& renderer) override {
        auto theme = getTheme();
        if (!theme) return;
        
        auto bounds = getBounds();
        
        // Background
        renderer.fillRoundedRect(
            bounds,
            theme->getBorderRadius(),
            theme->getBackground()
        );
        
        // Surface panel
        NUIRect panelRect = {
            bounds.x + 50,
            bounds.y + 50,
            bounds.width - 100,
            bounds.height - 100
        };
        
        // Glow effect when hovered
        if (hoverAlpha_ > 0.01f) {
            renderer.drawGlow(
                panelRect,
                20.0f,
                hoverAlpha_ * theme->getGlowIntensity(),
                theme->getPrimary()
            );
        }
        
        // Panel background
        renderer.fillRoundedRect(
            panelRect,
            theme->getBorderRadius() * 2,
            theme->getSurface()
        );
        
        // Title
        renderer.drawTextCentered(
            "Nomad UI Framework",
            NUIRect{panelRect.x, panelRect.y + 20, panelRect.width, 40},
            theme->getFontSizeTitle(),
            theme->getPrimary()
        );
        
        // Subtitle
        renderer.drawTextCentered(
            "GPU-Accelerated â€¢ Modern â€¢ Responsive",
            NUIRect{panelRect.x, panelRect.y + 60, panelRect.width, 30},
            theme->getFontSizeNormal(),
            theme->getTextSecondary()
        );
        
        // Interactive button
        NUIRect buttonRect = {
            panelRect.x + panelRect.width / 2 - 100,
            panelRect.y + panelRect.height / 2 - 25,
            200,
            50
        };
        
        // Button glow
        if (isHovered()) {
            renderer.drawGlow(
                buttonRect,
                15.0f,
                0.5f,
                theme->getPrimary()
            );
        }
        
        // Button background
        NUIColor buttonColor = isHovered() 
            ? theme->getPrimary() 
            : theme->getSurface();
        
        renderer.fillRoundedRect(
            buttonRect,
            theme->getBorderRadius(),
            buttonColor
        );
        
        // Button border
        renderer.strokeRoundedRect(
            buttonRect,
            theme->getBorderRadius(),
            2.0f,
            theme->getPrimary()
        );
        
        // Button text
        std::string buttonText = "Click Me! (" + std::to_string(clickCount_) + ")";
        renderer.drawTextCentered(
            buttonText,
            buttonRect,
            theme->getFontSizeNormal(),
            theme->getText()
        );
        
        // Stats at bottom
        std::string stats = "FPS: " + std::to_string(static_cast<int>(currentFPS_));
        renderer.drawText(
            stats,
            NUIPoint{panelRect.x + 20, panelRect.bottom() - 30},
            theme->getFontSizeSmall(),
            theme->getTextSecondary()
        );
        
        // Render children
        NUIComponent::onRender(renderer);
    }
    
    void onUpdate(double deltaTime) override {
        // Animate hover effect
        float targetAlpha = isHovered() ? 1.0f : 0.0f;
        float speed = 5.0f;
        
        if (hoverAlpha_ < targetAlpha) {
            hoverAlpha_ += speed * deltaTime;
            if (hoverAlpha_ > targetAlpha) hoverAlpha_ = targetAlpha;
            setDirty();
        } else if (hoverAlpha_ > targetAlpha) {
            hoverAlpha_ -= speed * deltaTime;
            if (hoverAlpha_ < targetAlpha) hoverAlpha_ = targetAlpha;
            setDirty();
        }
        
        NUIComponent::onUpdate(deltaTime);
    }
    
    bool onMouseEvent(const NUIMouseEvent& event) override {
        // Check if clicking the button
        auto bounds = getBounds();
        NUIRect buttonRect = {
            bounds.x + bounds.width / 2 - 100,
            bounds.y + bounds.height / 2 - 25,
            200,
            50
        };
        
        if (buttonRect.contains(event.position) && event.pressed) {
            clickCount_++;
            setDirty();
            std::cout << "Button clicked! Count: " << clickCount_ << std::endl;
            return true;
        }
        
        return NUIComponent::onMouseEvent(event);
    }
    
    void setFPS(float fps) {
        currentFPS_ = fps;
        setDirty();
    }
    
private:
    float hoverAlpha_;
    int clickCount_;
    float currentFPS_ = 0.0f;
};

// ============================================================================
// Main Application
// ============================================================================

int main(int argc, char** argv) {
    std::cout << "==================================" << std::endl;
    std::cout << "  Nomad UI Framework - Demo" << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << std::endl;
    
    // Create application
    NUIApp app;
    
    // Initialize
    std::cout << "Initializing..." << std::endl;
    if (!app.initialize(1024, 768, "Nomad UI - Simple Demo")) {
        std::cerr << "Failed to initialize application!" << std::endl;
        return 1;
    }
    std::cout << "âœ“ Application initialized" << std::endl;
    
    // Create theme
    auto theme = NUITheme::createDefault();
    std::cout << "âœ“ Theme loaded" << std::endl;
    
    // Create root component
    auto root = std::make_shared<DemoPanel>();
    root->setBounds(0, 0, 1024, 768);
    root->setTheme(theme);
    app.setRootComponent(root);
    std::cout << "âœ“ Root component created" << std::endl;
    
    // Update callback
    app.onUpdate = [&app, root]() {
        // Update FPS display
        root->setFPS(app.getCurrentFPS());
    };
    
    std::cout << std::endl;
    std::cout << "Running main loop..." << std::endl;
    std::cout << "Press ESC or close window to quit" << std::endl;
    std::cout << std::endl;
    
    // Run main loop
    app.run();
    
    std::cout << "Shutting down..." << std::endl;
    app.shutdown();
    
    std::cout << "âœ“ Clean exit" << std::endl;
    return 0;
}
