// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUIThemeSystem.h"
#include <algorithm>
#include <cmath>

namespace NomadUI {

// NUIThemeManager Implementation
NUIThemeManager& NUIThemeManager::getInstance() {
    static NUIThemeManager instance;
    return instance;
}

NUIThemeManager::NUIThemeManager() 
    : currentVariant_(NUIThemeVariant::Dark)
    , activeTheme_("nomad-dark")
    , isTransitioning_(false) {
    initializeDefaultThemes();
}

void NUIThemeManager::initializeDefaultThemes() {
    // Nomad Dark Theme
    NUIThemeProperties nomadDark = NUIThemePresets::createNomadDark();
    themes_["nomad-dark"] = nomadDark;
    
    // Nomad Light Theme
    NUIThemeProperties nomadLight = NUIThemePresets::createNomadLight();
    themes_["nomad-light"] = nomadLight;
    
    // Material Themes
    themes_["material-light"] = NUIThemePresets::createMaterialLight();
    themes_["material-dark"] = NUIThemePresets::createMaterialDark();
    
    // Fluent Themes
    themes_["fluent-light"] = NUIThemePresets::createFluentLight();
    themes_["fluent-dark"] = NUIThemePresets::createFluentDark();
    
    // Cupertino Themes
    themes_["cupertino-light"] = NUIThemePresets::createCupertinoLight();
    themes_["cupertino-dark"] = NUIThemePresets::createCupertinoDark();
    
    // High Contrast Themes
    themes_["high-contrast-light"] = NUIThemePresets::createHighContrastLight();
    themes_["high-contrast-dark"] = NUIThemePresets::createHighContrastDark();
}

void NUIThemeManager::setThemeVariant(NUIThemeVariant variant) {
    currentVariant_ = variant;
    updateSystemTheme();
}

void NUIThemeManager::setCustomTheme(const std::string& name, const NUIThemeProperties& properties) {
    themes_[name] = properties;
}

void NUIThemeManager::setActiveTheme(const std::string& name) {
    if (themes_.find(name) != themes_.end()) {
        activeTheme_ = name;
        if (onThemeChanged_) {
            onThemeChanged_(getCurrentTheme());
        }
    }
}

const NUIThemeProperties& NUIThemeManager::getCurrentTheme() const {
    auto it = themes_.find(activeTheme_);
    if (it != themes_.end()) {
        return it->second;
    }
    return themes_.at("nomad-dark");
}

NUIThemeProperties& NUIThemeManager::getCurrentThemeMutable() {
    return themes_[activeTheme_];
}

void NUIThemeManager::switchTheme(const std::string& name, float durationMs) {
    if (themes_.find(name) == themes_.end()) return;
    
    if (durationMs <= 0.0f) {
        setActiveTheme(name);
        return;
    }
    
    isTransitioning_ = true;
    transitionFromTheme_ = getCurrentTheme();
    transitionToTheme_ = themes_[name];
    
    themeTransitionAnimation_ = std::make_shared<NUIAnimation>();
    themeTransitionAnimation_->setDuration(durationMs);
    themeTransitionAnimation_->setEasing(NUIEasingType::EaseOutCubic);
    themeTransitionAnimation_->setStartValue(0.0f);
    themeTransitionAnimation_->setEndValue(1.0f);
    
    themeTransitionAnimation_->setOnUpdate([this](float progress) {
        // Interpolate between themes
        NUIThemeProperties currentTheme;
        currentTheme.background = NUIColor::lerpHSL(transitionFromTheme_.background, transitionToTheme_.background, progress);
        currentTheme.surface = NUIColor::lerpHSL(transitionFromTheme_.surface, transitionToTheme_.surface, progress);
        currentTheme.primary = NUIColor::lerpHSL(transitionFromTheme_.primary, transitionToTheme_.primary, progress);
        // ... interpolate other properties as needed
        
        if (onThemeChanged_) {
            onThemeChanged_(currentTheme);
        }
    });
    
    themeTransitionAnimation_->setOnComplete([this]() {
        isTransitioning_ = false;
        // Note: activeTheme_ should be set before calling switchTheme
        if (onThemeChanged_) {
            onThemeChanged_(getCurrentTheme());
        }
    });
    
    themeTransitionAnimation_->start();
    NUIAnimationManager::getInstance().addAnimation(themeTransitionAnimation_);
}

void NUIThemeManager::switchThemeVariant(NUIThemeVariant variant, float durationMs) {
    std::string targetTheme;
    switch (variant) {
        case NUIThemeVariant::Light:
            targetTheme = "nomad-light";
            break;
        case NUIThemeVariant::Dark:
            targetTheme = "nomad-dark";
            break;
        case NUIThemeVariant::Auto:
            // TODO: Detect system preference
            targetTheme = "nomad-dark";
            break;
    }
    
    switchTheme(targetTheme, durationMs);
}

void NUIThemeManager::setOnThemeChanged(std::function<void(const NUIThemeProperties&)> callback) {
    onThemeChanged_ = callback;
}

NUIColor NUIThemeManager::getColor(const std::string& colorName) const {
    const auto& theme = getCurrentTheme();
    
    // Core Structure
    if (colorName == "backgroundPrimary") return theme.backgroundPrimary;
    if (colorName == "backgroundSecondary") return theme.backgroundSecondary;
    if (colorName == "surfaceTertiary") return theme.surfaceTertiary;
    if (colorName == "surfaceRaised") return theme.surfaceRaised;
    
    // Legacy compatibility
    if (colorName == "background") return theme.background;
    if (colorName == "surface") return theme.surface;
    if (colorName == "surfaceVariant") return theme.surfaceVariant;
    
    // Accent & Branding
    if (colorName == "primary") return theme.primary;
    if (colorName == "primaryHover") return theme.primaryHover;
    if (colorName == "primaryPressed") return theme.primaryPressed;
    if (colorName == "accent") return theme.primary;
    if (colorName == "secondary") return theme.secondary;
    
    // Functional Colors
    if (colorName == "success") return theme.success;
    if (colorName == "warning") return theme.warning;
    if (colorName == "error") return theme.error;
    if (colorName == "info") return theme.info;
    
    // Liminal Dark v2.0 Accent Colors
    if (colorName == "accentCyan") return theme.accentCyan;
    if (colorName == "accentMagenta") return theme.accentMagenta;
    if (colorName == "accentLime") return theme.accentLime;
    if (colorName == "accentPrimary") return theme.accentPrimary;
    if (colorName == "accentSecondary") return theme.accentSecondary;
    
    // Text
    if (colorName == "text") return theme.textPrimary;
    if (colorName == "textPrimary") return theme.textPrimary;
    if (colorName == "textSecondary") return theme.textSecondary;
    if (colorName == "textDisabled") return theme.textDisabled;
    if (colorName == "textLink") return theme.textLink;
    if (colorName == "textCritical") return theme.textCritical;
    
    // Borders
    if (colorName == "border") return theme.border;
    if (colorName == "borderSubtle") return theme.borderSubtle;
    if (colorName == "borderActive") return theme.borderActive;
    if (colorName == "divider") return theme.divider;
    
    // Interactive States
    if (colorName == "hover") return theme.hover;
    if (colorName == "pressed") return theme.pressed;
    if (colorName == "focused") return theme.focused;
    
    // FL Studio-inspired dropdown theme tokens
    if (colorName == "dropdown.background") return theme.surfaceTertiary.withAlpha(0.95f);
    if (colorName == "dropdown.border") return NUIColor(0, 0, 0, 1.0f); // Black border
    if (colorName == "dropdown.hover") return NUIColor(0.471f, 0.353f, 1.0f, 0.15f); // FL Studio lavender hover
    if (colorName == "dropdown.focus") return theme.primary.withAlpha(0.8f);
    if (colorName == "dropdown.text") return theme.textPrimary;
    if (colorName == "dropdown.arrow") return theme.textSecondary;
    
    // Dropdown list container - black borders
    if (colorName == "dropdown.list.background") return theme.surfaceTertiary.withAlpha(0.98f);
    if (colorName == "dropdown.list.border") return NUIColor(0, 0, 0, 1.0f); // Black border
    if (colorName == "dropdown.item.divider") return NUIColor(0, 0, 0, 0.3f); // Black divider between items
    
    // Dropdown items - normal text by default, purple only for selected
    if (colorName == "dropdown.item.background") return NUIColor(0, 0, 0, 0); // Transparent by default
    if (colorName == "dropdown.item.text") return theme.textPrimary; // Normal text color by default
    if (colorName == "dropdown.item.hover") return theme.primary.withAlpha(0.2f); // Purple strip on hover
    if (colorName == "dropdown.item.hoverText") return theme.textPrimary; // Normal text on hover
    if (colorName == "dropdown.item.selectedText") return theme.primary; // Purple text for selected only
    if (colorName == "dropdown.item.disabled") return theme.textDisabled;
    
    if (colorName == "selected") return theme.selected;
    
    // Interactive Elements
    if (colorName == "buttonBgDefault") return theme.buttonBgDefault;
    if (colorName == "buttonBgHover") return theme.buttonBgHover;
    if (colorName == "buttonBgActive") return theme.buttonBgActive;
    if (colorName == "buttonTextDefault") return theme.buttonTextDefault;
    if (colorName == "buttonTextActive") return theme.buttonTextActive;
    
    if (colorName == "toggleDefault") return theme.toggleDefault;
    if (colorName == "toggleHover") return theme.toggleHover;
    if (colorName == "toggleActive") return theme.toggleActive;
    
    if (colorName == "inputBgDefault") return theme.inputBgDefault;
    if (colorName == "inputBgHover") return theme.inputBgHover;
    if (colorName == "inputBorderFocus") return theme.inputBorderFocus;
    
    if (colorName == "sliderTrack") return theme.sliderTrack;
    if (colorName == "sliderHandle") return theme.sliderHandle;
    if (colorName == "sliderHandleHover") return theme.sliderHandleHover;
    if (colorName == "sliderHandlePressed") return theme.sliderHandlePressed;
    
    if (colorName == "highlightGlow") return theme.highlightGlow;
    
    // Glass Aesthetic tokens
    if (colorName == "glassHover") return theme.glassHover;
    if (colorName == "glassBorder") return theme.glassBorder;
    if (colorName == "glassActive") return theme.glassActive;
    
    // === Arsenal / Step Sequencer Tokens ===
    // Step Grid Colors
    if (colorName == "stepActive") return theme.primary;                              // Active step (on)
    if (colorName == "stepInactive") return theme.surfaceRaised;                      // Inactive step (off)
    if (colorName == "stepHover") return theme.hover;                                 // Step hover state
    if (colorName == "stepTriggerGlow") return theme.primary.withAlpha(0.6f);         // Glow on pad hit
    if (colorName == "stepBeatMarker") return theme.borderSubtle.lightened(0.1f);     // Beat 1/4 markers
    if (colorName == "stepBarMarker") return theme.borderActive;                       // Bar markers
    
    // Arsenal Panel Colors
    if (colorName == "arsenalBackground") return theme.backgroundSecondary;            // Arsenal panel bg
    if (colorName == "arsenalRowEven") return theme.surfaceRaised;                     // Even row
    if (colorName == "arsenalRowOdd") return theme.surfaceRaised.darkened(0.02f);      // Odd row (subtle zebra)
    if (colorName == "arsenalAccent") return theme.accentCyan;                         // Accent for highlights
    
    // Grid Tokens (for TrackManagerUI/TrackUIComponent)
    if (colorName == "gridBar") return theme.border.withAlpha(0.60f);                  // Bar line
    if (colorName == "gridBeat") return theme.border.withAlpha(0.30f);                 // Beat line
    if (colorName == "gridSubdivision") return theme.border.withAlpha(0.16f);          // Sub-beat line
    
    // Waveform Preview Tokens
    if (colorName == "waveformFill") return theme.accentCyan.withAlpha(0.7f);          // Waveform fill color
    if (colorName == "waveformLine") return theme.accentCyan;                          // Waveform outline
    if (colorName == "waveformBackground") return theme.backgroundPrimary;             // Preview background

    // Red Accent (for Record, Arm states)
    if (colorName == "accentRed") return NUIColor(0.95f, 0.25f, 0.35f, 1.0f);         // Record red
    
    return theme.primary; // Default fallback
}

float NUIThemeManager::getSpacing(const std::string& spacingName) const {
    const auto& theme = getCurrentTheme();
    
    if (spacingName == "xs") return theme.spacingXS;
    if (spacingName == "s") return theme.spacingS;
    if (spacingName == "m") return theme.spacingM;
    if (spacingName == "l") return theme.spacingL;
    if (spacingName == "xl") return theme.spacingXL;
    if (spacingName == "xxl") return theme.spacingXXL;
    
    return theme.spacingM; // Default fallback
}

float NUIThemeManager::getRadius(const std::string& radiusName) const {
    const auto& theme = getCurrentTheme();
    
    if (radiusName == "xs") return theme.radiusXS;
    if (radiusName == "s") return theme.radiusS;
    if (radiusName == "m") return theme.radiusM;
    if (radiusName == "l") return theme.radiusL;
    if (radiusName == "xl") return theme.radiusXL;
    if (radiusName == "xxl") return theme.radiusXXL;
    
    return theme.radiusM; // Default fallback
}

float NUIThemeManager::getFontSize(const std::string& fontSizeName) const {
    const auto& theme = getCurrentTheme();
    
    if (fontSizeName == "xs") return theme.fontSizeXS;
    if (fontSizeName == "s") return theme.fontSizeS;
    if (fontSizeName == "m") return theme.fontSizeM;
    if (fontSizeName == "l") return theme.fontSizeL;
    if (fontSizeName == "xl") return theme.fontSizeXL;
    if (fontSizeName == "xxl") return theme.fontSizeXXL;
    if (fontSizeName == "h1") return theme.fontSizeH1;
    if (fontSizeName == "h2") return theme.fontSizeH2;
    if (fontSizeName == "h3") return theme.fontSizeH3;
    
    return theme.fontSizeM; // Default fallback
}

NUIThemeProperties::Shadow NUIThemeManager::getShadow(const std::string& shadowName) const {
    const auto& theme = getCurrentTheme();

    if (shadowName == "xs") return theme.shadowXS;
    if (shadowName == "s") return theme.shadowS;
    if (shadowName == "m") return theme.shadowM;
    if (shadowName == "l") return theme.shadowL;
    if (shadowName == "xl") return theme.shadowXL;

    return theme.shadowM; // Default fallback
}

float NUIThemeManager::getLayoutDimension(const std::string& dimensionName) const {
    const auto& theme = getCurrentTheme();
    const auto& layout = theme.layout;

    // Panel dimensions
    if (dimensionName == "fileBrowserWidth") return layout.fileBrowserWidth;
    if (dimensionName == "trackControlsWidth") return layout.trackControlsWidth;
    if (dimensionName == "timelineAreaWidth") return layout.timelineAreaWidth;

    // Track dimensions
    if (dimensionName == "trackHeight") return layout.trackHeight;
    if (dimensionName == "trackSpacing") return layout.trackSpacing;
    if (dimensionName == "trackLabelHeight") return layout.trackLabelHeight;

    // Transport bar dimensions
    if (dimensionName == "transportBarHeight") return layout.transportBarHeight;
    if (dimensionName == "transportButtonSize") return layout.transportButtonSize;
    if (dimensionName == "transportButtonSpacing") return layout.transportButtonSpacing;

    // Control dimensions
    if (dimensionName == "controlButtonWidth") return layout.controlButtonWidth;
    if (dimensionName == "controlButtonHeight") return layout.controlButtonHeight;
    if (dimensionName == "controlButtonSpacing") return layout.controlButtonSpacing;
    if (dimensionName == "controlButtonStartX") return layout.controlButtonStartX;

    // Grid and timeline
    if (dimensionName == "gridLineSpacing") return layout.gridLineSpacing;
    if (dimensionName == "timelineHeight") return layout.timelineHeight;

    // Margins and padding
    if (dimensionName == "panelMargin") return layout.panelMargin;
    if (dimensionName == "componentPadding") return layout.componentPadding;
    if (dimensionName == "buttonPadding") return layout.buttonPadding;

    // Window dimensions
    if (dimensionName == "minWindowWidth") return layout.minWindowWidth;
    if (dimensionName == "minWindowHeight") return layout.minWindowHeight;
    if (dimensionName == "defaultWindowWidth") return layout.defaultWindowWidth;
    if (dimensionName == "defaultWindowHeight") return layout.defaultWindowHeight;

    return 0.0f; // Default fallback
}

const NUIThemeProperties::LayoutDimensions& NUIThemeManager::getLayoutDimensions() const {
    return getCurrentTheme().layout;
}

float NUIThemeManager::getComponentDimension(const std::string& componentName, const std::string& dimensionName) const {
    const auto& theme = getCurrentTheme();

    // File Browser dimensions
    if (componentName == "fileBrowser") {
        if (dimensionName == "itemHeight") return 36.0f; // Slightly taller rows for larger text
        if (dimensionName == "iconSize") return 24.0f;
        if (dimensionName == "indentSize") return 16.0f;
        if (dimensionName == "hoverOpacity") return 0.1f;
        if (dimensionName == "scrollbarWidth") return 8.0f;
        if (dimensionName == "headerHeight") return 60.0f;
    }

    // Track Controls dimensions
    if (componentName == "trackControls") {
        if (dimensionName == "muteButtonSize") return 25.0f; // Width
        if (dimensionName == "soloButtonSize") return 25.0f; // Width
        if (dimensionName == "recordButtonSize") return 25.0f; // Width
        if (dimensionName == "buttonSpacing") return 5.0f;
        if (dimensionName == "buttonStartX") return 100.0f;
    }

    // Transport Bar dimensions
    if (componentName == "transportBar") {
        if (dimensionName == "playButtonSize") return 40.0f;
        if (dimensionName == "stopButtonSize") return 40.0f;
        if (dimensionName == "recordButtonSize") return 40.0f;
        if (dimensionName == "buttonSpacing") return 8.0f;
        if (dimensionName == "labelHeight") return 30.0f;
    }

    return 0.0f; // Default fallback
}

NUIColor NUIThemeManager::getContrastColor(const NUIColor& backgroundColor) const {
    return backgroundColor.textColor();
}

NUIColor NUIThemeManager::getHoverColor(const NUIColor& baseColor) const {
    return baseColor.withLightness(std::min(1.0f, baseColor.toHSL().l + 0.1f));
}

NUIColor NUIThemeManager::getPressedColor(const NUIColor& baseColor) const {
    return baseColor.withLightness(std::max(0.0f, baseColor.toHSL().l - 0.1f));
}

NUIColor NUIThemeManager::getDisabledColor(const NUIColor& baseColor) const {
    return baseColor.withAlpha(0.38f);
}

std::shared_ptr<NUIAnimation> NUIThemeManager::createColorTransition(
    const NUIColor& from, const NUIColor& to, float durationMs) const {
    
    if (durationMs < 0.0f) {
        durationMs = getCurrentTheme().durationNormal;
    }
    
    auto animation = std::make_shared<NUIAnimation>();
    animation->setDuration(durationMs);
    animation->setEasing(getCurrentTheme().easingStandard);
    animation->setStartValue(0.0f);
    animation->setEndValue(1.0f);
    
    return animation;
}

void NUIThemeManager::updateSystemTheme() {
    // TODO: Implement system theme detection
    // For now, just switch to appropriate theme
    if (currentVariant_ == NUIThemeVariant::Light) {
        setActiveTheme("nomad-light");
    } else {
        setActiveTheme("nomad-dark");
    }
}

// NUIThemedComponent Implementation
void NUIThemedComponent::registerForThemeUpdates() {
    if (!isThemeRegistered_) {
        // TODO: Register with theme manager
        isThemeRegistered_ = true;
    }
}

void NUIThemedComponent::unregisterFromThemeUpdates() {
    if (isThemeRegistered_) {
        // TODO: Unregister from theme manager
        isThemeRegistered_ = false;
    }
}

NUIColor NUIThemedComponent::getThemeColor(const std::string& colorName) const {
    return NUIThemeManager::getInstance().getColor(colorName);
}

float NUIThemedComponent::getThemeSpacing(const std::string& spacingName) const {
    return NUIThemeManager::getInstance().getSpacing(spacingName);
}

float NUIThemedComponent::getThemeRadius(const std::string& radiusName) const {
    return NUIThemeManager::getInstance().getRadius(radiusName);
}

float NUIThemedComponent::getThemeFontSize(const std::string& fontSizeName) const {
    return NUIThemeManager::getInstance().getFontSize(fontSizeName);
}

float NUIThemedComponent::getThemeLayoutDimension(const std::string& dimensionName) const {
    return NUIThemeManager::getInstance().getLayoutDimension(dimensionName);
}

float NUIThemedComponent::getThemeComponentDimension(const std::string& componentName, const std::string& dimensionName) const {
    return NUIThemeManager::getInstance().getComponentDimension(componentName, dimensionName);
}

// NUIThemePresets Implementation
NUIThemeProperties NUIThemePresets::createNomadDark() {
    NUIThemeProperties theme;
    
    // Core Structure - PREMIUM BLACK V4 (Unified Rich Black)
    theme.backgroundPrimary = NUIColor(0.05f, 0.05f, 0.06f, 1.0f);       // Deepest Slate/Black (Title Bar)
    theme.backgroundSecondary = NUIColor(0.05f, 0.05f, 0.06f, 1.0f);     // Match Primary! (Pattern Browser / Sidebars)
    theme.surfaceTertiary = NUIColor(0.05f, 0.05f, 0.06f, 1.0f);         // Match Primary! (Arsenal / Panels)
    theme.surfaceRaised = NUIColor(0.08f, 0.08f, 0.09f, 1.0f);           // Lighter elements (Cards)
    
    // Legacy compatibility
    theme.background = theme.backgroundPrimary;
    theme.surface = theme.backgroundSecondary;
    theme.surfaceVariant = theme.surfaceTertiary;

    // ... (keep middle sections) ...

    // Glass Aesthetic (v9.1 Premium Balanced)
    theme.glassHover = NUIColor(1.0f, 1.0f, 1.0f, 0.04f);                // Subtle hover
    theme.glassBorder = NUIColor(1.0f, 1.0f, 1.0f, 0.06f);               // Very faint edge definition
    theme.glassActive = theme.primary.withAlpha(0.15f);                  // Clearer active state
    
    // Accent & Branding
    theme.primary = NUIColor(0.471f, 0.353f, 1.0f, 1.0f);                // #785aff - Vibrant purple
    theme.primaryHover = NUIColor(0.549f, 0.451f, 1.0f, 1.0f);           // #8c73ff - Lighter purple
    theme.primaryPressed = NUIColor(0.392f, 0.275f, 0.863f, 1.0f);       // #6446dc - Darker purple
    theme.primaryVariant = theme.primaryPressed;
    
    theme.secondary = NUIColor(0.0f, 0.831f, 0.737f, 1.0f);              // #00d4bc - Teal accent
    theme.secondaryVariant = NUIColor(0.0f, 0.698f, 0.620f, 1.0f);       // #00b29e - Darker teal
    
    // Standard Accent Colors
    theme.accentCyan = NUIColor(0.0f, 0.831f, 0.737f, 1.0f);
    theme.accentMagenta = NUIColor(0.863f, 0.275f, 0.588f, 1.0f);
    theme.accentLime = NUIColor(0.620f, 0.941f, 0.380f, 1.0f);
    theme.accentPrimary = theme.primary;
    theme.accentSecondary = theme.secondary;
    
    // Functional Colors
    theme.success = NUIColor(0.0f, 0.831f, 0.620f, 1.0f);
    theme.warning = NUIColor(1.0f, 0.706f, 0.0f, 1.0f);
    theme.error = NUIColor(1.0f, 0.267f, 0.396f, 1.0f);
    theme.info = NUIColor(0.471f, 0.353f, 1.0f, 1.0f);
    
    // Text
    theme.textPrimary = NUIColor(0.933f, 0.933f, 0.949f, 1.0f);
    theme.textSecondary = NUIColor(0.667f, 0.667f, 0.698f, 1.0f);
    theme.textDisabled = NUIColor(0.502f, 0.502f, 0.533f, 1.0f);
    theme.textLink = theme.primary;
    theme.textCritical = theme.error;
    
    // Borders
    theme.borderSubtle = NUIColor(0.196f, 0.196f, 0.220f, 1.0f);
    theme.borderActive = theme.primary;
    theme.border = theme.borderSubtle;
    theme.divider = NUIColor(0.157f, 0.157f, 0.176f, 1.0f);
    theme.outline = NUIColor(0.392f, 0.392f, 0.431f, 1.0f);
    theme.outlineVariant = NUIColor(0.275f, 0.275f, 0.306f, 1.0f);
    
    // Buttons
    theme.buttonBgDefault = theme.surfaceTertiary;
    theme.buttonBgHover = NUIColor(0.196f, 0.196f, 0.220f, 1.0f);
    theme.buttonBgActive = theme.primary;
    theme.buttonTextDefault = theme.textPrimary;
    theme.buttonTextActive = NUIColor(1.0f, 1.0f, 1.0f, 1.0f);
    
    // Toggle
    theme.toggleDefault = NUIColor(0.235f, 0.235f, 0.259f, 1.0f);
    theme.toggleHover = NUIColor(0.275f, 0.275f, 0.306f, 1.0f);
    theme.toggleActive = theme.primary;
    
    // Sliders
    theme.sliderTrack = NUIColor(0.196f, 0.196f, 0.220f, 1.0f);
    theme.sliderHandle = theme.primary;
    theme.sliderHandleHover = theme.primaryHover;
    theme.sliderHandlePressed = theme.primaryPressed;
    
    // States
    theme.hover = NUIColor(1.0f, 1.0f, 1.0f, 0.08f);
    theme.pressed = NUIColor(1.0f, 1.0f, 1.0f, 0.12f);
    theme.focused = theme.primary.withAlpha(0.2f);
    theme.selected = theme.primary.withAlpha(0.15f);
    theme.disabled = NUIColor(0.5f, 0.5f, 0.5f, 0.38f);
    
    // Highlight glow
    theme.highlightGlow = NUIColor(0.471f, 0.353f, 1.0f, 0.25f);
    
    // Shadows
    theme.shadowXS = NUIThemeProperties::Shadow(0, 1, 2, 0, NUIColor::black(), 0.1f);
    theme.shadowS = NUIThemeProperties::Shadow(0, 2, 4, 0, NUIColor::black(), 0.15f);
    theme.shadowM = NUIThemeProperties::Shadow(0, 4, 8, 0, NUIColor::black(), 0.4f);
    theme.shadowL = NUIThemeProperties::Shadow(0, 8, 16, 0, NUIColor::black(), 0.6f);
    theme.shadowXL = NUIThemeProperties::Shadow(0, 16, 32, 0, NUIColor::black(), 0.6f);
    
    // Glass Aesthetic (v9.0 Systematic)
    theme.glassHover = NUIColor(1.0f, 1.0f, 1.0f, 0.08f);
    theme.glassBorder = NUIColor(1.0f, 1.0f, 1.0f, 0.08f);
    theme.glassActive = theme.primary.withAlpha(0.20f);
    
    return theme;
}

NUIThemeProperties NUIThemePresets::createNomadLight() {
    NUIThemeProperties theme;
    
    // Colors
    theme.background = NUIColor(0.98f, 0.98f, 0.98f, 1.0f);
    theme.surface = NUIColor(1.0f, 1.0f, 1.0f, 1.0f);
    theme.surfaceVariant = NUIColor(0.95f, 0.95f, 0.95f, 1.0f);
    theme.primary = NUIColor(0.2f, 0.4f, 0.8f, 1.0f);
    theme.primaryVariant = NUIColor(0.1f, 0.3f, 0.7f, 1.0f);
    theme.secondary = NUIColor(0.4f, 0.4f, 0.5f, 1.0f);
    theme.secondaryVariant = NUIColor(0.3f, 0.3f, 0.4f, 1.0f);
    theme.error = NUIColor(0.8f, 0.2f, 0.2f, 1.0f);
    theme.warning = NUIColor(0.9f, 0.6f, 0.1f, 1.0f);
    theme.success = NUIColor(0.2f, 0.7f, 0.3f, 1.0f);
    theme.info = NUIColor(0.1f, 0.6f, 0.8f, 1.0f);
    
    // Text colors
    theme.textPrimary = NUIColor(0.1f, 0.1f, 0.1f, 1.0f);
    theme.textSecondary = NUIColor(0.4f, 0.4f, 0.4f, 1.0f);
    theme.textDisabled = NUIColor(0.6f, 0.6f, 0.6f, 1.0f);
    
    // Interactive states
    theme.hover = NUIColor(0.0f, 0.0f, 0.0f, 0.04f);
    theme.pressed = NUIColor(0.0f, 0.0f, 0.0f, 0.08f);
    theme.focused = theme.primary.withAlpha(0.12f);
    theme.selected = theme.primary.withAlpha(0.08f);
    theme.disabled = NUIColor(0.6f, 0.6f, 0.6f, 0.38f);
    
    // Borders
    theme.border = NUIColor(0.8f, 0.8f, 0.8f, 1.0f);
    theme.divider = NUIColor(0.9f, 0.9f, 0.9f, 1.0f);
    theme.outline = NUIColor(0.7f, 0.7f, 0.7f, 1.0f);
    theme.outlineVariant = NUIColor(0.85f, 0.85f, 0.85f, 1.0f);
    
    // Shadows
    theme.shadowXS = NUIThemeProperties::Shadow(0, 1, 2, 0, NUIColor::black(), 0.05f);
    theme.shadowS = NUIThemeProperties::Shadow(0, 2, 4, 0, NUIColor::black(), 0.08f);
    theme.shadowM = NUIThemeProperties::Shadow(0, 4, 8, 0, NUIColor::black(), 0.12f);
    theme.shadowL = NUIThemeProperties::Shadow(0, 8, 16, 0, NUIColor::black(), 0.15f);
    theme.shadowXL = NUIThemeProperties::Shadow(0, 16, 32, 0, NUIColor::black(), 0.2f);

    // Glass Aesthetic (v9.0 Systematic)
    theme.glassHover = NUIColor(0.0f, 0.0f, 0.0f, 0.05f);
    theme.glassBorder = NUIColor(0.0f, 0.0f, 0.0f, 0.12f);
    theme.glassActive = theme.primary.withAlpha(0.12f);
    
    return theme;
}

NUIThemeProperties NUIThemePresets::createMaterialLight() { return createNomadLight(); }
NUIThemeProperties NUIThemePresets::createMaterialDark() { return createNomadDark(); }
NUIThemeProperties NUIThemePresets::createFluentLight() { return createNomadLight(); }
NUIThemeProperties NUIThemePresets::createFluentDark() { return createNomadDark(); }
NUIThemeProperties NUIThemePresets::createCupertinoLight() { return createNomadLight(); }
NUIThemeProperties NUIThemePresets::createCupertinoDark() { return createNomadDark(); }
NUIThemeProperties NUIThemePresets::createHighContrastLight() { return createNomadLight(); }
NUIThemeProperties NUIThemePresets::createHighContrastDark() { return createNomadDark(); }

} // namespace NomadUI
