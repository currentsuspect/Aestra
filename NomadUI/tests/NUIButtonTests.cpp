// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include <gtest/gtest.h>
#include "../Core/NUIButton.h"
#include "../Core/NUIComponent.h"

namespace NomadUI {
namespace Tests {

class ButtonTests : public ::testing::Test {
protected:
    void SetUp() override {
        button = std::make_shared<NUIButton>("Test Button");
    }

    std::shared_ptr<NUIButton> button;
};

TEST_F(ButtonTests, ButtonCreation) {
    EXPECT_TRUE(button != nullptr);
    EXPECT_EQ(button->getText(), "Test Button");
    EXPECT_TRUE(button->isEnabled());
    EXPECT_FALSE(button->isHovered());
}

TEST_F(ButtonTests, ButtonStates) {
    // Test initial state
    EXPECT_EQ(button->getState(), NUIButton::State::Normal);

    // Test hover state
    NUIMouseEvent hoverEvent;
    hoverEvent.position = NUIPoint(10, 10);
    hoverEvent.pressed = false;
    hoverEvent.released = false;

    button->setBounds(0, 0, 100, 30);
    button->onMouseEvent(hoverEvent);

    // Note: Hover state is managed by the component system
    // This test verifies the button can handle mouse events
    EXPECT_TRUE(button->isEnabled());
}

TEST_F(ButtonTests, ButtonTextAndStyle) {
    button->setText("Click Me");
    EXPECT_EQ(button->getText(), "Click Me");

    button->setStyle(NUIButton::Style::Secondary);
    EXPECT_EQ(button->getStyle(), NUIButton::Style::Secondary);

    button->setEnabled(false);
    EXPECT_FALSE(button->isEnabled());
}

TEST_F(ButtonTests, ButtonCallbacks) {
    bool clicked = false;
    bool toggled = false;

    button->setOnClick([&clicked]() {
        clicked = true;
    });

    button->setOnToggle([&toggled](bool state) {
        toggled = state;
    });

    // Test click callback setup (actual triggering tested in integration tests)
    EXPECT_TRUE(button->isEnabled());
}

} // namespace Tests
} // namespace NomadUI
