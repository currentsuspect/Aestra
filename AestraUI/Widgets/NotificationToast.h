// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "ToastStack.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace AestraUI {

/// A toast with buttons: a dot, one sentence, a raised primary button, a text button and a close x. At most one shows
/// (a new one replaces it); it sits in the bottom slot and text toasts stack above it.
struct ActionToastSpec {
    std::string text;
    uint32_t dotRgb = 0x808080; ///< 0xRRGGBB: the colour of the thing the toast is about (never the accent)
    std::string primaryLabel;
    std::string secondaryLabel;
    std::function<void()> onPrimary;
    std::function<void()> onSecondary;
    std::function<void()> onClose; ///< the x, or the time running out
};

/**
 * Transient status pills (toasts). The owner sets an anchor strip via setBounds, tall enough for kMaxToasts slots;
 * the newest pill sits in the bottom slot and older ones stack above it (ToastStack.h has the rules: up to three,
 * one shared wait time, the oldest pushed off by a fourth). It installs no mouse handlers, so it never steals
 * clicks from components beneath it, except on an action toast's own pill (showAction), which takes presses on its
 * buttons and holds its time while the pointer is over it.
 */
class NotificationToast : public NUIComponent {
public:
    /// Height of one slot (a 32 px pill and the gap above it); the owner's strip is kMaxToasts slots tall.
    static constexpr float kSlotHeight = 40.0f;

    NotificationToast();

    void onRender(NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;
    /** Always mouse-transparent: the toast is a passive overlay and must never
        consume clicks from the panels beneath it, even while it owns the
        top-most z-order. */
    bool onMouseEvent(const NUIMouseEvent& event) override;

    /** Shows @p text as the newest toast. Every toast stays kToastSeconds. */
    void show(const std::string& text);

    const ToastStack& stack() const { return stack_; }

    /** Shows @p spec in the bottom slot for kActionToastSeconds; replaces any action toast showing. */
    void showAction(ActionToastSpec spec);
    /** Removes the action toast without calling any of its callbacks. */
    void dismissAction();
    bool actionShowing() const { return action_.has_value(); }
    /** The pill's top edge, fixed by the strip: the top the owner keeps the list clear of. */
    float actionTop() const;
    /** The pill's rectangle, as last drawn (a centred estimate before the first draw). */
    NUIRect actionRect() const;

private:
    void drawAction(NUIRenderer& renderer);
    void finishAction(const std::function<void()>& callback);

    ToastStack stack_;
    std::optional<ActionToastSpec> action_;
    ActionToastTimer actionTimer_;
    double actionAge_ = 0.0; ///< seconds on screen, for the fade only
    NUIRect pill_{}, primaryRect_{}, secondaryRect_{}, closeRect_{};
};

} // namespace AestraUI
