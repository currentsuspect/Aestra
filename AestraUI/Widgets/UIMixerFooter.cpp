// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerFooter.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"

#include <cmath>
#include <cstdio>

namespace AestraUI {

namespace {
constexpr float kFont = 10.5f;
constexpr float kFloorDb = -89.0f;
} // namespace

UIMixerFooter::UIMixerFooter() { cacheThemeColors(); }

void UIMixerFooter::cacheThemeColors() {
    auto& theme = NUIThemeManager::getInstance();
    m_text = theme.getColor("textPrimary");
    m_textSecondary = theme.getColor("textSecondary");
    m_clip = theme.getColor("meterCrit");
    m_clipInk = theme.getColor("backgroundPrimary");
}

std::string UIMixerFooter::formatDb(float db) {
    if (!std::isfinite(db) || db <= kFloorDb)
        return "\xE2\x88\x92\xE2\x88\x9E";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.1f", std::fabs(db) < 0.05f ? 0.0f : db);
    std::string out = buf;
    if (db < 0.0f && !out.empty() && out[0] == '-')
        out = "\xE2\x88\x92" + out.substr(1);
    return out;
}

void UIMixerFooter::setGainDb(float db) {
    // The fader floor reads as minus infinity, the same as the fader itself (UIMixerFader::FADER_FLOOR_THRESHOLD).
    const std::string text = db <= -60.0f ? formatDb(-90.0f) : formatDb(db);
    if (text == m_gainText)
        return;
    m_gainText = text;
    repaint();
    if (onInvalidateRequested)
        onInvalidateRequested();
}

void UIMixerFooter::setPeakDb(float db, bool clipped) {
    const std::string text = formatDb(db);
    if (text == m_peakText && clipped == m_clipped)
        return;
    m_peakText = text;
    m_clipped = clipped;
    repaint();
    if (onInvalidateRequested)
        onInvalidateRequested();
}

void UIMixerFooter::onRender(NUIRenderer& renderer) {
    const auto b = getBounds();
    if (b.isEmpty())
        return;
    const float ty = b.y + (b.height - kFont) * 0.5f - 1.0f;
    renderer.drawText(m_gainText, {b.x + 2.0f, ty}, kFont, m_text.withAlpha(0.95f));

    const float peakW = renderer.measureText(m_peakText, kFont).width;
    if (m_clipped) {
        const NUIRect block{b.right() - peakW - 8.0f, b.y + 2.0f, peakW + 6.0f, b.height - 4.0f};
        renderer.fillRoundedRect(block, 2.0f, m_clip);
        renderer.drawText(m_peakText, {block.x + 3.0f, ty}, kFont, m_clipInk);
    } else {
        renderer.drawText(m_peakText, {b.right() - peakW - 2.0f, ty}, kFont, m_textSecondary.withAlpha(0.85f));
    }
}

bool UIMixerFooter::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible() || !isEnabled())
        return false;
    const auto b = getBounds();
    // Only the right half is the peak; the gain on the left is just a readout.
    const NUIRect peakArea{b.x + b.width * 0.5f, b.y, b.width * 0.5f, b.height};
    if (event.pressed && event.button == NUIMouseButton::Left && peakArea.contains(event.position)) {
        if (onResetPeakRequested)
            onResetPeakRequested();
        return true;
    }
    return false;
}

} // namespace AestraUI
