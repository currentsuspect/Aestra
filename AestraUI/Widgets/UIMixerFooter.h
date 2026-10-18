// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"

#include <functional>
#include <string>

namespace AestraUI {

/**
 * @brief Mixer strip footer (V81-M1, FD-28 point 3): the fader's gain on the left, the highest peak since the
 *        last reset on the right. A clip turns the peak into a red block; a click on it resets the peak.
 */
class UIMixerFooter : public NUIComponent {
public:
    UIMixerFooter();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onThemeChanged(const NUIThemeProperties& theme) override { cacheThemeColors(); NUIComponent::onThemeChanged(theme); }

    /// Narrow density: the gain only.
    void setCompact(bool compact) { if (compact != m_compact) { m_compact = compact; repaint(); } }
    void setGainDb(float db);
    void setPeakDb(float db, bool clipped);

    /// "−4.5", "0.0", "−∞" for the footer's two numbers.
    static std::string formatDb(float db);

    const std::string& gainText() const { return m_gainText; }
    const std::string& peakText() const { return m_peakText; }
    bool clipped() const { return m_clipped; }

    std::function<void()> onInvalidateRequested;
    /// Clicking the peak asks for it (and the clip latch) to be cleared.
    std::function<void()> onResetPeakRequested;

private:
    std::string m_gainText{"0.0"};
    std::string m_peakText{"\xE2\x88\x92\xE2\x88\x9E"};
    bool m_clipped{false};
    bool m_compact{false};

    NUIColor m_text;
    NUIColor m_textSecondary;
    NUIColor m_clip;
    NUIColor m_clipInk;

    void cacheThemeColors();
};

} // namespace AestraUI
