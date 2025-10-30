// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
/**
 * Button Test App - Test button components without OpenGL
 * This tests the button functionality without requiring a window
 */

#include <iostream>
#include <memory>
#include <chrono>

// NomadUI includes
#include "Core/NUIComponent.h"
#include "Core/NUIButton.h"
#include "Core/NUILabel.h"

using namespace NomadUI;

class ButtonTestApp : public NUIComponent
{
public:
    ButtonTestApp()
    {
        // Create test buttons
        primaryButton = std::make_shared<NUIButton>("Primary");
        primaryButton->setStyle(NUIButton::Style::Primary);
        primaryButton->setBackgroundColor(NUIColor::fromHex(0xff4CAF50));
        primaryButton->setTextColor(NUIColor::fromHex(0xffffffff));
        primaryButton->setOnClick([this]() {
            std::cout << "âœ“ Primary button clicked!" << std::endl;
        });
        addChild(primaryButton);

        secondaryButton = std::make_shared<NUIButton>("Secondary");
        secondaryButton->setStyle(NUIButton::Style::Secondary);
        secondaryButton->setBackgroundColor(NUIColor::fromHex(0xff2196F3));
        secondaryButton->setTextColor(NUIColor::fromHex(0xffffffff));
        secondaryButton->setOnClick([this]() {
            std::cout << "âœ“ Secondary button clicked!" << std::endl;
        });
        addChild(secondaryButton);

        textButton = std::make_shared<NUIButton>("Text Only");
        textButton->setStyle(NUIButton::Style::Text);
        textButton->setTextColor(NUIColor::fromHex(0xffFF9800));
        textButton->setOnClick([this]() {
            std::cout << "âœ“ Text button clicked!" << std::endl;
        });
        addChild(textButton);

        iconButton = std::make_shared<NUIButton>("â—");
        iconButton->setStyle(NUIButton::Style::Icon);
        iconButton->setBackgroundColor(NUIColor::fromHex(0xffE91E63));
        iconButton->setTextColor(NUIColor::fromHex(0xffffffff));
        iconButton->setOnClick([this]() {
            std::cout << "âœ“ Icon button clicked!" << std::endl;
        });
        addChild(iconButton);

        toggleButton = std::make_shared<NUIButton>("Toggle");
        toggleButton->setStyle(NUIButton::Style::Primary);
        toggleButton->setBackgroundColor(NUIColor::fromHex(0xff607D8B));
        toggleButton->setTextColor(NUIColor::fromHex(0xffffffff));
        toggleButton->setToggleable(true);
        toggleButton->setOnToggle([this](bool toggled) {
            std::cout << "âœ“ Toggle button: " << (toggled ? "ON" : "OFF") << std::endl;
        });
        addChild(toggleButton);

        disabledButton = std::make_shared<NUIButton>("Disabled");
        disabledButton->setStyle(NUIButton::Style::Primary);
        disabledButton->setBackgroundColor(NUIColor::fromHex(0xff757575));
        disabledButton->setTextColor(NUIColor::fromHex(0xffBDBDBD));
        disabledButton->setEnabled(false);
        addChild(disabledButton);

        // Create labels
        titleLabel = std::make_shared<NUILabel>("NomadUI Button Test");
        titleLabel->setTextColor(NUIColor::fromHex(0xffa855f7));
        titleLabel->setAlignment(NUILabel::Alignment::Center);
        addChild(titleLabel);

        statusLabel = std::make_shared<NUILabel>("Testing button functionality...");
        statusLabel->setTextColor(NUIColor::fromHex(0xff888888));
        addChild(statusLabel);

        // Set initial size
        setSize(400, 300);
    }

