// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"
#include <functional>
#include <string>

namespace NomadUI {

/**
 * NUIProgressBar - A progress indicator component
 * Supports determinate and indeterminate progress with various styles
 * Replaces juce::ProgressBar with NomadUI styling and theming
 */
class NUIProgressBar : public NUIComponent
{
public:
    // Progress bar styles
    enum class Style
    {
        Linear,         // Horizontal linear progress bar
        Circular,       // Circular progress indicator
        Indeterminate   // Animated indeterminate progress
    };

    // Progress bar orientations
    enum class Orientation
    {
        Horizontal,
        Vertical
    };

    NUIProgressBar();
    ~NUIProgressBar() override = default;

    // Component interface
    void onRender(NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;

    // Progress properties
    void setProgress(double progress);
    double getProgress() const { return progress_; }

    void setMinValue(double minValue);
    double getMinValue() const { return minValue_; }

    void setMaxValue(double maxValue);
    double getMaxValue() const { return maxValue_; }

    void setIndeterminate(bool indeterminate);
    bool isIndeterminate() const { return indeterminate_; }

    void setAnimated(bool animated);
    bool isAnimated() const { return animated_; }

    void setAnimationSpeed(double speed);
    double getAnimationSpeed() const { return animationSpeed_; }

    // Visual properties
    void setStyle(Style style);
    Style getStyle() const { return style_; }

    void setOrientation(Orientation orientation);
    Orientation getOrientation() const { return orientation_; }

    void setBackgroundColor(const NUIColor& color);
    NUIColor getBackgroundColor() const { return backgroundColor_; }

    void setProgressColor(const NUIColor& color);
    NUIColor getProgressColor() const { return progressColor_; }

    void setBorderColor(const NUIColor& color);
    NUIColor getBorderColor() const { return borderColor_; }

    void setTextColor(const NUIColor& color);
    NUIColor getTextColor() const { return textColor_; }

    void setBorderWidth(float width);
    float getBorderWidth() const { return borderWidth_; }

    void setBorderRadius(float radius);
    float getBorderRadius() const { return borderRadius_; }

    void setThickness(float thickness);
    float getThickness() const { return thickness_; }

    // Text display
    void setTextVisible(bool visible);
    bool isTextVisible() const { return textVisible_; }

    void setTextFormat(const std::string& format);
    const std::string& getTextFormat() const { return textFormat_; }

    void setCustomText(const std::string& text);
    const std::string& getCustomText() const { return customText_; }

    // Animation properties
    void setEasing(NUIEasing easing);
    NUIEasing getEasing() const { return easing_; }

    void setSmoothProgress(bool smooth);
    bool isSmoothProgress() const { return smoothProgress_; }

    void setSmoothSpeed(double speed);
    double getSmoothSpeed() const { return smoothSpeed_; }

    // Event callbacks
    void setOnProgressChange(std::function<void(double)> callback);
    void setOnComplete(std::function<void()> callback);

    // Utility
    void reset();
    void setProgressAnimated(double progress, double duration = 0.5);
    std::string getDisplayText() const;

protected:
    // Override these for custom progress bar styles
    virtual void drawLinearProgress(NUIRenderer& renderer);
    virtual void drawCircularProgress(NUIRenderer& renderer);
    virtual void drawIndeterminateProgress(NUIRenderer& renderer);
    virtual void drawText(NUIRenderer& renderer);

    // Animation helpers
    virtual double applyEasing(double t) const;
    virtual void updateAnimation(double deltaTime);

private:
    void updateProgress();
    void triggerProgressChange();
    void triggerComplete();
    void updateIndeterminateAnimation(double deltaTime);

    // Progress values
    double progress_ = 0.0;
    double minValue_ = 0.0;
    double maxValue_ = 1.0;
    double targetProgress_ = 0.0;
    double currentProgress_ = 0.0;

    // Animation state
    bool indeterminate_ = false;
    bool animated_ = true;
    double animationSpeed_ = 1.0;
    double indeterminatePhase_ = 0.0;
    double smoothProgress_ = true;
    double smoothSpeed_ = 5.0;
    NUIEasing easing_ = NUIEasing::EaseOut;

    // Visual properties
    Style style_ = Style::Linear;
    Orientation orientation_ = Orientation::Horizontal;
    NUIColor backgroundColor_ = NUIColor::fromHex(0xff2a2d32);
    NUIColor progressColor_ = NUIColor::fromHex(0xffa855f7);
    NUIColor borderColor_ = NUIColor::fromHex(0xff666666);
    NUIColor textColor_ = NUIColor::fromHex(0xffffffff);
    float borderWidth_ = 1.0f;
    float borderRadius_ = 4.0f;
    float thickness_ = 8.0f;

    // Text properties
    bool textVisible_ = true;
    std::string textFormat_ = "{0}%";
    std::string customText_;

    // Animation timing
    double animationTime_ = 0.0;
    double animationDuration_ = 0.0;
    bool isAnimating_ = false;

    // Callbacks
    std::function<void(double)> onProgressChangeCallback_;
    std::function<void()> onCompleteCallback_;
};

} // namespace NomadUI
