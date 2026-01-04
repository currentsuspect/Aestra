// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"
#include "../Graphics/NUISVGParser.h"
#include <string>
#include <memory>

namespace NomadUI {

/**
 * Icon sizes
 */
enum class NUIIconSize {
    Small = 16,
    Medium = 24,
    Large = 32,
    XLarge = 48
};

/**
 * NUIIcon - SVG-based icon component
 * Integrates with Nomad theme system for colors
 */
class NUIIcon : public NUIComponent {
public:
    NUIIcon();
    explicit NUIIcon(const std::string& svgContent);
    ~NUIIcon() override = default;
    
    // Component interface
    void onRender(NUIRenderer& renderer) override;
    
    // Icon management
    void loadSVG(const std::string& svgContent);
    void loadSVGFile(const std::string& filePath);
    void setIconSize(NUIIconSize size);
    void setIconSize(float width, float height);
    
    // Color management
    void setColor(const NUIColor& color);
    void setColorFromTheme(const std::string& colorName);
    void clearColor(); // Use original SVG colors
    
    // Properties
    NUIColor getColor() const { return color_; }
    bool hasCustomColor() const { return hasCustomColor_; }
    float getIconWidth() const { return iconWidth_; }
    float getIconHeight() const { return iconHeight_; }
    
    // Predefined icons (inline SVG)
    static std::shared_ptr<NUIIcon> createCutIcon();
    static std::shared_ptr<NUIIcon> createCopyIcon();
    static std::shared_ptr<NUIIcon> createPasteIcon();
    static std::shared_ptr<NUIIcon> createSettingsIcon();
    static std::shared_ptr<NUIIcon> createCloseIcon();
    static std::shared_ptr<NUIIcon> createMinimizeIcon();
    static std::shared_ptr<NUIIcon> createMaximizeIcon();
    static std::shared_ptr<NUIIcon> createCheckIcon();
    static std::shared_ptr<NUIIcon> createChevronRightIcon();
    static std::shared_ptr<NUIIcon> createChevronDownIcon();
    static std::shared_ptr<NUIIcon> createTrashIcon();
    
private:
    void updateBounds();
    
    std::shared_ptr<NUISVGDocument> svgDoc_;
    NUIColor color_ = NUIColor::white();
    bool hasCustomColor_ = false;
    float iconWidth_ = 24.0f;
    float iconHeight_ = 24.0f;
};

} // namespace NomadUI
