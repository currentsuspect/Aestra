// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include <string>

namespace AestraUI {

/**
 * Transient status pill (toast). The owner sets an anchor strip via
 * setBounds; the toast draws a centered pill sized to its text and hides
 * itself after the configured duration (with a short fade). It installs no
 * mouse handlers, so it never steals clicks from components beneath it.
 */
class NotificationToast : public NUIComponent {
public:
    NotificationToast();

    void onRender(NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;

    void setText(const std::string& text);
    const std::string& getText() const { return text_; }

    /** Restarts the visible window; the toast hides itself when it elapses. */
    void setDuration(double duration);

private:
    std::string text_;
    double duration_;
    double elapsed_;
};

} // namespace AestraUI
