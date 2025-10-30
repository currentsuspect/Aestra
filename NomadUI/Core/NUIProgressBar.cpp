// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUIProgressBar.h"
#include "../Graphics/NUIRenderer.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace NomadUI {

NUIProgressBar::NUIProgressBar()
    : NUIComponent()
{
    setSize(200, 20); // Default size
}

void NUIProgressBar::onRender(NUIRenderer& renderer)
{
    if (!isVisible()) return;

    // Draw the appropriate progress bar style
    switch (style_)
    {
        case Style::Linear:
            drawLinearProgress(renderer);
            break;
        case Style::Circular:
            drawCircularProgress(renderer);
            break;
        case Style::Indeterminate:
            drawIndeterminateProgress(renderer);
            break;
    }

    // Draw text if visible
    if (textVisible_)
    {
        drawText(renderer);
    }
}

void NUIProgressBar::onUpdate(double deltaTime)
{
    if (!isVisible()) return;

    updateAnimation(deltaTime);
}

void NUIProgressBar::setProgress(double progress)
{
    double newProgress = std::clamp(progress, minValue_, maxValue_);
    if (std::abs(newProgress - progress_) > 1e-9)
    {
        progress_ = newProgress;
        targetProgress_ = newProgress;
        
        if (smoothProgress_)
        {
            isAnimating_ = true;
        }
        else
        {
            currentProgress_ = progress_;
        }
        
        triggerProgressChange();
        setDirty(true);
    }
}

void NUIProgressBar::setMinValue(double minValue)
{
    minValue_ = minValue;
    setProgress(progress_); // Clamp current progress to new range
}

void NUIProgressBar::setMaxValue(double maxValue)
{
    maxValue_ = maxValue;
    setProgress(progress_); // Clamp current progress to new range
}

void NUIProgressBar::setIndeterminate(bool indeterminate)
{
    indeterminate_ = indeterminate;
    setDirty(true);
}

void NUIProgressBar::setAnimated(bool animated)
{
    animated_ = animated;
}

void NUIProgressBar::setAnimationSpeed(double speed)
{
    animationSpeed_ = speed;
}

void NUIProgressBar::setStyle(Style style)
{
    style_ = style;
    setDirty(true);
}

void NUIProgressBar::setOrientation(Orientation orientation)
{
    orientation_ = orientation;
    setDirty(true);
}

void NUIProgressBar::setBackgroundColor(const NUIColor& color)
{
    backgroundColor_ = color;
    setDirty(true);
}

void NUIProgressBar::setProgressColor(const NUIColor& color)
{
    progressColor_ = color;
    setDirty(true);
}

void NUIProgressBar::setBorderColor(const NUIColor& color)
{
    borderColor_ = color;
    setDirty(true);
}

void NUIProgressBar::setTextColor(const NUIColor& color)
{
    textColor_ = color;
    setDirty(true);
}

void NUIProgressBar::setBorderWidth(float width)
{
    borderWidth_ = width;
    setDirty(true);
}

void NUIProgressBar::setBorderRadius(float radius)
{
    borderRadius_ = radius;
    setDirty(true);
}

void NUIProgressBar::setThickness(float thickness)
{
    thickness_ = thickness;
    setDirty(true);
}

void NUIProgressBar::setTextVisible(bool visible)
{
    textVisible_ = visible;
    setDirty(true);
}

void NUIProgressBar::setTextFormat(const std::string& format)
{
    textFormat_ = format;
    setDirty(true);
}

void NUIProgressBar::setCustomText(const std::string& text)
{
    customText_ = text;
    setDirty(true);
}

void NUIProgressBar::setEasing(NUIEasing easing)
{
    easing_ = easing;
}

void NUIProgressBar::setSmoothProgress(bool smooth)
{
    smoothProgress_ = smooth;
}

void NUIProgressBar::setSmoothSpeed(double speed)
{
    smoothSpeed_ = speed;
}

void NUIProgressBar::setOnProgressChange(std::function<void(double)> callback)
{
    onProgressChangeCallback_ = callback;
}

void NUIProgressBar::setOnComplete(std::function<void()> callback)
{
    onCompleteCallback_ = callback;
}

void NUIProgressBar::reset()
{
    setProgress(minValue_);
    indeterminatePhase_ = 0.0;
    isAnimating_ = false;
}

void NUIProgressBar::setProgressAnimated(double progress, double duration)
{
    targetProgress_ = std::clamp(progress, minValue_, maxValue_);
    animationDuration_ = duration;
    animationTime_ = 0.0;
    isAnimating_ = true;
    setDirty(true);
}

std::string NUIProgressBar::getDisplayText() const
{
    if (!customText_.empty())
    {
        return customText_;
    }
    
    if (indeterminate_)
    {
        return "...";
    }
    
    double percentage = ((progress_ - minValue_) / (maxValue_ - minValue_)) * 100.0;
    
    // Simple format replacement for {0} placeholder
    std::string result = textFormat_;
    size_t pos = result.find("{0}");
    if (pos != std::string::npos)
    {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << percentage;
        result.replace(pos, 3, oss.str());
    }
    
    return result;
}

void NUIProgressBar::drawLinearProgress(NUIRenderer& renderer)
{
    NUIRect bounds = getBounds();
    
    // Draw background
    renderer.fillRoundedRect(bounds, borderRadius_, backgroundColor_);
    
    // Draw border
    renderer.strokeRoundedRect(bounds, borderRadius_, borderWidth_, borderColor_);
    
    // Draw progress
    if (currentProgress_ > minValue_)
    {
        double progressRatio = (currentProgress_ - minValue_) / (maxValue_ - minValue_);
        progressRatio = std::clamp(progressRatio, 0.0, 1.0);
        
        NUIRect progressRect;
        if (orientation_ == Orientation::Horizontal)
        {
            float progressWidth = bounds.width * static_cast<float>(progressRatio);
            progressRect = NUIRect(bounds.x, bounds.y, progressWidth, bounds.height);
        }
        else
        {
            float progressHeight = bounds.height * static_cast<float>(progressRatio);
            progressRect = NUIRect(bounds.x, bounds.y + bounds.height - progressHeight, 
                                 bounds.width, progressHeight);
        }
        
        renderer.fillRoundedRect(progressRect, borderRadius_, progressColor_);
    }
}

