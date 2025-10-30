// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"
#include <functional>
#include <string>

namespace NomadUI {

/**
 * NUITextInput - A text input field component
 * Supports single-line and multi-line text input with cursor and selection
 * Replaces juce::TextEditor with NomadUI styling and theming
 */
class NUITextInput : public NUIComponent
{
public:
    // Input types
    enum class InputType
    {
        Text,           // General text input
        Password,       // Password input (masked)
        Number,         // Numeric input only
        Email,          // Email input
        URL             // URL input
    };

    // Text justification
    enum class Justification
    {
        Left,
        Center,
        Right
    };

    NUITextInput(const std::string& text = "");
    ~NUITextInput() override = default;

    // Component interface
    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    bool onKeyEvent(const NUIKeyEvent& event) override;
    void onFocusGained() override;
    void onFocusLost() override;
    void onMouseEnter() override;
    void onMouseLeave() override;

    // Text properties
    void setText(const std::string& text);
    const std::string& getText() const { return text_; }

    void setPlaceholderText(const std::string& placeholder);
    const std::string& getPlaceholderText() const { return placeholderText_; }

    void setInputType(InputType type);
    InputType getInputType() const { return inputType_; }

    void setJustification(Justification justification);
    Justification getJustification() const { return justification_; }

    void setMultiline(bool multiline);
    bool isMultiline() const { return multiline_; }

    void setWordWrap(bool wordWrap);
    bool isWordWrap() const { return wordWrap_; }

    void setReadOnly(bool readOnly);
    bool isReadOnly() const { return readOnly_; }

    void setPasswordCharacter(char character);
    char getPasswordCharacter() const { return passwordCharacter_; }

    // Selection and cursor
    void setSelection(int start, int end);
    int getSelectionStart() const { return selectionStart_; }
    int getSelectionEnd() const { return selectionEnd_; }
    int getSelectionLength() const { return selectionEnd_ - selectionStart_; }

    void setCaretPosition(int position);
    int getCaretPosition() const { return caretPosition_; }

    void selectAll();
    void clearSelection();

    // Text limits
    void setMaxLength(int maxLength);
    int getMaxLength() const { return maxLength_; }

    void setMinLength(int minLength);
    int getMinLength() const { return minLength_; }

    // Visual properties
    void setTextColor(const NUIColor& color);
    NUIColor getTextColor() const { return textColor_; }

    void setBackgroundColor(const NUIColor& color);
    NUIColor getBackgroundColor() const { return backgroundColor_; }

    void setBorderColor(const NUIColor& color);
    NUIColor getBorderColor() const { return borderColor_; }

    void setFocusedBorderColor(const NUIColor& color);
    NUIColor getFocusedBorderColor() const { return focusedBorderColor_; }

    void setPlaceholderColor(const NUIColor& color);
    NUIColor getPlaceholderColor() const { return placeholderColor_; }

    void setSelectionColor(const NUIColor& color);
    NUIColor getSelectionColor() const { return selectionColor_; }

    void setCaretColor(const NUIColor& color);
    NUIColor getCaretColor() const { return caretColor_; }
    
    // Validation properties
    void setValidationError(bool hasError);
    bool hasValidationError() const { return hasValidationError_; }
    
    void setValidationSuccess(bool hasSuccess);
    bool hasValidationSuccess() const { return hasValidationSuccess_; }

    void setBorderWidth(float width);
    float getBorderWidth() const { return borderWidth_; }

    void setBorderRadius(float radius);
    float getBorderRadius() const { return borderRadius_; }

    void setPadding(float padding);
    float getPadding() const { return padding_; }

    // Scrolling
    void setScrollBarVisible(bool visible);
    bool isScrollBarVisible() const { return scrollBarVisible_; }

    void setScrollPosition(float position);
    float getScrollPosition() const { return scrollPosition_; }

