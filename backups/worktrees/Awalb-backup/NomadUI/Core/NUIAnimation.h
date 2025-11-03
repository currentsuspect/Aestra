// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUITypes.h"
#include <functional>
#include <chrono>
#include <vector>

namespace NomadUI {

// Easing function types
enum class NUIEasingType {
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
    EaseOutCubic,
    EaseInCubic,
    EaseInOutCubic,
    EaseOutElastic,
    EaseInElastic,
    EaseInOutElastic,
    EaseOutBounce,
    EaseInBounce,
    EaseInOutBounce
};

// Animation state
enum class NUIAnimationState {
    Stopped,
    Running,
    Paused,
    Completed
};

// Animation direction
enum class NUIAnimationDirection {
    Forward,
    Reverse,
    Alternate
};

// Animation class for smooth property transitions
class NUIAnimation {
public:
    NUIAnimation();
    ~NUIAnimation() = default;

    // Animation properties
    void setDuration(float durationMs);
    void setEasing(NUIEasingType easing);
    void setDirection(NUIAnimationDirection direction);
    void setLoop(bool loop);
    void setDelay(float delayMs);

    // Value interpolation
    void setStartValue(float value);
    void setEndValue(float value);
    void setCurrentValue(float value);

    // Animation control
    void start();
    void stop();
    void pause();
    void resume();
    void reset();

    // Update animation (call each frame)
    void update(float deltaTime);

    // Getters
    float getCurrentValue() const { return currentValue_; }
    float getProgress() const { return progress_; }
    NUIAnimationState getState() const { return state_; }
    bool isRunning() const { return state_ == NUIAnimationState::Running; }
    bool isCompleted() const { return state_ == NUIAnimationState::Completed; }

    // Callbacks
    void setOnUpdate(std::function<void(float)> callback);
    void setOnComplete(std::function<void()> callback);

private:
    // Animation properties
    float duration_;
    float delay_;
    float progress_;
    float startValue_;
    float endValue_;
    float currentValue_;
    
    NUIEasingType easing_;
    NUIAnimationDirection direction_;
    NUIAnimationState state_;
    bool loop_;
    bool reverse_;

    // Timing
    std::chrono::high_resolution_clock::time_point startTime_;
    std::chrono::high_resolution_clock::time_point pauseTime_;
    float pausedDuration_;

    // Callbacks
    std::function<void(float)> onUpdate_;
    std::function<void()> onComplete_;

    // Easing functions
    float applyEasing(float t);
    float easeIn(float t);
    float easeOut(float t);
    float easeInOut(float t);
    float easeOutCubic(float t);
    float easeInCubic(float t);
    float easeInOutCubic(float t);
    float easeOutElastic(float t);
    float easeInElastic(float t);
    float easeInOutElastic(float t);
    float easeOutBounce(float t);
    float easeInBounce(float t);
    float easeInOutBounce(float t);
};

// Animation manager for handling multiple animations
class NUIAnimationManager {
public:
    static NUIAnimationManager& getInstance();

    // Animation management
    void addAnimation(std::shared_ptr<NUIAnimation> animation);
    void removeAnimation(std::shared_ptr<NUIAnimation> animation);
    void updateAll(float deltaTime);
    void clearAll();

    // Utility methods
    std::shared_ptr<NUIAnimation> createAnimation();
    void stopAllAnimations();

private:
    NUIAnimationManager() = default;
    std::vector<std::shared_ptr<NUIAnimation>> animations_;
};

// Utility functions for common animations
class NUIAnimationUtils {
public:
    // Create smooth scale animation
    static std::shared_ptr<NUIAnimation> createScaleAnimation(
        float startScale, float endScale, float durationMs = 200.0f);

    // Create smooth color transition
    static std::shared_ptr<NUIAnimation> createColorAnimation(
        const NUIColor& startColor, const NUIColor& endColor, float durationMs = 300.0f);

    // Create smooth position animation
    static std::shared_ptr<NUIAnimation> createPositionAnimation(
        const NUIPoint& startPos, const NUIPoint& endPos, float durationMs = 250.0f);

    // Create smooth opacity animation
    static std::shared_ptr<NUIAnimation> createOpacityAnimation(
        float startOpacity, float endOpacity, float durationMs = 200.0f);
};

} // namespace NomadUI
