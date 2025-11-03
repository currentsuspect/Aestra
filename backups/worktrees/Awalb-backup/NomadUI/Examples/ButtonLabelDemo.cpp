// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
/**
 * Button and Label Demo - Test basic NomadUI components
 * Demonstrates NUIButton and NUILabel functionality
 */

#include <iostream>
#include <memory>
#include <chrono>
#include <cmath>

// Include stddef.h first to get standard ptrdiff_t
#include <cstddef>
#include <cstdint>

// Windows headers first with proper macro definitions
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOCOMM
#include <Windows.h>

// GLAD must be included after Windows headers to avoid macro conflicts
#include <glad/glad.h>

// Suppress APIENTRY redefinition warning - both define the same value
#pragma warning(push)
#pragma warning(disable: 4005)
// Windows.h redefines APIENTRY but it's the same value, so we can ignore the warning
#pragma warning(pop)

#include <iostream>
#include <chrono>
#include <cmath>

// NomadUI includes
#include "Core/NUIComponent.h"
#include "Core/NUIButton.h"
#include "Core/NUILabel.h"
#include "Graphics/OpenGL/NUIRendererGL.h"
#include "Platform/NUIPlatformBridge.h"
using NUIWindowWin32 = NomadUI::NUIPlatformBridge;

using namespace NomadUI;

class ButtonLabelDemo : public NUIComponent
{
public:
    ButtonLabelDemo()
    {
        // Create title
        titleLabel = std::make_shared<NUILabel>("NomadUI Button Styles Demo");
        titleLabel->setTextColor(NUIColor::fromHex(0xffa855f7));
        titleLabel->setAlignment(NUILabel::Alignment::Center);
        addChild(titleLabel);

        // === PRIMARY STYLE BUTTONS ===
        primaryButton = std::make_shared<NUIButton>("Primary");
        primaryButton->setStyle(NUIButton::Style::Primary);
        primaryButton->setBackgroundColor(NUIColor::fromHex(0xff4CAF50));
        primaryButton->setTextColor(NUIColor::fromHex(0xffffffff));
        primaryButton->setOnClick([this]() {
            statusLabel->setText("Clicked: Primary Button");
        });
        addChild(primaryButton);

        // === SECONDARY STYLE BUTTONS ===
        secondaryButton = std::make_shared<NUIButton>("Secondary");
        secondaryButton->setStyle(NUIButton::Style::Secondary);
        secondaryButton->setBackgroundColor(NUIColor::fromHex(0xff2196F3));
        secondaryButton->setTextColor(NUIColor::fromHex(0xffffffff));
        secondaryButton->setOnClick([this]() {
            statusLabel->setText("Clicked: Secondary Button");
        });
        addChild(secondaryButton);

        // === TEXT STYLE BUTTONS ===
        textButton = std::make_shared<NUIButton>("Text Only");
        textButton->setStyle(NUIButton::Style::Text);
        textButton->setTextColor(NUIColor::fromHex(0xffFF9800));
        textButton->setOnClick([this]() {
            statusLabel->setText("Clicked: Text Button");
        });
        addChild(textButton);

        // === ICON STYLE BUTTONS ===
        iconButton = std::make_shared<NUIButton>("â—");
        iconButton->setStyle(NUIButton::Style::Icon);
        iconButton->setBackgroundColor(NUIColor::fromHex(0xffE91E63));
        iconButton->setTextColor(NUIColor::fromHex(0xffffffff));
        iconButton->setOnClick([this]() {
            statusLabel->setText("Clicked: Icon Button");
        });
        addChild(iconButton);

        // === CUSTOM COLORED BUTTONS ===
        customButton1 = std::make_shared<NUIButton>("Purple");
        customButton1->setStyle(NUIButton::Style::Primary);
        customButton1->setBackgroundColor(NUIColor::fromHex(0xff9C27B0));
        customButton1->setTextColor(NUIColor::fromHex(0xffffffff));
        customButton1->setOnClick([this]() {
            statusLabel->setText("Clicked: Purple Button");
        });
        addChild(customButton1);

        customButton2 = std::make_shared<NUIButton>("Orange");
        customButton2->setStyle(NUIButton::Style::Primary);
        customButton2->setBackgroundColor(NUIColor::fromHex(0xffFF5722));
        customButton2->setTextColor(NUIColor::fromHex(0xffffffff));
        customButton2->setOnClick([this]() {
            statusLabel->setText("Clicked: Orange Button");
        });
        addChild(customButton2);

        // === TOGGLE BUTTONS ===
        toggleButton = std::make_shared<NUIButton>("Toggle");
        toggleButton->setStyle(NUIButton::Style::Primary);
        toggleButton->setBackgroundColor(NUIColor::fromHex(0xff607D8B));
        toggleButton->setTextColor(NUIColor::fromHex(0xffffffff));
        toggleButton->setToggleable(true);
        toggleButton->setOnToggle([this](bool toggled) {
            toggleLabel->setText(toggled ? "Toggle: ON" : "Toggle: OFF");
        });
        addChild(toggleButton);

        // === DISABLED BUTTON ===
        disabledButton = std::make_shared<NUIButton>("Disabled");
        disabledButton->setStyle(NUIButton::Style::Primary);
        disabledButton->setBackgroundColor(NUIColor::fromHex(0xff757575));
        disabledButton->setTextColor(NUIColor::fromHex(0xffBDBDBD));
        disabledButton->setEnabled(false);
        addChild(disabledButton);

        // === LABELS ===
        statusLabel = std::make_shared<NUILabel>("Click any button to see status");
        statusLabel->setTextColor(NUIColor::fromHex(0xff888888));
        addChild(statusLabel);

        toggleLabel = std::make_shared<NUILabel>("Toggle: OFF");
        toggleLabel->setTextColor(NUIColor::fromHex(0xff888888));
        addChild(toggleLabel);

        infoLabel = std::make_shared<NUILabel>("Different button styles and customizations");
        infoLabel->setTextColor(NUIColor::fromHex(0xff666666));
        infoLabel->setAlignment(NUILabel::Alignment::Center);
        addChild(infoLabel);

        // Set initial size - make it larger to fit all buttons
        setSize(600, 500);
    }