    // Event callbacks
    void setOnTextChange(std::function<void(const std::string&)> callback);
    void setOnReturnKey(std::function<void()> callback);
    void setOnEscapeKey(std::function<void()> callback);
    void setOnFocusGained(std::function<void()> callback);
    void setOnFocusLost(std::function<void()> callback);

    // Utility
    void clear();
    void insertText(const std::string& text);
    void deleteSelectedText();
    void deleteText(int start, int end);
    std::string getSelectedText() const;
    void setTextToShowWhenEmpty(const std::string& text);

protected:
    // Override these for custom text input behavior
    virtual void drawBackground(NUIRenderer& renderer);
    virtual void drawText(NUIRenderer& renderer);
    virtual void drawSelection(NUIRenderer& renderer);
    virtual void drawCaret(NUIRenderer& renderer);
    virtual void drawPlaceholder(NUIRenderer& renderer);

    // Text layout
    virtual void updateTextLayout();
    virtual NUIPoint getTextPosition(int characterIndex) const;
    virtual int getCharacterIndexAtPosition(const NUIPoint& position) const;

    // Input validation
    virtual bool isValidCharacter(char character) const;
    virtual bool isValidText(const std::string& text) const;

private:
    void updateCaretPosition();
    void updateSelection();
    void handleTextInput(const std::string& text);
    void handleKeyInput(const NUIKeyEvent& event);
    void moveCaret(int direction, bool extendSelection = false);
    void moveCaretToLine(int line, bool extendSelection = false);
    void moveCaretToWord(int direction, bool extendSelection = false);
    void deleteCharacter(int direction);
    void insertCharacter(char character);
    void triggerTextChange();
    
    // Enhanced drawing methods
    void drawEnhancedBackground(NUIRenderer& renderer);
    void drawAnimatedCaret(NUIRenderer& renderer);
    void triggerReturnKey();
    void triggerEscapeKey();

    // Text content
    std::string text_;
    std::string placeholderText_;
    InputType inputType_ = InputType::Text;
    Justification justification_ = Justification::Left;
    bool multiline_ = false;
    bool wordWrap_ = true;
    bool readOnly_ = false;
    char passwordCharacter_ = '*';

    // Selection and cursor
    int selectionStart_ = 0;
    int selectionEnd_ = 0;
    int caretPosition_ = 0;
    bool hasSelection_ = false;

    // Text limits
    int maxLength_ = 0; // 0 = no limit
    int minLength_ = 0;

    // Visual properties
    NUIColor textColor_ = NUIColor::fromHex(0xffffffff);
    NUIColor backgroundColor_ = NUIColor::fromHex(0xff1a1d22);
    NUIColor borderColor_ = NUIColor::fromHex(0xff666666);
    NUIColor focusedBorderColor_ = NUIColor::fromHex(0xffa855f7);
    NUIColor placeholderColor_ = NUIColor::fromHex(0xff888888);
    NUIColor selectionColor_ = NUIColor::fromHex(0xffa855f7);
    NUIColor caretColor_ = NUIColor::fromHex(0xffffffff);
    float borderWidth_ = 1.0f;
    float borderRadius_ = 4.0f;
    float padding_ = 8.0f;

    // Scrolling
    bool scrollBarVisible_ = true;
    float scrollPosition_ = 0.0f;

    // Interaction state
    bool isFocused_ = false;
    bool isHovered_ = false;
    bool isPressed_ = false;
    bool showCaret_ = true;
    double caretBlinkTime_ = 0.0;
    
    // Validation state
    bool hasValidationError_ = false;
    bool hasValidationSuccess_ = false;

    // Text layout cache
    std::vector<std::string> lines_;
    std::vector<float> lineHeights_;
    float totalTextHeight_ = 0.0f;

    // Callbacks
    std::function<void(const std::string&)> onTextChangeCallback_;
    std::function<void()> onReturnKeyCallback_;
    std::function<void()> onEscapeKeyCallback_;
    std::function<void()> onFocusGainedCallback_;
    std::function<void()> onFocusLostCallback_;
};

} // namespace NomadUI
