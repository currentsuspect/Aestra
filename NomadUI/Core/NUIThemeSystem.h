// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUITypes.h"
#include "NUIAnimation.h"
#include <unordered_map>
#include <string>
#include <memory>

namespace NomadUI {

// Theme variants
enum class NUIThemeVariant {
    Light,
    Dark,
    Auto  // Follows system preference
};

// Theme properties
struct NUIThemeProperties {
    // Core Structure - Layered backgrounds
    NUIColor backgroundPrimary;      // Primary canvas (#181819)
    NUIColor backgroundSecondary;    // Panels, sidebars (#1e1e1f)
    NUIColor surfaceTertiary;        // Dialogs, popups (#242428)
    NUIColor surfaceRaised;          // Cards, highlighted containers (#2c2c31)
    
    // Legacy compatibility
    NUIColor background;
    NUIColor surface;
    NUIColor surfaceVariant;
    
    // Accent & Branding
    NUIColor primary;                // Core accent (#8B7FFF)
    NUIColor primaryHover;           // Hover variant (#A79EFF)
    NUIColor primaryPressed;         // Pressed state (#665AD9)
    NUIColor primaryVariant;
    
    NUIColor secondary;
    NUIColor secondaryVariant;
    
    // Functional Colors (Status)
    NUIColor success;                // #5BD896
    NUIColor warning;                // #FFD86B
    NUIColor error;                  // #FF5E5E
    NUIColor info;                   // #6BCBFF
    
    // Liminal Dark v2.0 Accent Colors
    NUIColor accentCyan;             // #00bcd4 - Playful but professional blue
    NUIColor accentMagenta;          // #ff4081 - Passion, energy - stereo right
    NUIColor accentLime;             // #9eff61 - Active/Connected indicators
    NUIColor accentPrimary;          // #00bcd4 - Primary accent (cyan)
    NUIColor accentSecondary;        // #ff4081 - Secondary accent (magenta)
    
    NUIColor onBackground;
    NUIColor onSurface;
    NUIColor onPrimary;
    NUIColor onSecondary;
    NUIColor onError;
    NUIColor onWarning;
    NUIColor onSuccess;
    NUIColor onInfo;
    
    // Text & Typography
    NUIColor textPrimary;            // Main text (#E5E5E8)
    NUIColor textSecondary;          // Subtext, labels (#A6A6AA)
    NUIColor textDisabled;           // Inactive states (#5A5A5D)
    NUIColor textLink;               // Links/actions (#8B7FFF)
    NUIColor textCritical;           // Errors (#FF5E5E)
    NUIColor textOnPrimary;
    NUIColor textOnSecondary;
    
    // Borders & Highlights
    NUIColor borderSubtle;           // Divider lines (#2c2c2f)
    NUIColor borderActive;           // Selected/focused (#8B7FFF)
    NUIColor border;
    NUIColor divider;
    NUIColor outline;
    NUIColor outlineVariant;
    
    // Interactive States
    NUIColor hover;
    NUIColor pressed;
    NUIColor focused;
    NUIColor selected;
    NUIColor disabled;
    
    // Interactive Element Defaults
    NUIColor buttonBgDefault;        // #242428
    NUIColor buttonBgHover;          // #2e2e33
    NUIColor buttonBgActive;         // #8B7FFF
    NUIColor buttonTextDefault;      // #E5E5E8
    NUIColor buttonTextActive;       // #ffffff
    
    NUIColor toggleDefault;          // #3a3a3f
    NUIColor toggleHover;            // #4a4a50
    NUIColor toggleActive;           // #8B7FFF
    
    NUIColor inputBgDefault;         // #1b1b1c
    NUIColor inputBgHover;           // #1f1f20
    NUIColor inputBorderFocus;       // #8B7FFF
    
    NUIColor sliderTrack;            // #2a2a2e
    NUIColor sliderHandle;           // #8B7FFF
    NUIColor sliderHandleHover;      // #A79EFF
    NUIColor sliderHandlePressed;    // #665AD9
    