    void onRender(NUIRenderer& renderer) override
    {
        auto bounds = getBounds();
        
        // Draw background
        renderer.fillRect(bounds, NUIColor::fromHex(0xff1a1a1a));

        // Draw border
        renderer.strokeRect(bounds, 1.0f, NUIColor::fromHex(0xff333333));
        
        // Render all child components
        for (auto& child : getChildren()) {
            if (child) {
                child->onRender(renderer);
            }
        }
    }

    void onResize(int width, int height) override
    {
        auto bounds = getBounds();
        
        // Title at top
        titleLabel->setBounds(10, 10, bounds.getWidth() - 20, 30);
        
        // Button layout - 3 rows of buttons
        int buttonWidth = 100;
        int buttonHeight = 35;
        int buttonSpacing = 15;
        int startX = 20;
        int startY = 60;
        
        // Row 1: Style buttons
        primaryButton->setBounds(startX, startY, buttonWidth, buttonHeight);
        secondaryButton->setBounds(startX + buttonWidth + buttonSpacing, startY, buttonWidth, buttonHeight);
        textButton->setBounds(startX + 2 * (buttonWidth + buttonSpacing), startY, buttonWidth, buttonHeight);
        iconButton->setBounds(startX + 3 * (buttonWidth + buttonSpacing), startY, buttonHeight, buttonHeight); // Square for icon
        
        // Row 2: Custom colored buttons
        customButton1->setBounds(startX, startY + buttonHeight + buttonSpacing, buttonWidth, buttonHeight);
        customButton2->setBounds(startX + buttonWidth + buttonSpacing, startY + buttonHeight + buttonSpacing, buttonWidth, buttonHeight);
        toggleButton->setBounds(startX + 2 * (buttonWidth + buttonSpacing), startY + buttonHeight + buttonSpacing, buttonWidth, buttonHeight);
        disabledButton->setBounds(startX + 3 * (buttonWidth + buttonSpacing), startY + buttonHeight + buttonSpacing, buttonWidth, buttonHeight);
        
        // Labels below buttons
        statusLabel->setBounds(20, startY + 2 * (buttonHeight + buttonSpacing) + 20, bounds.getWidth() - 40, 20);
        toggleLabel->setBounds(20, startY + 2 * (buttonHeight + buttonSpacing) + 50, bounds.getWidth() - 40, 20);
        infoLabel->setBounds(20, startY + 2 * (buttonHeight + buttonSpacing) + 80, bounds.getWidth() - 40, 20);
    }

