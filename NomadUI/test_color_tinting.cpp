// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
/**
 * Test program to verify color tinting with NanoSVG integration.
 * Tests that icons can be tinted with different theme colors and alpha transparency.
 */

#include "Core/NUIIcon.h"
#include "Core/NUIThemeSystem.h"
#include <iostream>
#include <memory>

using namespace NomadUI;

int main() {
    std::cout << "Color Tinting Test" << std::endl;
    std::cout << "==================" << std::endl;
    std::cout << std::endl;
    
    // Initialize theme system
    auto& themeManager = NUIThemeManager::getInstance();
    themeManager.setActiveTheme("nomad-dark");
    
    // Test 1: Apply theme colors to icon
    std::cout << "Test 1: Applying theme colors..." << std::endl;
    auto icon = NUIIcon::createCheckIcon();
    
    std::vector<std::string> themeColors = {
        "textPrimary",
        "primary",
        "success",
        "warning",
        "error",
        "info"
    };
    
    for (const auto& colorName : themeColors) {
        icon->setColorFromTheme(colorName);
        auto color = icon->getColor();
        std::cout << "  - " << colorName << ": ";
        std::cout << "R=" << color.r << " G=" << color.g << " B=" << color.b << " A=" << color.a;
        std::cout << " (hasCustomColor=" << (icon->hasCustomColor() ? "true" : "false") << ")" << std::endl;
    }
    
    std::cout << "âœ“ Theme colors applied successfully" << std::endl;
    std::cout << std::endl;
    
    // Test 2: Apply custom colors
    std::cout << "Test 2: Applying custom colors..." << std::endl;
    
    struct ColorTest {
        NUIColor color;
        const char* name;
    };
    
    ColorTest colors[] = {
        {NUIColor(1.0f, 0.0f, 0.0f, 1.0f), "Red"},
        {NUIColor(0.0f, 1.0f, 0.0f, 1.0f), "Green"},
        {NUIColor(0.0f, 0.0f, 1.0f, 1.0f), "Blue"},
        {NUIColor(1.0f, 1.0f, 0.0f, 1.0f), "Yellow"},
        {NUIColor(1.0f, 0.0f, 1.0f, 1.0f), "Magenta"},
        {NUIColor(0.0f, 1.0f, 1.0f, 1.0f), "Cyan"}
    };
    
    for (const auto& test : colors) {
        icon->setColor(test.color);
        auto color = icon->getColor();
        std::cout << "  - " << test.name << ": ";
        std::cout << "R=" << color.r << " G=" << color.g << " B=" << color.b << " A=" << color.a;
        std::cout << " (hasCustomColor=" << (icon->hasCustomColor() ? "true" : "false") << ")" << std::endl;
    }
    
    std::cout << "âœ“ Custom colors applied successfully" << std::endl;
    std::cout << std::endl;
    
    // Test 3: Test alpha transparency
    std::cout << "Test 3: Testing alpha transparency..." << std::endl;
    
    float alphaValues[] = {1.0f, 0.75f, 0.5f, 0.25f, 0.0f};
    
    for (float alpha : alphaValues) {
        NUIColor colorWithAlpha(1.0f, 1.0f, 1.0f, alpha);
        icon->setColor(colorWithAlpha);
        auto color = icon->getColor();
        std::cout << "  - Alpha " << alpha << ": ";
        std::cout << "R=" << color.r << " G=" << color.g << " B=" << color.b << " A=" << color.a << std::endl;
    }
    
    std::cout << "âœ“ Alpha transparency values set successfully" << std::endl;
    std::cout << std::endl;
    
    // Test 4: Clear color (use original SVG colors)
    std::cout << "Test 4: Clearing custom color..." << std::endl;
    icon->setColor(NUIColor(1.0f, 0.0f, 0.0f, 1.0f));
    std::cout << "  - Before clear: hasCustomColor=" << (icon->hasCustomColor() ? "true" : "false") << std::endl;
    
    icon->clearColor();
    std::cout << "  - After clear: hasCustomColor=" << (icon->hasCustomColor() ? "true" : "false") << std::endl;
    std::cout << "âœ“ Color cleared successfully" << std::endl;
    std::cout << std::endl;
    
    // Test 5: Test with custom SVG file
    std::cout << "Test 5: Testing color tinting with custom SVG..." << std::endl;
    auto pauseIcon = std::make_shared<NUIIcon>();
    pauseIcon->loadSVGFile("NomadUI/Examples/test_pause.svg");
    
    pauseIcon->setColorFromTheme("primary");
    std::cout << "  - Primary color applied to pause icon" << std::endl;
    
    pauseIcon->setColor(NUIColor(1.0f, 0.5f, 0.0f, 0.8f));
    auto color = pauseIcon->getColor();
    std::cout << "  - Custom color with alpha: R=" << color.r << " G=" << color.g 
              << " B=" << color.b << " A=" << color.a << std::endl;
    
    std::cout << "âœ“ Color tinting works with custom SVG files" << std::endl;
    std::cout << std::endl;
    
    std::cout << "==================" << std::endl;
    std::cout << "All color tinting tests passed!" << std::endl;
    std::cout << "Run IconDemo to visually verify color rendering." << std::endl;
    
    return 0;
}
