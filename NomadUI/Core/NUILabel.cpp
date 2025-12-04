// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUILabel.h"
#include "NUITheme.h"
#include "Graphics/NUIRenderer.h"

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
            textSizeValid_ = true;
        }
        
        // Calculate text position based on alignment (top-left Y)
        float textX = bounds.x;
        float textY = bounds.y + (bounds.height - fontSize) * 0.5f;
        
        switch (alignment_)
        {
            case Alignment::Left:
                textX = bounds.x + 4.0f; // Small padding from left edge
                break;
            case Alignment::Center:
                textX = bounds.x + (bounds.width - cachedTextSize_.width) / 2.0f;
                break;
            case Alignment::Right:
                textX = bounds.x + bounds.width - cachedTextSize_.width - 4.0f; // Small padding from right edge
                break;
            case Alignment::Justified:
                textX = bounds.x + (bounds.width - cachedTextSize_.width) / 2.0f; // Center for now
                break;
        }
        
        renderer.drawText(text_, NUIPoint(textX, textY), fontSize, textColor_);
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
