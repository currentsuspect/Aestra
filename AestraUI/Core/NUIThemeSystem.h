// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUITypes.h"
#include "NUIAnimation.h"
#include <unordered_map>
#include <string>
#include <algorithm>
#include <memory>
#include <cstdint>
#include <functional>
#include <map>

namespace AestraUI {

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
    NUIColor textMuted;              // Low-emphasis metadata and placeholders
    NUIColor textDisabled;           // Inactive states (#5A5A5D)
    NUIColor textLink;               // Links/actions (#8B7FFF)
    NUIColor textCritical;           // Errors (#FF5E5E)
    NUIColor textOnPrimary;
    NUIColor textOnSecondary;
    
    // Borders & Highlights
    NUIColor borderSubtle;           // Divider lines (#2c2c2f)
    NUIColor borderStrong;           // Structural/control edge with increased contrast
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
    NUIColor focusRing;

    // Domain states remain separate from generic interaction states so
    // selection does not compete with transport and mixer status.
    NUIColor armed;
    NUIColor muted;
    NUIColor soloed;
    NUIColor bypassed;
    NUIColor dragTarget;
    
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
    
    // Meter Colors
    NUIColor meterSafe;
    NUIColor meterWarn;
    NUIColor meterCrit;
    NUIColor meterBackground;
    NUIColor meterActive;

    NUIColor gridMajor;
    NUIColor gridMinor;
    
    // Glass Aesthetic tokens
    NUIColor glassHover;             // Neutral clear glass highlighting
    NUIColor glassBorder;            // Subtle white border for glass
    NUIColor glassActive;            // Colored luminous glass (usually derived from accent)

    // Mixer tokens
    NUIColor mixerStripBg;
    NUIColor mixerMasterBorder;
    
    // Spacing
    float spacingXS = 4.0f;
    float spacingS = 8.0f;
    float spacingM = 16.0f;
    float spacingL = 24.0f;
    float spacingXL = 32.0f;
    float spacingXXL = 48.0f;
    
    // Border radius — "dense instrument" scale, recentered onto the 5-7px
    // mass the UI actually uses (see AestraDocs/ui-type-space-grammar.md).
    float radiusXS = 3.0f;
    float radiusS = 5.0f;
    float radiusM = 7.0f;
    float radiusL = 10.0f;
    float radiusXL = 14.0f;
    float radiusXXL = 20.0f;

    // Typography — "dense instrument" scale. A DAW lives at 9-14px, not the
    // generic 12-18; steps are perceptually distinct (no 1px neighbours) and
    // absorb the historical fractional drift. See the grammar doc.
    float fontSizeMicro = 9.0f;   // meter ticks, tiny inline values
    float fontSizeXS = 10.0f;     // dense/secondary labels — the workhorse
    float fontSizeS = 11.0f;      // control labels, rows
    float fontSizeM = 12.0f;      // primary body, list rows
    float fontSizeL = 14.0f;      // section titles
    float fontSizeXL = 16.0f;     // panel headers
    float fontSizeDisplayS = 24.0f; // secondary numeric readouts
    float fontSizeDisplayL = 32.0f; // primary numeric readouts (BPM/time)
    // Legacy display tokens retained until their surfaces migrate.
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
        float trackControlsWidth = 236.0f;
        float timelineAreaWidth = 800.0f;

        // Track heights and spacing
        float trackHeight = 46.0f;
        float trackSpacing = 4.0f;
        float trackLabelHeight = 20.0f;

        // Transport bar dimensions
        float transportBarHeight = 56.0f;
        float transportButtonSize = 28.0f;
        float transportButtonSpacing = 8.0f;

        // Application chrome dimensions
        float titleBarHeight = 32.0f;
        float viewToggleWidth = 310.0f;
        float viewToggleHeight = 24.0f;

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

        // Shared compact-desktop control metrics.
        float compactControlHeight = 24.0f;
        float standardControlHeight = 28.0f;
        float dialogActionHeight = 36.0f;
        float standardRowHeight = 28.0f;
        float compactMenuRowHeight = 24.0f;
        float standardMenuRowHeight = 28.0f;
        float panelHeaderHeight = 32.0f;
        float sectionHeaderHeight = 24.0f;
        float standardIconSize = 16.0f;
        float minimumHitArea = 24.0f;
        float dividerWidth = 1.0f;
        float panelPadding = 8.0f;
        float dialogPadding = 16.0f;

        // Window dimensions
        float minWindowWidth = 800.0f;
        float minWindowHeight = 600.0f;
        float defaultWindowWidth = 1200.0f;
        float defaultWindowHeight = 800.0f;
    };

    LayoutDimensions layout;
};

struct NUIControlVisualState {
    bool enabled = true;
    bool hovered = false;
    bool pressed = false;
    bool selected = false;
    bool focused = false;
};

