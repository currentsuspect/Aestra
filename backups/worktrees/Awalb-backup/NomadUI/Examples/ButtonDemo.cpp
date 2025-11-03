// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUIPlatformBridge.h"
#include "../Core/NUIButton.h"
#include "../Core/NUILabel.h"
#include "../Core/NUIThemeManager.h"
#include "../Core/NUIComponent.h"
#include "../../NomadCore/include/NomadLog.h"

namespace NomadUI {
namespace Examples {

/**
 * @brief Demo showcasing the improved button system
 *
 * This demo demonstrates:
 * - Smooth hover effects without lingering states
 * - No jarring color changes on button press
 * - Consistent button behavior across all styles
 * - Professional UI interactions
 */
class ButtonDemo : public NUIComponent {
public:
    ButtonDemo() {
        setupDemo();
    }

    void setupDemo() {
        Log::info("Setting up Button Demo");

        // Demo title
        auto title = std::make_shared<NUILabel>();
        title->setText("Button System Demo");
        title->setAlignment(NUILabel::Alignment::Center);
        addChild(title);

        // Primary button demo
        auto primaryButton = std::make_shared<NUIButton>("Primary Button");
        primaryButton->setStyle(NUIButton::Style::Primary);
        primaryButton->setOnClick([]() {
            Log::info("Primary button clicked!");
        });
        addChild(primaryButton);

        // Secondary button demo
        auto secondaryButton = std::make_shared<NUIButton>("Secondary Button");
        secondaryButton->setStyle(NUIButton::Style::Secondary);
        secondaryButton->setOnClick([]() {
            Log::info("Secondary button clicked!");
        });
        addChild(secondaryButton);

        // Icon button demo
        auto iconButton = std::make_shared<NUIButton>();
        iconButton->setStyle(NUIButton::Style::Icon);
        iconButton->setSize(40, 40);
        iconButton->setOnClick([]() {
            Log::info("Icon button clicked!");
        });
        addChild(iconButton);

        // Text button demo
        auto textButton = std::make_shared<NUIButton>("Text Button");
        textButton->setStyle(NUIButton::Style::Text);
        textButton->setOnClick([]() {
            Log::info("Text button clicked!");
        });
        addChild(textButton);

        // Status label
        statusLabel = std::make_shared<NUILabel>();
        statusLabel->setText("Hover and click the buttons above!");
        statusLabel->setAlignment(NUILabel::Alignment::Center);
        addChild(statusLabel);

        layoutComponents();
        Log::info("Button Demo setup complete");
    }

    void layoutComponents() {
        NUIRect bounds = getBounds();
        float y = 20.0f;
        float centerX = bounds.width / 2.0f;
        float buttonWidth = 200.0f;
        float buttonHeight = 40.0f;

        // Title
        if (auto title = findChildById("title")) {
            title->setBounds(0, y, bounds.width, 30);
            y += 50;
        }

        // Primary button
        if (auto btn = findChildById("primary")) {
            btn->setBounds(centerX - buttonWidth/2, y, buttonWidth, buttonHeight);
            y += 60;
        }

        // Secondary button
        if (auto btn = findChildById("secondary")) {
            btn->setBounds(centerX - buttonWidth/2, y, buttonWidth, buttonHeight);
            y += 60;
        }

        // Icon button
        if (auto btn = findChildById("icon")) {
            btn->setBounds(centerX - 20, y, 40, 40);
            y += 60;
        }

        // Text button
        if (auto btn = findChildById("text")) {
            btn->setBounds(centerX - buttonWidth/2, y, buttonWidth, buttonHeight);
            y += 60;
        }

        // Status label
        if (statusLabel) {
            statusLabel->setBounds(0, y, bounds.width, 30);
        }
    }

    void onRender(NUIRenderer& renderer) override {
        NUIRect bounds = getBounds();

        // Draw demo background
        auto& themeManager = NUIThemeManager::getInstance();
        NUIColor bgColor = themeManager.getColor("backgroundPrimary");
        renderer.fillRect(bounds, bgColor);

        // Draw title
        renderer.drawText("ðŸ–±ï¸ Button Hover System Demo",
                         NUIPoint(bounds.width/2 - 100, 15),
                         16, themeManager.getColor("textPrimary"));

        // Draw instructions
        renderer.drawText("âœ“ Smooth hover effects",
                         NUIPoint(20, bounds.height - 60),
                         12, themeManager.getColor("textSecondary"));
        renderer.drawText("âœ“ No lingering hover states",
                         NUIPoint(20, bounds.height - 45),
                         12, themeManager.getColor("textSecondary"));
        renderer.drawText("âœ“ Clean button press feedback",
                         NUIPoint(20, bounds.height - 30),
                         12, themeManager.getColor("textSecondary"));

        renderChildren(renderer);
    }

private:
    std::shared_ptr<NUILabel> statusLabel;
};

} // namespace Examples
} // namespace NomadUI