    // Shadows and overlays
    NUIColor shadow;
    NUIColor overlay;
    NUIColor backdrop;
    NUIColor highlightGlow;          // rgba(139, 127, 255, 0.3)
    
    // Spacing
    float spacingXS = 4.0f;
    float spacingS = 8.0f;
    float spacingM = 16.0f;
    float spacingL = 24.0f;
    float spacingXL = 32.0f;
    float spacingXXL = 48.0f;
    
    // Border radius
    float radiusXS = 2.0f;
    float radiusS = 4.0f;
    float radiusM = 8.0f;
    float radiusL = 12.0f;
    float radiusXL = 16.0f;
    float radiusXXL = 24.0f;
    
    // Typography - crisp UI text sizes (bumped +2px)
    float fontSizeXS = 12.0f;
    float fontSizeS = 13.0f;
    float fontSizeM = 14.0f;
    float fontSizeL = 16.0f;
    float fontSizeXL = 18.0f;
    float fontSizeXXL = 22.0f;
    float fontSizeH1 = 26.0f;
    float fontSizeH2 = 22.0f;
    float fontSizeH3 = 20.0f;
    
    // Line heights
    float lineHeightTight = 1.2f;
    float lineHeightNormal = 1.4f;
    float lineHeightRelaxed = 1.6f;
    
    // Shadows
    struct Shadow {
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        float blurRadius = 0.0f;
        float spreadRadius = 0.0f;
        NUIColor color = NUIColor::black();
        float opacity = 0.0f;
        
        Shadow() = default;
        Shadow(float x, float y, float blur, float spread, const NUIColor& c, float op = 1.0f)
            : offsetX(x), offsetY(y), blurRadius(blur), spreadRadius(spread), color(c), opacity(op) {}
    };
    
    Shadow shadowXS;
    Shadow shadowS;
    Shadow shadowM;
    Shadow shadowL;
    Shadow shadowXL;
    
    // Animation durations
    float durationFast = 150.0f;
    float durationNormal = 250.0f;
    float durationSlow = 350.0f;
    
    // Animation easings
    NUIEasingType easingStandard = NUIEasingType::EaseOutCubic;
    NUIEasingType easingDecelerate = NUIEasingType::EaseOutCubic;
    NUIEasingType easingAccelerate = NUIEasingType::EaseInCubic;
    NUIEasingType easingSharp = NUIEasingType::EaseInOutCubic;
    
    // Z-index layers
    int zIndexBackground = 0;
    int zIndexSurface = 100;
    int zIndexDropdown = 200;
    int zIndexModal = 300;
    int zIndexTooltip = 400;
    int zIndexNotification = 500;

    // Layout Dimensions - Configurable UI Sizing
    struct LayoutDimensions {
        // Panel widths
        float fileBrowserWidth = 300.0f;
        float trackControlsWidth = 200.0f;
        float timelineAreaWidth = 800.0f;

        // Track heights and spacing
        float trackHeight = 48.0f;
        float trackSpacing = 4.0f;
        float trackLabelHeight = 20.0f;

        // Transport bar dimensions
        float transportBarHeight = 56.0f;
        float transportButtonSize = 36.0f;
        float transportButtonSpacing = 8.0f;

        // Control button dimensions
        float controlButtonWidth = 32.0f;
        float controlButtonHeight = 20.0f;
        float controlButtonSpacing = 4.0f;
        float controlButtonStartX = 100.0f; // X position where control buttons start

        // Grid and timeline
        float gridLineSpacing = 50.0f;
        float timelineHeight = 40.0f;

        // Margins and padding
        float panelMargin = 10.0f;
        float componentPadding = 8.0f;
        float buttonPadding = 4.0f;

        // Window dimensions
        float minWindowWidth = 800.0f;
        float minWindowHeight = 600.0f;
        float defaultWindowWidth = 1200.0f;
        float defaultWindowHeight = 800.0f;
    };

    LayoutDimensions layout;
};

// Theme manager
class NUIThemeManager {
public:
    static NUIThemeManager& getInstance();
    
