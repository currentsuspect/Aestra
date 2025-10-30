// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUIAnimation.h"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace NomadUI {

// NUIAnimation Implementation
NUIAnimation::NUIAnimation()
    : duration_(300.0f)
    , delay_(0.0f)
    , progress_(0.0f)
    , startValue_(0.0f)
    , endValue_(1.0f)
    , currentValue_(0.0f)
    , easing_(NUIEasingType::EaseOutCubic)
    , direction_(NUIAnimationDirection::Forward)
    , state_(NUIAnimationState::Stopped)
    , loop_(false)
    , reverse_(false)
    , pausedDuration_(0.0f)
{
}

void NUIAnimation::setDuration(float durationMs) {
    duration_ = std::max(0.0f, durationMs);
}

void NUIAnimation::setEasing(NUIEasingType easing) {
    easing_ = easing;
}

void NUIAnimation::setDirection(NUIAnimationDirection direction) {
    direction_ = direction;
}

void NUIAnimation::setLoop(bool loop) {
    loop_ = loop;
}

void NUIAnimation::setDelay(float delayMs) {
    delay_ = std::max(0.0f, delayMs);
}

void NUIAnimation::setStartValue(float value) {
    startValue_ = value;
    if (state_ == NUIAnimationState::Stopped) {
        currentValue_ = value;
    }
}

void NUIAnimation::setEndValue(float value) {
    endValue_ = value;
}

void NUIAnimation::setCurrentValue(float value) {
    currentValue_ = value;
}

void NUIAnimation::start() {
    if (state_ == NUIAnimationState::Running) return;
    
    state_ = NUIAnimationState::Running;
    startTime_ = std::chrono::high_resolution_clock::now();
    pausedDuration_ = 0.0f;
    progress_ = 0.0f;
    reverse_ = false;
}

void NUIAnimation::stop() {
    state_ = NUIAnimationState::Stopped;
    progress_ = 0.0f;
    currentValue_ = startValue_;
}

void NUIAnimation::pause() {
    if (state_ == NUIAnimationState::Running) {
        state_ = NUIAnimationState::Paused;
        pauseTime_ = std::chrono::high_resolution_clock::now();
    }
}

void NUIAnimation::resume() {
    if (state_ == NUIAnimationState::Paused) {
        state_ = NUIAnimationState::Running;
        auto now = std::chrono::high_resolution_clock::now();
        pausedDuration_ += std::chrono::duration<float, std::milli>(now - pauseTime_).count();
    }
}

void NUIAnimation::reset() {
    stop();
    currentValue_ = startValue_;
}

void NUIAnimation::update(float deltaTime) {
    if (state_ != NUIAnimationState::Running) return;

    auto now = std::chrono::high_resolution_clock::now();
    float elapsed = std::chrono::duration<float, std::milli>(now - startTime_).count() - pausedDuration_;

    // Handle delay
    if (elapsed < delay_) return;

    // Calculate progress
    float totalDuration = duration_ + delay_;
    progress_ = (elapsed - delay_) / duration_;
    progress_ = std::clamp(progress_, 0.0f, 1.0f);

    // Apply easing
    float easedProgress = applyEasing(progress_);

    // Handle direction
    if (direction_ == NUIAnimationDirection::Reverse) {
        easedProgress = 1.0f - easedProgress;
    } else if (direction_ == NUIAnimationDirection::Alternate) {
        if (reverse_) {
            easedProgress = 1.0f - easedProgress;
        }
    }

    // Interpolate value
    currentValue_ = startValue_ + (endValue_ - startValue_) * easedProgress;

    // Call update callback
    if (onUpdate_) {
        onUpdate_(currentValue_);
    }

    // Check for completion
    if (progress_ >= 1.0f) {
        if (direction_ == NUIAnimationDirection::Alternate && !reverse_) {
            reverse_ = true;
            progress_ = 0.0f;
            startTime_ = now;
            pausedDuration_ = 0.0f;
        } else if (loop_) {
            progress_ = 0.0f;
            startTime_ = now;
            pausedDuration_ = 0.0f;
            reverse_ = false;
        } else {
            state_ = NUIAnimationState::Completed;
            if (onComplete_) {
                onComplete_();
            }
        }
    }
}

void NUIAnimation::setOnUpdate(std::function<void(float)> callback) {
    onUpdate_ = callback;
}

void NUIAnimation::setOnComplete(std::function<void()> callback) {
    onComplete_ = callback;
}

float NUIAnimation::applyEasing(float t) {
    switch (easing_) {
        case NUIEasingType::Linear: return t;
        case NUIEasingType::EaseIn: return easeIn(t);
        case NUIEasingType::EaseOut: return easeOut(t);
        case NUIEasingType::EaseInOut: return easeInOut(t);
        case NUIEasingType::EaseOutCubic: return easeOutCubic(t);
        case NUIEasingType::EaseInCubic: return easeInCubic(t);
        case NUIEasingType::EaseInOutCubic: return easeInOutCubic(t);
        case NUIEasingType::EaseOutElastic: return easeOutElastic(t);
        case NUIEasingType::EaseInElastic: return easeInElastic(t);
        case NUIEasingType::EaseInOutElastic: return easeInOutElastic(t);
        case NUIEasingType::EaseOutBounce: return easeOutBounce(t);
        case NUIEasingType::EaseInBounce: return easeInBounce(t);
        case NUIEasingType::EaseInOutBounce: return easeInOutBounce(t);
        default: return t;
    }
}

