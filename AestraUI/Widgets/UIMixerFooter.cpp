// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerFooter.h"
#include "SlotText.h"
#include "../Graphics/NUIDepth.h"
#include "UIMixerFader.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"

#include <cmath>
#include <cstdio>

namespace AestraUI {

namespace {
constexpr float kFont = 11.0f;     // the dB pill: 11px / 500, tabular (board p-mixer2)
constexpr float kPillMinW = 58.0f;
constexpr float kPillH = 23.0f;
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
    const std::string text = db <= UIMixerFader::FADER_FLOOR_THRESHOLD ? formatDb(-90.0f) : formatDb(db);
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
    // Design Spec, Mixer (board p-mixer2): the gain sits in one sunk pill at the strip's foot, at least 58 wide and
    // 23 tall, 11px / 500, tabular. A Narrow strip keeps its small pill.
    if (m_compact) {
        Depth::bed(renderer, b, b.height * 0.5f, Depth::Cut::Shallow);
        renderer.drawTextCentered(m_gainText, b, 9.5f, m_text.withAlpha(0.95f));
        return;
    }
    const NUIRect gainPill{b.x, b.y + (b.height - kPillH) * 0.5f, std::min(b.width, std::max(kPillMinW, renderer.measureText(m_gainText, kFont).width + 16.0f)), kPillH};
    Depth::bed(renderer, gainPill, gainPill.height * 0.5f, Depth::Cut::Shallow);
    const float pillTextY = SlotText::slotTextY(renderer, gainPill, kFont);
    renderer.drawText(m_gainText, {std::round(gainPill.x + (gainPill.width - renderer.measureText(m_gainText, kFont).width) * 0.5f), pillTextY},
                      kFont, m_text);

    const float peakW = renderer.measureText(m_peakText, kFont).width;
    if (m_clipped) {
        const NUIRect block{b.right() - peakW - 8.0f, b.y + 2.0f, peakW + 6.0f, b.height - 4.0f};
        renderer.fillRoundedRect(block, 2.0f, m_clip);
        renderer.drawText(m_peakText, {block.x + 3.0f, SlotText::slotTextY(renderer, block, kFont)}, kFont, m_clipInk);
    } else {
        renderer.drawText(m_peakText, {b.right() - peakW - 2.0f, SlotText::slotTextY(renderer, b, kFont)}, kFont,
                          m_textSecondary.withAlpha(0.85f));
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
