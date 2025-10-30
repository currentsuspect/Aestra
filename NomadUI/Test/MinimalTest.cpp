// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
/**
 * Minimal Test - Verify Core Classes Compile
 * 
 * This is a simple compilation test to verify our core classes
 * work without needing OpenGL or platform code.
 */

#include "../Core/NUITypes.h"
#include "../Core/NUIComponent.h"
#include "../Core/NUITheme.h"
#include <iostream>
#include <cassert>

using namespace NomadUI;

// ============================================================================
// Test Component
// ============================================================================

// Mock renderer for testing (no actual rendering)
class MockRenderer {
public:
    void beginFrame() {}
    void endFrame() {}
    void clear(const NUIColor&) {}
};

class TestComponent : public NUIComponent {
public:
    int updateCount = 0;
    bool mouseEventReceived = false;
    
    void onUpdate(double deltaTime) override {
        updateCount++;
        NUIComponent::onUpdate(deltaTime);
    }
    
    bool onMouseEvent(const NUIMouseEvent& event) override {
        mouseEventReceived = true;
        return NUIComponent::onMouseEvent(event);
    }
};

// ============================================================================
// Tests
// ============================================================================

void testTypes() {
    std::cout << "Testing NUITypes..." << std::endl;
    
    // Test Point
    NUIPoint p1(10.0f, 20.0f);
    assert(p1.x == 10.0f);
    assert(p1.y == 20.0f);
    
    // Test Rect
    NUIRect r1(0, 0, 100, 50);
    assert(r1.contains(50, 25));
    assert(!r1.contains(150, 25));
    assert(r1.right() == 100.0f);
    assert(r1.bottom() == 50.0f);
    
    // Test Color
    NUIColor c1 = NUIColor::fromHex(0xFF0000); // Red
    assert(c1.r > 0.9f && c1.r < 1.1f);
    assert(c1.g < 0.1f);
    assert(c1.b < 0.1f);
    
    NUIColor c2 = c1.withAlpha(0.5f);
    assert(c2.a == 0.5f);
    
    std::cout << "  âœ“ NUITypes tests passed" << std::endl;
}

void testComponent() {
    std::cout << "Testing NUIComponent..." << std::endl;
    
    // Create component
    auto comp = std::make_shared<TestComponent>();
    comp->setId("test1");
    assert(comp->getId() == "test1");
    
    // Test bounds
    comp->setBounds(10, 20, 100, 50);
    assert(comp->getX() == 10.0f);
    assert(comp->getY() == 20.0f);
    assert(comp->getWidth() == 100.0f);
    assert(comp->getHeight() == 50.0f);
    
    // Test visibility
    assert(comp->isVisible());
    comp->setVisible(false);
    assert(!comp->isVisible());
    comp->setVisible(true);
    
    // Test enabled
    assert(comp->isEnabled());
    comp->setEnabled(false);
    assert(!comp->isEnabled());
    comp->setEnabled(true);
    
    // Test hierarchy
    auto child1 = std::make_shared<TestComponent>();
    auto child2 = std::make_shared<TestComponent>();
    
    comp->addChild(child1);
    comp->addChild(child2);
    assert(comp->getChildren().size() == 2);
    assert(child1->getParent() == comp.get());
    
    comp->removeChild(child1);
    assert(comp->getChildren().size() == 1);
    
    comp->removeAllChildren();
    assert(comp->getChildren().empty());
    
    // Test dirty flag
    comp->setDirty(false);
    assert(!comp->isDirty());
    comp->setDirty(true);
    assert(comp->isDirty());
    
    std::cout << "  âœ“ NUIComponent tests passed" << std::endl;
}