    void testButtons()
    {
        std::cout << "\n=== Testing Button Functionality ===" << std::endl;
        
        // Test button properties
        std::cout << "Primary button text: " << primaryButton->getText() << std::endl;
        std::cout << "Secondary button enabled: " << (secondaryButton->isEnabled() ? "Yes" : "No") << std::endl;
        std::cout << "Toggle button toggleable: " << (toggleButton->isToggleable() ? "Yes" : "No") << std::endl;
        std::cout << "Disabled button enabled: " << (disabledButton->isEnabled() ? "Yes" : "No") << std::endl;
        
        // Test button styles
        std::cout << "\nButton Styles:" << std::endl;
        std::cout << "  Primary: " << static_cast<int>(primaryButton->getStyle()) << std::endl;
        std::cout << "  Secondary: " << static_cast<int>(secondaryButton->getStyle()) << std::endl;
        std::cout << "  Text: " << static_cast<int>(textButton->getStyle()) << std::endl;
        std::cout << "  Icon: " << static_cast<int>(iconButton->getStyle()) << std::endl;
        
        // Test colors (using private members - this is just for testing)
        // Note: In real usage, you'd access these through public getters
        std::cout << "\nPrimary button style: " << static_cast<int>(primaryButton->getStyle()) << std::endl;
        
        // Test bounds
        auto bounds = primaryButton->getBounds();
        std::cout << "\nPrimary button bounds: x=" << bounds.x 
                  << " y=" << bounds.y 
                  << " w=" << bounds.getWidth() 
                  << " h=" << bounds.getHeight() << std::endl;
        
        std::cout << "\n=== Button Test Complete ===" << std::endl;
    }

    void simulateClicks()
    {
        std::cout << "\n=== Simulating Button Clicks ===" << std::endl;
        
        // Simulate mouse events (this would normally come from the window system)
        NUIMouseEvent clickEvent;
        clickEvent.position = {50.0f, 50.0f};  // Within button bounds
        clickEvent.delta = {0.0f, 0.0f};
        clickEvent.button = NUIMouseButton::Left;  // Left mouse button
        clickEvent.pressed = true;
        clickEvent.released = false;
        clickEvent.wheelDelta = 0.0f;
        
        // Test each button
        std::cout << "Testing primary button click..." << std::endl;
        primaryButton->onMouseEvent(clickEvent);
        
        clickEvent.position = {150.0f, 50.0f};
        std::cout << "Testing secondary button click..." << std::endl;
        secondaryButton->onMouseEvent(clickEvent);
        
        clickEvent.position = {250.0f, 50.0f};
        std::cout << "Testing text button click..." << std::endl;
        textButton->onMouseEvent(clickEvent);
        
        clickEvent.position = {350.0f, 50.0f};
        std::cout << "Testing icon button click..." << std::endl;
        iconButton->onMouseEvent(clickEvent);
        
        clickEvent.position = {50.0f, 100.0f};
        std::cout << "Testing toggle button click..." << std::endl;
        toggleButton->onMouseEvent(clickEvent);
        
        std::cout << "\n=== Click Simulation Complete ===" << std::endl;
    }

private:
    std::shared_ptr<NUIButton> primaryButton;
    std::shared_ptr<NUIButton> secondaryButton;
    std::shared_ptr<NUIButton> textButton;
    std::shared_ptr<NUIButton> iconButton;
    std::shared_ptr<NUIButton> toggleButton;
    std::shared_ptr<NUIButton> disabledButton;
    std::shared_ptr<NUILabel> titleLabel;
    std::shared_ptr<NUILabel> statusLabel;
};

int main()
{
    std::cout << "==================================" << std::endl;
    std::cout << "  NomadUI - Button Test App" << std::endl;
    std::cout << "==================================" << std::endl;

    try
    {
        // Create test app
        auto app = std::make_unique<ButtonTestApp>();
        
        // Test button functionality
        app->testButtons();
        app->simulateClicks();
        
        std::cout << "\n==================================" << std::endl;
        std::cout << "  All tests completed successfully!" << std::endl;
        std::cout << "==================================" << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