struct NUIResolvedControlColors {
    NUIColor background;
    NUIColor border;
    NUIColor text;
    float borderWidth = 1.0f;
};

// Priority: disabled > pressed > selected > hovered > idle. Focus changes
// border treatment without changing control geometry.
NUIResolvedControlColors resolveControlColors(const NUIThemeProperties& theme,
                                              const NUIControlVisualState& state);

// Theme manager
class NUIThemeManager {
public:
    using ThemeSubscriptionId = std::uint64_t;

    static NUIThemeManager& getInstance();
    
    // Theme management
    void setThemeVariant(NUIThemeVariant variant);
    NUIThemeVariant getThemeVariant() const { return currentVariant_; }
    
    void setCustomTheme(const std::string& name, const NUIThemeProperties& properties);
    bool loadThemeFromFile(const std::string& name, const std::string& filepath,
                           const std::string& baseTheme = "Aestra-dark");
    bool setActiveTheme(const std::string& name);
    bool hasTheme(const std::string& name) const;
    std::string getActiveTheme() const { return activeTheme_; }
    
    // Theme access
    const NUIThemeProperties& getCurrentTheme() const;
    NUIThemeProperties& getCurrentThemeMutable();
    
    // Theme switching with animation
    void switchTheme(const std::string& name, float durationMs = 300.0f);
    void switchThemeVariant(NUIThemeVariant variant, float durationMs = 300.0f);
    
    // Theme callbacks
    ThemeSubscriptionId subscribeToThemeChanges(std::function<void(const NUIThemeProperties&)> callback);
    void unsubscribeFromThemeChanges(ThemeSubscriptionId subscriptionId);
    // Compatibility callback slot. New owners should use subscriptions so they
    // do not replace one another.
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
    void notifyThemeChanged();
    
    NUIThemeVariant currentVariant_;
    std::string activeTheme_;
    std::unordered_map<std::string, NUIThemeProperties> themes_;
    std::function<void(const NUIThemeProperties&)> onThemeChanged_;
    std::map<ThemeSubscriptionId, std::function<void(const NUIThemeProperties&)>> themeSubscribers_;
    ThemeSubscriptionId nextSubscriptionId_ = 1;
    
    // Animation for theme switching
    std::shared_ptr<NUIAnimation> themeTransitionAnimation_;
    NUIThemeProperties transitionFromTheme_;
    NUIThemeProperties transitionToTheme_;
    bool isTransitioning_;
};

// Theme-aware component base
class NUIThemedComponent {
public:
    virtual ~NUIThemedComponent();
    
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
    NUIThemeManager::ThemeSubscriptionId themeSubscriptionId_ = 0;
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
    static NUIThemeProperties createAestraLight();
    static NUIThemeProperties createAestraDark();
    static NUIThemeProperties createHighContrastLight();
    static NUIThemeProperties createHighContrastDark();
};

// ---------------------------------------------------------------------------
// Plugin-editor chrome helpers (polarity-aware neutrals)
// ---------------------------------------------------------------------------

/** True when the active theme is light (lights-on). */
inline bool editorLightUi() {
    const auto& bg = NUIThemeManager::getInstance().getCurrentTheme().backgroundPrimary;
    return (0.2126f * bg.r + 0.7152f * bg.g + 0.0722f * bg.b) >= 0.5f;
}

/**
 * Map a plugin editor's dark neutral gray onto the active theme's polarity.
 * Dark themes return the given gray untouched (editors keep their tuned
 * near-blacks bit-for-bit); light themes mirror it onto the light-surface
 * ramp, preserving the raised/recessed ordering (lighter = more raised).
 */
inline NUIColor editorNeutral(float darkGray, float alpha = 1.0f) {
    if (!editorLightUi()) return NUIColor(darkGray, darkGray, darkGray, alpha);
    const float g = std::min(0.905f + darkGray * 0.38f, 0.985f);
    return NUIColor(g, g, g, alpha);
}

/** Tinted variant: dark themes keep the tuned color; light themes map its
    luma onto the light ramp (the subtle tint reads as gray up there anyway). */
inline NUIColor editorNeutral(const NUIColor& dark) {
    if (!editorLightUi()) return dark;
    const float luma = 0.2126f * dark.r + 0.7152f * dark.g + 0.0722f * dark.b;
    const float g = std::min(0.905f + luma * 0.38f, 0.985f);
    return NUIColor(g, g, g, dark.a);
}

/** The active theme's text ink — replaces hardcoded white overlays/labels. */
inline NUIColor editorInk(float alpha = 1.0f) {
    return NUIThemeManager::getInstance().getCurrentTheme().textPrimary.withAlpha(alpha);
}

} // namespace AestraUI