void testTheme() {
    std::cout << "Testing NUITheme..." << std::endl;
    
    // Create default theme
    auto theme = NUITheme::createDefault();
    assert(theme != nullptr);
    
    // Test colors
    auto bg = theme->getBackground();
    assert(bg.r >= 0.0f && bg.r <= 1.0f);
    
    auto primary = theme->getPrimary();
    assert(primary.r > 0.0f || primary.g > 0.0f || primary.b > 0.0f);
    
    // Test dimensions
    float radius = theme->getBorderRadius();
    assert(radius > 0.0f);
    
    float padding = theme->getPadding();
    assert(padding > 0.0f);
    
    // Test effects
    float glow = theme->getGlowIntensity();
    assert(glow >= 0.0f && glow <= 1.0f);
    
    // Test font sizes
    float fontSize = theme->getFontSizeNormal();
    assert(fontSize > 0.0f);
    
    // Test custom values
    theme->setColor("custom", NUIColor::fromHex(0x123456));
    auto custom = theme->getColor("custom");
    assert(custom.r > 0.0f || custom.g > 0.0f || custom.b > 0.0f);
    
    std::cout << "  âœ“ NUITheme tests passed" << std::endl;
}

void testComponentHierarchy() {
    std::cout << "Testing Component Hierarchy..." << std::endl;
    
    // Create hierarchy
    auto root = std::make_shared<TestComponent>();
    root->setId("root");
    root->setBounds(0, 0, 800, 600);
    
    auto panel = std::make_shared<TestComponent>();
    panel->setId("panel");
    panel->setBounds(100, 100, 200, 150);
    
    auto button = std::make_shared<TestComponent>();
    button->setId("button");
    button->setBounds(10, 10, 80, 30);
    
    root->addChild(panel);
    panel->addChild(button);
    
    // Test hierarchy
    assert(root->getChildren().size() == 1);
    assert(panel->getParent() == root.get());
    assert(button->getParent() == panel.get());
    
    // Test find by ID
    auto found = root->findChildById("button");
    assert(found != nullptr);
    assert(found->getId() == "button");
    
    // Test coordinate conversion
    NUIPoint local(10, 10);
    NUIPoint global = button->localToGlobal(local);
    // Should be: button(10,10) + panel(10,10) + panel offset(100,100) = (120, 120)
    assert(global.x == 120.0f);
    assert(global.y == 120.0f);
    
    std::cout << "  âœ“ Component Hierarchy tests passed" << std::endl;
}

void testEvents() {
    std::cout << "Testing Event System..." << std::endl;
    
    auto comp = std::make_shared<TestComponent>();
    comp->setBounds(0, 0, 100, 100);
    
    // Test mouse event
    NUIMouseEvent mouseEvent;
    mouseEvent.position = NUIPoint(50, 50);
    mouseEvent.button = NUIMouseButton::Left;
    mouseEvent.pressed = true;
    
    comp->onMouseEvent(mouseEvent);
    assert(comp->mouseEventReceived);
    
    // Test hover
    assert(!comp->isHovered());
    comp->setHovered(true);
    assert(comp->isHovered());
    
    // Test focus
    assert(!comp->isFocused());
    comp->setFocused(true);
    assert(comp->isFocused());
    
    std::cout << "  âœ“ Event System tests passed" << std::endl;
}

void testThemeInheritance() {
    std::cout << "Testing Theme Inheritance..." << std::endl;
    
    auto theme = NUITheme::createDefault();
    
    auto parent = std::make_shared<TestComponent>();
    parent->setTheme(theme);
    
    auto child = std::make_shared<TestComponent>();
    parent->addChild(child);
    
    // Child should inherit parent's theme
    assert(child->getTheme() != nullptr);
    assert(child->getTheme() == theme);
    
    std::cout << "  âœ“ Theme Inheritance tests passed" << std::endl;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  Nomad UI - Minimal Core Tests" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    
    try {
        testTypes();
        testComponent();
        testTheme();
        testComponentHierarchy();
        testEvents();
        testThemeInheritance();
        
        std::cout << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "  âœ… ALL TESTS PASSED!" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << std::endl;
        std::cout << "Core classes are working correctly." << std::endl;
        std::cout << "Next: Implement OpenGL renderer and platform layer." << std::endl;
        
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << std::endl;
        std::cerr << "âŒ TEST FAILED: " << e.what() << std::endl;
        return 1;
    }
}