void NUIProgressBar::drawCircularProgress(NUIRenderer& renderer)
{
    NUIRect bounds = getBounds();
    NUIPoint center = bounds.center();
    float radius = std::min(bounds.width, bounds.height) * 0.5f - thickness_ * 0.5f;
    
    // Draw background circle
    renderer.strokeCircle(center, radius, thickness_, backgroundColor_);
    
    // Draw progress arc
    if (currentProgress_ > minValue_)
    {
        double progressRatio = (currentProgress_ - minValue_) / (maxValue_ - minValue_);
        progressRatio = std::clamp(progressRatio, 0.0, 1.0);
        
        float startAngle = -90.0f; // Start from top
        float endAngle = startAngle + static_cast<float>(progressRatio * 360.0);
        
        // TODO: Implement arc drawing when available in renderer
        // renderer.drawArc(center, radius, startAngle, endAngle, thickness_, progressColor_);
    }
}

void NUIProgressBar::drawIndeterminateProgress(NUIRenderer& renderer)
{
    NUIRect bounds = getBounds();
    
    // Draw background
    renderer.fillRoundedRect(bounds, borderRadius_, backgroundColor_);
    
    // Draw border
    renderer.strokeRoundedRect(bounds, borderRadius_, borderWidth_, borderColor_);
    
    // Draw animated progress bar
    float barWidth = bounds.width * 0.3f; // Width of the moving bar
    float barX = bounds.x + (bounds.width - barWidth) * static_cast<float>(indeterminatePhase_);
    
    NUIRect barRect(barX, bounds.y, barWidth, bounds.height);
    renderer.fillRoundedRect(barRect, borderRadius_, progressColor_);
}

void NUIProgressBar::drawText(NUIRenderer& renderer)
{
    if (!textVisible_) return;
    
    std::string text = getDisplayText();
    if (text.empty()) return;
    
    NUIRect bounds = getBounds();
    NUIPoint textPos = bounds.center();
    
    // TODO: Implement text rendering when NUIFont is available
    // renderer.drawText(text, textPos, NUITextAlignment::Center, textColor_);
}

double NUIProgressBar::applyEasing(double t) const
{
    switch (easing_)
    {
        case NUIEasing::Linear:
            return t;
        case NUIEasing::EaseIn:
            return t * t;
        case NUIEasing::EaseOut:
            return 1.0 - (1.0 - t) * (1.0 - t);
        case NUIEasing::EaseInOut:
            return t < 0.5 ? 2.0 * t * t : 1.0 - 2.0 * (1.0 - t) * (1.0 - t);
        case NUIEasing::BounceIn:
            return 1.0 - std::cos(t * 3.14159 * 0.5);
        case NUIEasing::BounceOut:
            return std::sin(t * 3.14159 * 0.5);
        case NUIEasing::ElasticIn:
            return t == 0 ? 0 : t == 1 ? 1 : -std::pow(2, 10 * t - 10) * std::sin((t * 10 - 10.75) * 2.09439);
        case NUIEasing::ElasticOut:
            return t == 0 ? 0 : t == 1 ? 1 : std::pow(2, -10 * t) * std::sin((t * 10 - 0.75) * 2.09439) + 1;
        case NUIEasing::BackIn:
            return 2.7 * t * t * t - 1.7 * t * t;
        case NUIEasing::BackOut:
            return 1 + 2.7 * std::pow(t - 1, 3) + 1.7 * std::pow(t - 1, 2);
        default:
            return t;
    }
}

void NUIProgressBar::updateAnimation(double deltaTime)
{
    if (indeterminate_)
    {
        updateIndeterminateAnimation(deltaTime);
    }
    else if (isAnimating_)
    {
        if (smoothProgress_)
        {
            // Smooth progress animation
            double diff = targetProgress_ - currentProgress_;
            if (std::abs(diff) > 1e-6)
            {
                currentProgress_ += diff * smoothSpeed_ * deltaTime;
                setDirty(true);
            }
            else
            {
                currentProgress_ = targetProgress_;
                isAnimating_ = false;
            }
        }
        else if (animationDuration_ > 0)
        {
            // Timed animation
            animationTime_ += deltaTime;
            double t = animationTime_ / animationDuration_;
            
            if (t >= 1.0)
            {
                t = 1.0;
                isAnimating_ = false;
                triggerComplete();
            }
            
            double easedT = applyEasing(t);
            currentProgress_ = progress_ + (targetProgress_ - progress_) * easedT;
            setDirty(true);
        }
    }
}

void NUIProgressBar::updateProgress()
{
    // This method can be overridden for custom progress calculation
    setDirty(true);
}

void NUIProgressBar::triggerProgressChange()
{
    if (onProgressChangeCallback_)
    {
        onProgressChangeCallback_(currentProgress_);
    }
}

void NUIProgressBar::triggerComplete()
{
    if (onCompleteCallback_)
    {
        onCompleteCallback_();
    }
}

void NUIProgressBar::updateIndeterminateAnimation(double deltaTime)
{
    indeterminatePhase_ += deltaTime * animationSpeed_;
    if (indeterminatePhase_ > 1.0)
    {
        indeterminatePhase_ = 0.0;
    }
    setDirty(true);
}

} // namespace NomadUI
