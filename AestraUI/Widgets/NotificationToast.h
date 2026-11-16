// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "ToastStack.h"
#include <string>

namespace AestraUI {

/**
 * Transient status pills (toasts). The owner sets an anchor strip via setBounds, tall enough for kMaxToasts slots;
 * the newest pill sits in the bottom slot and older ones stack above it (ToastStack.h has the rules: up to three,
 * one shared wait time, the oldest pushed off by a fourth). It installs no mouse handlers, so it never steals
 * clicks from components beneath it.
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

private:
    ToastStack stack_;
};

} // namespace AestraUI
