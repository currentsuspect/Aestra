#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"
#include <string>

namespace NomadUI {

/**
 * NUILabel - A text display component
 * Replaces juce::Label with NomadUI styling and theming
 */
class NUILabel : public NUIComponent
{
public:
    // Text alignment options
    enum class Alignment
    {
        Left,
        Center,
        Right,
        Justified
    };

    NUILabel(const std::string& text = "");
    ~NUILabel() override = default;

    // Component interface
    void onRender(NUIRenderer& renderer) override;

    // Text properties
    void setText(const std::string& text);
    const std::string& getText() const { return text_; }

    // TODO: Implement font support when NUIFont is available
    // void setFont(const NUIFont& font);
    // const NUIFont& getFont() const { return font_; }

    void setTextColor(const NUIColor& color);
    NUIColor getTextColor() const { return textColor_; }

    void setAlignment(Alignment alignment);
    Alignment getAlignment() const { return alignment_; }

    void setMultiline(bool multiline);
    bool isMultiline() const { return multiline_; }

    void setWordWrap(bool wordWrap);
    bool isWordWrap() const { return wordWrap_; }

    // Background
    void setBackgroundColor(const NUIColor& color);
    NUIColor getBackgroundColor() const { return backgroundColor_; }

    void setBackgroundVisible(bool visible);
    bool isBackgroundVisible() const { return backgroundVisible_; }

    // Border
    void setBorderColor(const NUIColor& color);
    NUIColor getBorderColor() const { return borderColor_; }

    void setBorderWidth(float width);
    float getBorderWidth() const { return borderWidth_; }

    void setBorderVisible(bool visible);
    bool isBorderVisible() const { return borderVisible_; }

    // Utility
    void setEditable(bool editable);
    bool isEditable() const { return editable_; }

private:
    std::string text_;
    // NUIFont font_; // TODO: Add back when NUIFont is available
    NUIColor textColor_ = NUIColor::fromHex(0xffffffff);
    Alignment alignment_ = Alignment::Left;
    bool multiline_ = false;
    bool wordWrap_ = true;
    
    // Background
    NUIColor backgroundColor_ = NUIColor::fromHex(0x00000000); // Transparent
    bool backgroundVisible_ = false;
    
    // Border
    NUIColor borderColor_ = NUIColor::fromHex(0xff666666);
    float borderWidth_ = 1.0f;
    bool borderVisible_ = false;
    
    // Editable state
    bool editable_ = false;
    
    // OPTIMIZATION: Cache text measurements
    mutable NUISize cachedTextSize_{0, 0};
    mutable bool textSizeValid_ = false;
};

} // namespace NomadUI