    // Theme management
    void setThemeVariant(NUIThemeVariant variant);
    NUIThemeVariant getThemeVariant() const { return currentVariant_; }
    
    void setCustomTheme(const std::string& name, const NUIThemeProperties& properties);
    void setActiveTheme(const std::string& name);
    std::string getActiveTheme() const { return activeTheme_; }
    
    // Theme access
    const NUIThemeProperties& getCurrentTheme() const;
    NUIThemeProperties& getCurrentThemeMutable();
    
    // Theme switching with animation
    void switchTheme(const std::string& name, float durationMs = 300.0f);
    void switchThemeVariant(NUIThemeVariant variant, float durationMs = 300.0f);
    
    // Theme callbacks
    void setOnThemeChanged(std::function<void(const NUIThemeProperties&)> callback);
    
    // Utility methods
    NUIColor getColor(const std::string& colorName) const;
    float getSpacing(const std::string& spacingName) const;
    float getRadius(const std::string& radiusName) const;
    float getFontSize(const std::string& fontSizeName) const;
    NUIThemeProperties::Shadow getShadow(const std::string& shadowName) const;

    // Layout dimension access
    float getLayoutDimension(const std::string& dimensionName) const;
    const NUIThemeProperties::LayoutDimensions& getLayoutDimensions() const;

    // Component-specific dimension access
    float getComponentDimension(const std::string& componentName, const std::string& dimensionName) const;
    
    // Color utilities
    NUIColor getContrastColor(const NUIColor& backgroundColor) const;
    NUIColor getHoverColor(const NUIColor& baseColor) const;
    NUIColor getPressedColor(const NUIColor& baseColor) const;
    NUIColor getDisabledColor(const NUIColor& baseColor) const;
    
    // Animation utilities
    std::shared_ptr<NUIAnimation> createColorTransition(
        const NUIColor& from, const NUIColor& to, float durationMs = -1.0f) const;
    
private:
    NUIThemeManager();
    void initializeDefaultThemes();
    void updateSystemTheme();
    
    NUIThemeVariant currentVariant_;
    std::string activeTheme_;
    std::unordered_map<std::string, NUIThemeProperties> themes_;
    std::function<void(const NUIThemeProperties&)> onThemeChanged_;
    
    // Animation for theme switching
    std::shared_ptr<NUIAnimation> themeTransitionAnimation_;
    NUIThemeProperties transitionFromTheme_;
    NUIThemeProperties transitionToTheme_;
    bool isTransitioning_;
};

// Theme-aware component base
class NUIThemedComponent {
public:
    virtual ~NUIThemedComponent() = default;
    
    // Theme integration
    virtual void onThemeChanged(const NUIThemeProperties& theme) {}
    virtual void applyTheme(const NUIThemeProperties& theme) {}
    
    // Color helpers
    NUIColor getThemeColor(const std::string& colorName) const;
    float getThemeSpacing(const std::string& spacingName) const;
    float getThemeRadius(const std::string& radiusName) const;
    float getThemeFontSize(const std::string& fontSizeName) const;
    float getThemeLayoutDimension(const std::string& dimensionName) const;
    float getThemeComponentDimension(const std::string& componentName, const std::string& dimensionName) const;
    
protected:
    void registerForThemeUpdates();
    void unregisterFromThemeUpdates();
    
private:
    bool isThemeRegistered_ = false;
};

// Predefined theme presets
class NUIThemePresets {
public:
    static NUIThemeProperties createMaterialLight();
    static NUIThemeProperties createMaterialDark();
    static NUIThemeProperties createFluentLight();
    static NUIThemeProperties createFluentDark();
    static NUIThemeProperties createCupertinoLight();
    static NUIThemeProperties createCupertinoDark();
    static NUIThemeProperties createNomadLight();
    static NUIThemeProperties createNomadDark();
    static NUIThemeProperties createHighContrastLight();
    static NUIThemeProperties createHighContrastDark();
};

} // namespace NomadUI
