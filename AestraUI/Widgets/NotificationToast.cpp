// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NotificationToast.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "NUIDepth.h"
#include <algorithm>

namespace AestraUI {

NotificationToast::NotificationToast()
{
    setDropPassThrough(true); // mouse-transparent status text: drops reach the panel under it
}

namespace {
constexpr float kActionH = 36.0f; // the pill; the slot around it is kSlotHeight
}

float NotificationToast::actionTop() const
{
    return getBounds().bottom() - kSlotHeight + (kSlotHeight - kActionH) * 0.5f;
}

NUIRect NotificationToast::actionRect() const
{
    if (pill_.width > 0.0f)
        return pill_;
    const NUIRect b = getBounds();
    constexpr float kGuess = 460.0f;
    return {b.x + (b.width - kGuess) * 0.5f, actionTop(), kGuess, kActionH};
}

void NotificationToast::onRender(NUIRenderer& renderer)
{
    drawAction(renderer);
    if (stack_.empty())
        return;

    auto& theme = NUIThemeManager::getInstance();
    const auto& t = theme.getCurrentTheme();
    const NUIRect bounds = getBounds();
    constexpr float kFontSize = 12.0f;
    constexpr double kFadeIn = 0.12;  // arrives quickly
    constexpr double kFadeOut = 0.35; // leaves gently instead of vanishing

    // Newest in the bottom slot, each older one a slot higher.
    const auto& entries = stack_.entries();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const ToastEntry& e = entries[entries.size() - 1 - i];
        const double remaining = kToastSeconds - e.age;
        const float alpha = static_cast<float>(std::clamp(std::min(e.age / kFadeIn, remaining / kFadeOut), 0.0, 1.0));
        const NUISize textSize = renderer.measureText(e.text, kFontSize);
        const float pillW = std::min(bounds.width, textSize.width + 32.0f);
        const float pillH = std::min(kSlotHeight, 32.0f);
        const float slotY = bounds.bottom() - kSlotHeight * static_cast<float>(i + 1 + (action_ ? 1 : 0));
        const NUIRect pill(bounds.x + (bounds.width - pillW) * 0.5f, slotY + (kSlotHeight - pillH) * 0.5f, pillW, pillH);
        // Design Spec, Toast (locked 12 Nov 2026): an accent-filled pill floating on a soft shadow, no outline.
        renderer.setOpacity(alpha);
        Depth::floatingCard(renderer, pill, pillH * 0.5f, t.primary);
        renderer.drawTextCentered(e.text, pill, kFontSize, t.textOnPrimary);
    }
    renderer.setOpacity(1.0f);
}

void NotificationToast::onUpdate(double deltaTime)
{
    if (action_) {
        if (!actionTimer_.update(deltaTime)) {
            actionAge_ += deltaTime;
        } else {
            finishAction(action_->onClose); // its time is up: the same as the x
        }
    }
    if (stack_.empty() && !action_) {
        setVisible(false);
        return;
    }
    stack_.update(deltaTime);
    repaint(); // animate the fades while anything is showing
}

bool NotificationToast::onMouseEvent(const NUIMouseEvent& event)
{
    if (!action_)
        return false; // passive overlay — never intercept
    const bool over = pill_.width > 0.0f && pill_.contains(event.position);
    actionTimer_.setHovered(over);
    if (!over || !event.pressed || event.button != NUIMouseButton::Left)
        return false; // only a press on the pill is ours; moves and everything else reach what is beneath
    if (primaryRect_.contains(event.position))
        finishAction(action_->onPrimary);
    else if (!action_->secondaryLabel.empty() && secondaryRect_.contains(event.position))
        finishAction(action_->onSecondary);
    else if (closeRect_.contains(event.position))
        finishAction(action_->onClose);
    return true; // a press on the pill's own surface does nothing, but never falls through to the panel under it
}

