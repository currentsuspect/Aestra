// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUIIcon.h"
#include "NUIThemeSystem.h"
#include "../Graphics/NUIRenderer.h"

namespace NomadUI {

// ============================================================================
// NUIIcon Implementation
// ============================================================================

NUIIcon::NUIIcon()
    : NUIComponent()
{
    updateBounds();
}

NUIIcon::NUIIcon(const std::string& svgContent)
    : NUIComponent()
{
    loadSVG(svgContent);
    updateBounds();
}

void NUIIcon::onRender(NUIRenderer& renderer) {
    if (!svgDoc_) return;
    
    NUIRect bounds = getBounds();
    
    if (hasCustomColor_) {
        NUISVGRenderer::render(renderer, *svgDoc_, bounds, color_);
    } else {
        NUISVGRenderer::render(renderer, *svgDoc_, bounds);
    }
}

void NUIIcon::loadSVG(const std::string& svgContent) {
    svgDoc_ = NUISVGParser::parse(svgContent);
    updateBounds();
    setDirty(true);
}

void NUIIcon::loadSVGFile(const std::string& filePath) {
    svgDoc_ = NUISVGParser::parseFile(filePath);
    updateBounds();
    setDirty(true);
}

void NUIIcon::setIconSize(NUIIconSize size) {
    float s = static_cast<float>(size);
    setIconSize(s, s);
}

void NUIIcon::setIconSize(float width, float height) {
    iconWidth_ = width;
    iconHeight_ = height;
    updateBounds();
    setDirty(true);
}

void NUIIcon::setColor(const NUIColor& color) {
    color_ = color;
    hasCustomColor_ = true;
    setDirty(true);
}

void NUIIcon::setColorFromTheme(const std::string& colorName) {
    auto& themeManager = NUIThemeManager::getInstance();
    setColor(themeManager.getColor(colorName));
}

void NUIIcon::clearColor() {
    hasCustomColor_ = false;
    setDirty(true);
}

void NUIIcon::updateBounds() {
    setSize(iconWidth_, iconHeight_);
}

// ============================================================================
// Predefined Icons
// ============================================================================

std::shared_ptr<NUIIcon> NUIIcon::createCutIcon() {
    const char* svg = R"(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <circle cx="6" cy="6" r="3"/>
            <circle cx="6" cy="18" r="3"/>
            <line x1="20" y1="4" x2="8.12" y2="15.88"/>
            <line x1="14.47" y1="14.48" x2="20" y2="20"/>
            <line x1="8.12" y1="8.12" x2="12" y2="12"/>
        </svg>
    )";
    
    auto icon = std::make_shared<NUIIcon>(svg);
    icon->setColorFromTheme("textPrimary");
    return icon;
}

std::shared_ptr<NUIIcon> NUIIcon::createCopyIcon() {
    const char* svg = R"(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <rect x="9" y="9" width="13" height="13" rx="2" ry="2"/>
            <path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/>
        </svg>
    )";
    
    auto icon = std::make_shared<NUIIcon>(svg);
    icon->setColorFromTheme("textPrimary");
    return icon;
}

std::shared_ptr<NUIIcon> NUIIcon::createPasteIcon() {
    const char* svg = R"(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <path d="M16 4h2a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2h2"/>
            <rect x="8" y="2" width="8" height="4" rx="1" ry="1"/>
        </svg>
    )";
    
    auto icon = std::make_shared<NUIIcon>(svg);
    icon->setColorFromTheme("textPrimary");
    return icon;
}

std::shared_ptr<NUIIcon> NUIIcon::createSettingsIcon() {
    const char* svg = R"(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <path d="M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z"/>
            <circle cx="12" cy="12" r="3"/>
        </svg>
    )";
    
    auto icon = std::make_shared<NUIIcon>(svg);
    icon->setColorFromTheme("textPrimary");
    return icon;
}

std::shared_ptr<NUIIcon> NUIIcon::createCloseIcon() {
    const char* svg = R"(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <line x1="18" y1="6" x2="6" y2="18"/>
            <line x1="6" y1="6" x2="18" y2="18"/>
        </svg>
    )";
    
    auto icon = std::make_shared<NUIIcon>(svg);
    icon->setColorFromTheme("textPrimary");
    return icon;
}

std::shared_ptr<NUIIcon> NUIIcon::createMinimizeIcon() {
    const char* svg = R"(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <line x1="5" y1="12" x2="19" y2="12"/>
        </svg>
    )";
    
    auto icon = std::make_shared<NUIIcon>(svg);
    icon->setColorFromTheme("textPrimary");
    return icon;
}

std::shared_ptr<NUIIcon> NUIIcon::createMaximizeIcon() {
    const char* svg = R"(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <rect x="3" y="3" width="18" height="18" rx="2" ry="2"/>
        </svg>
    )";
    
    auto icon = std::make_shared<NUIIcon>(svg);
    icon->setColorFromTheme("textPrimary");
    return icon;
}

std::shared_ptr<NUIIcon> NUIIcon::createCheckIcon() {
    const char* svg = R"(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <polyline points="20 6 9 17 4 12"/>
        </svg>
    )";
    
    auto icon = std::make_shared<NUIIcon>(svg);
    icon->setColorFromTheme("success");
    return icon;
}

std::shared_ptr<NUIIcon> NUIIcon::createChevronRightIcon() {
    const char* svg = R"(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <polyline points="9 18 15 12 9 6"/>
        </svg>
    )";
    
    auto icon = std::make_shared<NUIIcon>(svg);
    icon->setColorFromTheme("textSecondary");
    return icon;
}

std::shared_ptr<NUIIcon> NUIIcon::createChevronDownIcon() {
    const char* svg = R"(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <polyline points="6 9 12 15 18 9"/>
        </svg>
    )";
    
    auto icon = std::make_shared<NUIIcon>(svg);
    icon->setColorFromTheme("textSecondary");
    return icon;
}

} // namespace NomadUI