    void onMouseDown(int x, int y, int button)
    {
        // Forward mouse events to children
        // TODO: Implement mouse event forwarding
    }

private:
    // Labels
    std::shared_ptr<NUILabel> titleLabel;
    std::shared_ptr<NUILabel> statusLabel;
    std::shared_ptr<NUILabel> toggleLabel;
    std::shared_ptr<NUILabel> infoLabel;
    
    // Buttons - Style examples
    std::shared_ptr<NUIButton> primaryButton;
    std::shared_ptr<NUIButton> secondaryButton;
    std::shared_ptr<NUIButton> textButton;
    std::shared_ptr<NUIButton> iconButton;
    
    // Buttons - Custom colors
    std::shared_ptr<NUIButton> customButton1;
    std::shared_ptr<NUIButton> customButton2;
    
    // Buttons - Special functionality
    std::shared_ptr<NUIButton> toggleButton;
    std::shared_ptr<NUIButton> disabledButton;
};

int main()
{
    std::cout << "==================================" << std::endl;
    std::cout << "  NomadUI - Button Styles Demo" << std::endl;
    std::cout << "==================================" << std::endl;

    try
    {
        // Create window first (this creates the OpenGL context)
        auto window = std::make_unique<NUIWindowWin32>();
        if (!window->create("NomadUI Button Styles Demo", 600, 500))
        {
            std::cerr << "Failed to create window!" << std::endl;
            return -1;
        }

        // Make the OpenGL context current
        if (!window->makeContextCurrent())
        {
            std::cerr << "Failed to make OpenGL context current!" << std::endl;
            return -1;
        }

        // Now create and initialize the OpenGL renderer
        auto renderer = std::make_unique<NUIRendererGL>();
        if (!renderer->initialize(600, 500))
        {
            std::cerr << "Failed to initialize OpenGL renderer!" << std::endl;
            return -1;
        }

        // Show the window
        window->show();

        // Create demo component
        auto demo = std::make_unique<ButtonLabelDemo>();
        // TODO: Set content when window supports it
        // window->setContent(demo.get());

        std::cout << "Window created and shown successfully!" << std::endl;
        std::cout << "You should see different button styles and colors!" << std::endl;
        std::cout << "Click buttons to test functionality. Close window to exit." << std::endl;
        std::cout << std::endl;

        // Main loop
        auto lastTime = std::chrono::high_resolution_clock::now();
        int frameCount = 0;

        while (window->processEvents())
        {
            auto currentTime = std::chrono::high_resolution_clock::now();
            auto deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
            lastTime = currentTime;

            // Render
            renderer->beginFrame();
            demo->onRender(*renderer);
            renderer->endFrame();
            window->swapBuffers();
            
            // Debug output every 60 frames
            if (frameCount % 60 == 0) {
                std::cout << "Rendering frame " << frameCount << " - Demo bounds: " 
                          << demo->getBounds().getWidth() << "x" << demo->getBounds().getHeight() << std::endl;
            }

            // FPS counter
            frameCount++;
            if (frameCount % 60 == 0)
            {
                float fps = 1.0f / deltaTime;
                std::cout << "FPS: " << static_cast<int>(fps) << std::endl;
            }

            // Small delay to prevent 100% CPU usage
            Sleep(16); // ~60 FPS
        }

        std::cout << std::endl;
        std::cout << "==================================" << std::endl;
        std::cout << "  Demo closed successfully!" << std::endl;
        std::cout << "==================================" << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