void NotificationToast::finishAction(const std::function<void()>& callback)
{
    std::function<void()> run = callback; // dismissing destroys the spec the callback came from
    dismissAction();
    if (run)
        run();
}

void NotificationToast::showAction(ActionToastSpec spec)
{
    action_ = std::move(spec);
    actionTimer_ = ActionToastTimer{};
    actionAge_ = 0.0;
    pill_ = {};
    setVisible(true);
    repaint();
}

void NotificationToast::dismissAction()
{
    action_.reset();
    pill_ = {};
    if (stack_.empty())
        setVisible(false);
    repaint();
}

void NotificationToast::drawAction(NUIRenderer& renderer)
{
    if (!action_)
        return;
    auto& theme = NUIThemeManager::getInstance();
    const auto& t = theme.getCurrentTheme();
    const NUIRect bounds = getBounds();
    constexpr float kFont = 12.0f, kPad = 12.0f, kGap = 12.0f, kBtnH = 26.0f, kBtnPad = 12.0f, kCloseW = 24.0f;

    const float textW = renderer.measureText(action_->text, kFont).width;
    const float primW = renderer.measureText(action_->primaryLabel, kFont).width + kBtnPad * 2.0f;
    const bool hasSecondary = !action_->secondaryLabel.empty();
    const float secW = hasSecondary ? renderer.measureText(action_->secondaryLabel, kFont).width + 16.0f : 0.0f;
    constexpr float kDot = 7.0f;
    const float inner = kDot + 8.0f + textW + kGap + primW + (hasSecondary ? 4.0f + secW : 0.0f) + 4.0f + kCloseW;
    const float w = std::min(bounds.width - 32.0f, inner + kPad);
    const float x = bounds.x + (bounds.width - w) * 0.5f;
    const float y = actionTop();
    pill_ = {x, y, w, kActionH};

    constexpr double kFadeIn = 0.12, kFadeOut = 0.35;
    const double remaining = kActionToastSeconds - actionAge_;
    renderer.setOpacity(static_cast<float>(std::clamp(std::min(actionAge_ / kFadeIn, remaining / kFadeOut), 0.0, 1.0)));

    // Design Spec, Toast: a floating card, no outline. The dot is the row's own colour; the primary is a raised cap.
    Depth::floatingCard(renderer, pill_, 8.0f, t.surfaceRaised);
    float cx = x + kPad;
    const float midY = y + kActionH * 0.5f;
    renderer.fillRoundedRect({cx, midY - kDot * 0.5f, kDot, kDot}, 2.0f, NUIColor::fromHex(action_->dotRgb));
    cx += kDot + 8.0f;
    renderer.drawText(action_->text, {cx, renderer.calculateTextY(NUIRect{0.0f, y, 0.0f, kActionH}, kFont)}, kFont, t.textPrimary);
    cx += textW + kGap;
    primaryRect_ = {cx, midY - kBtnH * 0.5f, primW, kBtnH};
    Depth::object(renderer, primaryRect_, 5.0f, Depth::ObjectLook{t.surfaceRaised.lightened(0.10f), false});
    renderer.drawTextCentered(action_->primaryLabel, primaryRect_, kFont, t.textPrimary);
    cx += primW + 4.0f;
    secondaryRect_ = {};
    if (hasSecondary) {
        secondaryRect_ = {cx, midY - kBtnH * 0.5f, secW, kBtnH};
        renderer.drawTextCentered(action_->secondaryLabel, secondaryRect_, kFont, t.textSecondary);
        cx += secW + 4.0f;
    }
    closeRect_ = {cx, midY - kBtnH * 0.5f, kCloseW, kBtnH};
    renderer.drawTextCentered("\xC3\x97", closeRect_, kFont + 2.0f, t.textMuted); // the multiplication sign
    renderer.setOpacity(1.0f);
}

void NotificationToast::show(const std::string& text)
{
    stack_.push(text);
    setVisible(true);
    repaint();
}

} // namespace AestraUI
