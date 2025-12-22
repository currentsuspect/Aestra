// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUILabel.h"
#include "NUITheme.h"
#include "Graphics/NUIRenderer.h"
#include <algorithm>
#include <cmath>

namespace NomadUI {

NUILabel::NUILabel(const std::string& text)
    : text_(text)
{
    setSize(100, 20); // Default size
}

void NUILabel::onRender(NUIRenderer& renderer)
{
    auto bounds = getBounds();
    if (bounds.isEmpty()) return;

    // Draw background if visible
    if (backgroundVisible_)
    {
        renderer.fillRect(bounds, backgroundColor_);
    }

    // Draw border if visible
    if (borderVisible_ && borderWidth_ > 0.0f)
    {
        renderer.strokeRect(bounds, borderWidth_, borderColor_);
    }

    // Draw text
    if (!text_.empty())
    {
        float fontSize = fontSize_;
        
        // OPTIMIZATION: Cache text measurements - only measure when text or size changes
        if (!textSizeValid_) {
            cachedTextSize_ = renderer.measureText(text_, fontSize);
            
            // FIX: Add padding to account for visual overhangs and descenders
            // The renderer's measureText often returns tight typographic bounds (ascent-to-baseline-to-descent).
            // This causes clipping when glyphs visually extend beyond these bounds (e.g. anti-aliasing spread).
            // Adding +2.0f width and +4.0f height ensures the layout allocates enough space.
            cachedTextSize_.width += 2.0f;
            cachedTextSize_.height += 4.0f;
            
            textSizeValid_ = true;
        }
        
        std::string displayText = text_;
        NUISize displaySize = cachedTextSize_;

        // Single-line ellipsis truncation to avoid text bleeding into adjacent UI.
        const float pad = 4.0f;
        const float availableWidth = std::max(0.0f, bounds.width - pad * 2.0f);
        if (ellipsize_ && !multiline_ && availableWidth > 0.0f && cachedTextSize_.width > availableWidth) {
            const std::string ellipsis = "...";
            const float ellipsisW = renderer.measureText(ellipsis, fontSize).width;

            if (ellipsisW >= availableWidth) {
                displayText = ellipsis;
            } else {
                size_t lo = 0;
                size_t hi = text_.size();

                while (lo < hi) {
                    const size_t mid = (lo + hi + 1) / 2;
                    const std::string candidate = text_.substr(0, mid);
                    const float candidateW = renderer.measureText(candidate, fontSize).width;
                    if (candidateW + ellipsisW <= availableWidth) {
                        lo = mid;
                    } else {
                        hi = mid - 1;
                    }
                }

                displayText = text_.substr(0, lo) + ellipsis;
            }

            displaySize = renderer.measureText(displayText, fontSize);
        }
        
        // Calculate text position based on alignment
        // Use actual measured text height for proper vertical centering
        float textX = bounds.x;
        float textY = bounds.y + (bounds.height - displaySize.height) * 0.5f;
        
        switch (alignment_)
        {
            case Alignment::Left:
                textX = bounds.x + pad; // Small padding from left edge
                break;
            case Alignment::Center:
                textX = bounds.x + (bounds.width - displaySize.width) / 2.0f;
                break;
            case Alignment::Right:
                textX = bounds.x + bounds.width - displaySize.width - pad; // Small padding from right edge
                break;
            case Alignment::Justified:
                textX = bounds.x + pad; // Fallback to left for now
                break;
        }
        
        renderer.drawText(displayText, NUIPoint(std::round(textX), std::round(textY)), fontSize, textColor_);
    }
}

void NUILabel::setText(const std::string& text)
{
    if (text_ != text) {
        text_ = text;
        textSizeValid_ = false; // Invalidate cache when text changes
        repaint();
    }
}

// TODO: Implement font setting when NUIFont is available
// void NUILabel::setFont(const NUIFont& font)
// {
//     font_ = font;
//     repaint();
// }

void NUILabel::setTextColor(const NUIColor& color)
{
    textColor_ = color;
    repaint();
}

void NUILabel::setFontSize(float size)
{
    fontSize_ = size;
    textSizeValid_ = false;
    repaint();
}

void NUILabel::setAlignment(Alignment alignment)
{
    alignment_ = alignment;
    repaint();
}

void NUILabel::setMultiline(bool multiline)
{
    multiline_ = multiline;
    repaint();
}

void NUILabel::setWordWrap(bool wordWrap)
{
    wordWrap_ = wordWrap;
    repaint();
}

void NUILabel::setEllipsize(bool ellipsize)
{
    if (ellipsize_ != ellipsize) {
        ellipsize_ = ellipsize;
        repaint();
    }
}

void NUILabel::setBackgroundColor(const NUIColor& color)
{
    backgroundColor_ = color;
    repaint();
}

void NUILabel::setBackgroundVisible(bool visible)
{
    backgroundVisible_ = visible;
    repaint();
}

void NUILabel::setBorderColor(const NUIColor& color)
{
    borderColor_ = color;
    repaint();
}

void NUILabel::setBorderWidth(float width)
{
    borderWidth_ = width;
    repaint();
}

void NUILabel::setBorderVisible(bool visible)
{
    borderVisible_ = visible;
    repaint();
}

void NUILabel::setEditable(bool editable)
{
    editable_ = editable;
    // TODO: Implement text editing functionality
}

} // namespace NomadUI