float NUIAnimation::easeIn(float t) {
    return t * t;
}

float NUIAnimation::easeOut(float t) {
    return 1.0f - (1.0f - t) * (1.0f - t);
}

float NUIAnimation::easeInOut(float t) {
    return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
}

float NUIAnimation::easeOutCubic(float t) {
    return 1.0f - std::pow(1.0f - t, 3.0f);
}

float NUIAnimation::easeInCubic(float t) {
    return t * t * t;
}

float NUIAnimation::easeInOutCubic(float t) {
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

float NUIAnimation::easeOutElastic(float t) {
    const float c4 = (2.0f * M_PI) / 3.0f;
    return t == 0.0f ? 0.0f : t == 1.0f ? 1.0f : std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
}

float NUIAnimation::easeInElastic(float t) {
    const float c4 = (2.0f * M_PI) / 3.0f;
    return t == 0.0f ? 0.0f : t == 1.0f ? 1.0f : -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * c4);
}

float NUIAnimation::easeInOutElastic(float t) {
    const float c5 = (2.0f * M_PI) / 4.5f;
    return t == 0.0f ? 0.0f : t == 1.0f ? 1.0f : t < 0.5f
        ? -(std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) / 2.0f
        : (std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) / 2.0f + 1.0f;
}

float NUIAnimation::easeOutBounce(float t) {
    const float n1 = 7.5625f;
    const float d1 = 2.75f;
    
    if (t < 1.0f / d1) {
        return n1 * t * t;
    } else if (t < 2.0f / d1) {
        return n1 * (t -= 1.5f / d1) * t + 0.75f;
    } else if (t < 2.5f / d1) {
        return n1 * (t -= 2.25f / d1) * t + 0.9375f;
    } else {
        return n1 * (t -= 2.625f / d1) * t + 0.984375f;
    }
}

float NUIAnimation::easeInBounce(float t) {
    return 1.0f - easeOutBounce(1.0f - t);
}

float NUIAnimation::easeInOutBounce(float t) {
    return t < 0.5f ? (1.0f - easeOutBounce(1.0f - 2.0f * t)) / 2.0f : (1.0f + easeOutBounce(2.0f * t - 1.0f)) / 2.0f;
}

// NUIAnimationManager Implementation
NUIAnimationManager& NUIAnimationManager::getInstance() {
    static NUIAnimationManager instance;
    return instance;
}

void NUIAnimationManager::addAnimation(std::shared_ptr<NUIAnimation> animation) {
    if (animation) {
        animations_.push_back(animation);
    }
}

void NUIAnimationManager::removeAnimation(std::shared_ptr<NUIAnimation> animation) {
    animations_.erase(
        std::remove(animations_.begin(), animations_.end(), animation),
        animations_.end()
    );
}

void NUIAnimationManager::updateAll(float deltaTime) {
    for (auto& animation : animations_) {
        if (animation) {
            animation->update(deltaTime);
        }
    }
    
    // Remove completed animations
    animations_.erase(
        std::remove_if(animations_.begin(), animations_.end(),
            [](const std::shared_ptr<NUIAnimation>& anim) {
                return !anim || anim->isCompleted();
            }),
        animations_.end()
    );
}

void NUIAnimationManager::clearAll() {
    animations_.clear();
}

std::shared_ptr<NUIAnimation> NUIAnimationManager::createAnimation() {
    return std::make_shared<NUIAnimation>();
}

void NUIAnimationManager::stopAllAnimations() {
    for (auto& animation : animations_) {
        if (animation) {
            animation->stop();
        }
    }
}

// NUIAnimationUtils Implementation
std::shared_ptr<NUIAnimation> NUIAnimationUtils::createScaleAnimation(
    float startScale, float endScale, float durationMs) {
    auto animation = std::make_shared<NUIAnimation>();
    animation->setStartValue(startScale);
    animation->setEndValue(endScale);
    animation->setDuration(durationMs);
    animation->setEasing(NUIEasingType::EaseOutCubic);
    return animation;
}

std::shared_ptr<NUIAnimation> NUIAnimationUtils::createColorAnimation(
    const NUIColor& startColor, const NUIColor& endColor, float durationMs) {
    auto animation = std::make_shared<NUIAnimation>();
    animation->setStartValue(0.0f);
    animation->setEndValue(1.0f);
    animation->setDuration(durationMs);
    animation->setEasing(NUIEasingType::EaseOutCubic);
    return animation;
}

std::shared_ptr<NUIAnimation> NUIAnimationUtils::createPositionAnimation(
    const NUIPoint& startPos, const NUIPoint& endPos, float durationMs) {
    auto animation = std::make_shared<NUIAnimation>();
    animation->setStartValue(0.0f);
    animation->setEndValue(1.0f);
    animation->setDuration(durationMs);
    animation->setEasing(NUIEasingType::EaseOutCubic);
    return animation;
}

std::shared_ptr<NUIAnimation> NUIAnimationUtils::createOpacityAnimation(
    float startOpacity, float endOpacity, float durationMs) {
    auto animation = std::make_shared<NUIAnimation>();
    animation->setStartValue(startOpacity);
    animation->setEndValue(endOpacity);
    animation->setDuration(durationMs);
    animation->setEasing(NUIEasingType::EaseOutCubic);
    return animation;
}

} // namespace NomadUI
