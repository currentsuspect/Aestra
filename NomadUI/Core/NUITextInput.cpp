// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUITextInput.h"
#include "../Graphics/NUIRenderer.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cctype>

namespace NomadUI {

NUITextInput::NUITextInput(const std::string& text)
    : NUIComponent()
    , text_(text)
{
    setSize(200, 30); // Default size
    updateTextLayout();
}

void NUITextInput::onRender(NUIRenderer& renderer)
{
    if (!isVisible()) return;

    // Enhanced background with inner shadows and focus glow
    drawEnhancedBackground(renderer);
    
    if (hasSelection_)
    {
        drawSelection(renderer);
    }
    
    if (text_.empty() && !placeholderText_.empty())
    {
        drawPlaceholder(renderer);
    }
    else
    {
        drawText(renderer);
    }
    
    if (isFocused_ && showCaret_)
    {
        drawAnimatedCaret(renderer);
    }
}

bool NUITextInput::onMouseEvent(const NUIMouseEvent& event)
{
    if (!isVisible()) return false;

    NUIRect bounds = getBounds();
    if (!bounds.contains(event.position)) return false;

    if (event.pressed && event.button == NUIMouseButton::Left)
    {
        isPressed_ = true;
        
        // Set caret position based on mouse click
        int newCaretPos = getCharacterIndexAtPosition(event.position);
        setCaretPosition(newCaretPos);
        
        // Clear selection if not extending
        if (!(event.modifiers & NUIModifiers::Shift))
        {
            clearSelection();
        }
        else
        {
            // Extend selection
            if (selectionStart_ == selectionEnd_)
            {
                selectionStart_ = caretPosition_;
            }
            selectionEnd_ = caretPosition_;
            hasSelection_ = true;
        }
        
        setDirty(true);
        return true;
    }
    else if (event.released && event.button == NUIMouseButton::Left)
    {
        isPressed_ = false;
        setDirty(true);
        return true;
    }

    return false;
}

bool NUITextInput::onKeyEvent(const NUIKeyEvent& event)
{
    if (!isFocused_ || !isVisible()) return false;

    if (event.pressed)
    {
        handleKeyInput(event);
        return true;
    }

    return false;
}

void NUITextInput::onFocusGained()
{
    isFocused_ = true;
    showCaret_ = true;
    caretBlinkTime_ = 0.0;
    
    if (onFocusGainedCallback_)
    {
        onFocusGainedCallback_();
    }
    
    setDirty(true);
}

void NUITextInput::onFocusLost()
{
    isFocused_ = false;
    showCaret_ = false;
    clearSelection();
    
    if (onFocusLostCallback_)
    {
        onFocusLostCallback_();
    }
    
    setDirty(true);
}

void NUITextInput::onMouseEnter()
{
    isHovered_ = true;
    setDirty(true);
}

void NUITextInput::onMouseLeave()
{
    isHovered_ = false;
    isPressed_ = false;
    setDirty(true);
}

void NUITextInput::setText(const std::string& text)
{
    if (text_ != text)
    {
        text_ = text;
        updateTextLayout();
        setCaretPosition(static_cast<int>(text_.length()));
        clearSelection();
        triggerTextChange();
        setDirty(true);
    }
}

void NUITextInput::setPlaceholderText(const std::string& placeholder)
{
    placeholderText_ = placeholder;
    setDirty(true);
}

void NUITextInput::setInputType(InputType type)
{
    inputType_ = type;
    setDirty(true);
}

void NUITextInput::setJustification(Justification justification)
{
    justification_ = justification;
    updateTextLayout();
    setDirty(true);
}

void NUITextInput::setMultiline(bool multiline)
{
    multiline_ = multiline;
    updateTextLayout();
    setDirty(true);
}

void NUITextInput::setWordWrap(bool wordWrap)
{
    wordWrap_ = wordWrap;
    updateTextLayout();
    setDirty(true);
}

void NUITextInput::setReadOnly(bool readOnly)
{
    readOnly_ = readOnly;
    setDirty(true);
}

void NUITextInput::setPasswordCharacter(char character)
{
    passwordCharacter_ = character;
    setDirty(true);
}

void NUITextInput::setSelection(int start, int end)
{
    int textLength = static_cast<int>(text_.length());
    selectionStart_ = std::clamp(start, 0, textLength);
    selectionEnd_ = std::clamp(end, 0, textLength);
    hasSelection_ = (selectionStart_ != selectionEnd_);
    setDirty(true);
}

void NUITextInput::setCaretPosition(int position)
{
    int textLength = static_cast<int>(text_.length());
    caretPosition_ = std::clamp(position, 0, textLength);
    setDirty(true);
}

void NUITextInput::selectAll()
{
    if (!text_.empty())
    {
        selectionStart_ = 0;
        selectionEnd_ = static_cast<int>(text_.length());
        hasSelection_ = true;
        setDirty(true);
    }
}

void NUITextInput::clearSelection()
{
    selectionStart_ = 0;
    selectionEnd_ = 0;
    hasSelection_ = false;
    setDirty(true);
}

void NUITextInput::setMaxLength(int maxLength)
{
    maxLength_ = maxLength;
    
    // Truncate text if it exceeds max length
    if (maxLength_ > 0 && static_cast<int>(text_.length()) > maxLength_)
    {
        text_ = text_.substr(0, maxLength_);
        updateTextLayout();
        setCaretPosition(static_cast<int>(text_.length()));
        clearSelection();
        setDirty(true);
    }
}

void NUITextInput::setMinLength(int minLength)
{
    minLength_ = minLength;
}

void NUITextInput::setTextColor(const NUIColor& color)
{
    textColor_ = color;
    setDirty(true);
}

void NUITextInput::setBackgroundColor(const NUIColor& color)
{
    backgroundColor_ = color;
    setDirty(true);
}

void NUITextInput::setBorderColor(const NUIColor& color)
{
    borderColor_ = color;
    setDirty(true);
}

void NUITextInput::setFocusedBorderColor(const NUIColor& color)
{
    focusedBorderColor_ = color;
    setDirty(true);
}

void NUITextInput::setPlaceholderColor(const NUIColor& color)
{
    placeholderColor_ = color;
    setDirty(true);
}

void NUITextInput::setSelectionColor(const NUIColor& color)
{
    selectionColor_ = color;
    setDirty(true);
}

void NUITextInput::setCaretColor(const NUIColor& color)
{
    caretColor_ = color;
    setDirty(true);
}

void NUITextInput::setValidationError(bool hasError)
{
    hasValidationError_ = hasError;
    if (hasError) hasValidationSuccess_ = false; // Clear success when error
    setDirty(true);
}

void NUITextInput::setValidationSuccess(bool hasSuccess)
{
    hasValidationSuccess_ = hasSuccess;
    if (hasSuccess) hasValidationError_ = false; // Clear error when success
    setDirty(true);
}

void NUITextInput::setBorderWidth(float width)
{
    borderWidth_ = width;
    setDirty(true);
}

void NUITextInput::setBorderRadius(float radius)
{
    borderRadius_ = radius;
    setDirty(true);
}

void NUITextInput::setPadding(float padding)
{
    padding_ = padding;
    updateTextLayout();
    setDirty(true);
}

void NUITextInput::setScrollBarVisible(bool visible)
{
    scrollBarVisible_ = visible;
    setDirty(true);
}

void NUITextInput::setScrollPosition(float position)
{
    scrollPosition_ = std::clamp(position, 0.0f, 1.0f);
    setDirty(true);
}

void NUITextInput::setOnTextChange(std::function<void(const std::string&)> callback)
{
    onTextChangeCallback_ = callback;
}

void NUITextInput::setOnReturnKey(std::function<void()> callback)
{
    onReturnKeyCallback_ = callback;
}

void NUITextInput::setOnEscapeKey(std::function<void()> callback)
{
    onEscapeKeyCallback_ = callback;
}

void NUITextInput::setOnFocusGained(std::function<void()> callback)
{
    onFocusGainedCallback_ = callback;
}

void NUITextInput::setOnFocusLost(std::function<void()> callback)
{
    onFocusLostCallback_ = callback;
}

void NUITextInput::clear()
{
    setText("");
}

void NUITextInput::insertText(const std::string& text)
{
    if (readOnly_) return;
    
    // Delete selected text first
    if (hasSelection_)
    {
        deleteSelectedText();
    }
    
    // Insert new text
    text_.insert(caretPosition_, text);
    
    // Move caret to end of inserted text
    setCaretPosition(caretPosition_ + static_cast<int>(text.length()));
    
    updateTextLayout();
    triggerTextChange();
    setDirty(true);
}

void NUITextInput::deleteSelectedText()
{
    if (!hasSelection_) return;
    
    text_.erase(selectionStart_, selectionEnd_ - selectionStart_);
    setCaretPosition(selectionStart_);
    clearSelection();
    updateTextLayout();
    triggerTextChange();
    setDirty(true);
}

void NUITextInput::deleteText(int start, int end)
{
    int textLength = static_cast<int>(text_.length());
    start = std::clamp(start, 0, textLength);
    end = std::clamp(end, 0, textLength);
    
    if (start < end)
    {
        text_.erase(start, end - start);
        setCaretPosition(start);
        updateTextLayout();
        triggerTextChange();
        setDirty(true);
    }
}

std::string NUITextInput::getSelectedText() const
{
    if (!hasSelection_) return "";
    
    return text_.substr(selectionStart_, selectionEnd_ - selectionStart_);
}

void NUITextInput::setTextToShowWhenEmpty(const std::string& text)
{
    setPlaceholderText(text);
}

void NUITextInput::drawBackground(NUIRenderer& renderer)
{
    NUIRect bounds = getBounds();
    
    // Draw background
    renderer.fillRoundedRect(bounds, borderRadius_, backgroundColor_);
    
    // Draw border
    NUIColor borderColor = isFocused_ ? focusedBorderColor_ : borderColor_;
    renderer.strokeRoundedRect(bounds, borderRadius_, borderWidth_, borderColor);
}

void NUITextInput::drawText(NUIRenderer& renderer)
{
    if (text_.empty()) return;
    
    NUIRect bounds = getBounds();
    NUIRect textRect(bounds.x + padding_, bounds.y + padding_, 
                    bounds.width - padding_ * 2, bounds.height - padding_ * 2);
    
    // Get display text (masked for password)
    std::string displayText = text_;
    if (inputType_ == InputType::Password)
    {
        displayText = std::string(text_.length(), passwordCharacter_);
    }
    
    // TODO: Implement text rendering when NUIFont is available
    // For now, this is a placeholder
    // renderer.drawText(displayText, textRect, justification_, textColor_);
}

void NUITextInput::drawSelection(NUIRenderer& renderer)
{
    if (!hasSelection_) return;
    
    // TODO: Implement selection rendering
    // This would draw a highlighted rectangle over the selected text
}

void NUITextInput::drawCaret(NUIRenderer& renderer)
{
    if (!isFocused_ || !showCaret_) return;
    
    // TODO: Implement caret rendering
    // This would draw a blinking vertical line at the caret position
}

void NUITextInput::drawPlaceholder(NUIRenderer& renderer)
{
    if (placeholderText_.empty()) return;
    
    NUIRect bounds = getBounds();
    NUIRect textRect(bounds.x + padding_, bounds.y + padding_, 
                    bounds.width - padding_ * 2, bounds.height - padding_ * 2);
    
    // TODO: Implement placeholder text rendering
    // renderer.drawText(placeholderText_, textRect, justification_, placeholderColor_);
}

void NUITextInput::updateTextLayout()
{
    // TODO: Implement text layout calculation
    // This would break text into lines, calculate line heights, etc.
    lines_.clear();
    lineHeights_.clear();
    
    if (multiline_)
    {
        // Split text into lines
        // For now, just put everything in one line
        lines_.push_back(text_);
        lineHeights_.push_back(20.0f); // Default line height
    }
    else
    {
        lines_.push_back(text_);
        lineHeights_.push_back(20.0f);
    }
    
    totalTextHeight_ = 0.0f;
    for (float height : lineHeights_)
    {
        totalTextHeight_ += height;
    }
}

NUIPoint NUITextInput::getTextPosition(int characterIndex) const
{
    // TODO: Implement text position calculation
    // This would return the screen position of a character
    return NUIPoint(0, 0);
}

int NUITextInput::getCharacterIndexAtPosition(const NUIPoint& position) const
{
    // TODO: Implement character index calculation
    // This would return the character index at a given screen position
    return 0;
}

bool NUITextInput::isValidCharacter(char character) const
{
    switch (inputType_)
    {
        case InputType::Number:
            return std::isdigit(character) || character == '.' || character == '-';
        case InputType::Email:
            return std::isalnum(character) || character == '@' || character == '.' || character == '_' || character == '-';
        case InputType::URL:
            return std::isalnum(character) || character == '.' || character == '/' || character == ':' || character == '?' || character == '&' || character == '=';
        default:
            return true;
    }
}

bool NUITextInput::isValidText(const std::string& text) const
{
    for (char c : text)
    {
        if (!isValidCharacter(c))
            return false;
    }
    return true;
}

void NUITextInput::updateCaretPosition()
{
    // TODO: Implement caret position update
    setDirty(true);
}

void NUITextInput::updateSelection()
{
    // TODO: Implement selection update
    setDirty(true);
}

void NUITextInput::handleTextInput(const std::string& text)
{
    if (readOnly_) return;
    
    for (char c : text)
    {
        if (isValidCharacter(c))
        {
            insertCharacter(c);
        }
    }
}

void NUITextInput::handleKeyInput(const NUIKeyEvent& event)
{
    if (readOnly_) return;
    
    switch (event.keyCode)
    {
        case NUIKeyCode::Enter:
            if (multiline_)
            {
                insertCharacter('\n');
            }
            else
            {
                triggerReturnKey();
            }
            break;
            
        case NUIKeyCode::Escape:
            triggerEscapeKey();
            break;
            
        case NUIKeyCode::Backspace:
            if (hasSelection_)
            {
                deleteSelectedText();
            }
            else if (caretPosition_ > 0)
            {
                deleteCharacter(-1);
            }
            break;
            
        case NUIKeyCode::Delete:
            if (hasSelection_)
            {
                deleteSelectedText();
            }
            else if (caretPosition_ < static_cast<int>(text_.length()))
            {
                deleteCharacter(1);
            }
            break;
            
        case NUIKeyCode::Left:
            moveCaret(-1, event.modifiers & NUIModifiers::Shift);
            break;
            
        case NUIKeyCode::Right:
            moveCaret(1, event.modifiers & NUIModifiers::Shift);
            break;
            
        case NUIKeyCode::Up:
            if (multiline_)
            {
                moveCaretToLine(-1, event.modifiers & NUIModifiers::Shift);
            }
            break;
            
        case NUIKeyCode::Down:
            if (multiline_)
            {
                moveCaretToLine(1, event.modifiers & NUIModifiers::Shift);
            }
            break;
            
        case NUIKeyCode::A:
            if (event.modifiers & NUIModifiers::Ctrl)
            {
                selectAll();
            }
            break;
            
        case NUIKeyCode::C:
            if (event.modifiers & NUIModifiers::Ctrl)
            {
                // TODO: Copy to clipboard
            }
            break;
            
        case NUIKeyCode::V:
            if (event.modifiers & NUIModifiers::Ctrl)
            {
                // TODO: Paste from clipboard
            }
            break;
            
        case NUIKeyCode::X:
            if (event.modifiers & NUIModifiers::Ctrl)
            {
                // TODO: Cut to clipboard
            }
            break;
            
        default:
            if (event.character != 0)
            {
                insertCharacter(event.character);
            }
            break;
    }
}

void NUITextInput::moveCaret(int direction, bool extendSelection)
{
    int newPos = caretPosition_ + direction;
    newPos = std::clamp(newPos, 0, static_cast<int>(text_.length()));
    
    if (extendSelection)
    {
        if (selectionStart_ == selectionEnd_)
        {
            selectionStart_ = caretPosition_;
        }
        selectionEnd_ = newPos;
        hasSelection_ = true;
    }
    else
    {
        clearSelection();
    }
    
    setCaretPosition(newPos);
}

void NUITextInput::moveCaretToLine(int line, bool extendSelection)
{
    // TODO: Implement line-based caret movement
}

void NUITextInput::moveCaretToWord(int direction, bool extendSelection)
{
    // TODO: Implement word-based caret movement
}

void NUITextInput::deleteCharacter(int direction)
{
    if (direction < 0 && caretPosition_ > 0)
    {
        text_.erase(caretPosition_ - 1, 1);
        setCaretPosition(caretPosition_ - 1);
    }
    else if (direction > 0 && caretPosition_ < static_cast<int>(text_.length()))
    {
        text_.erase(caretPosition_, 1);
    }
    
    updateTextLayout();
    triggerTextChange();
    setDirty(true);
}

void NUITextInput::insertCharacter(char character)
{
    if (maxLength_ > 0 && static_cast<int>(text_.length()) >= maxLength_)
        return;
    
    text_.insert(caretPosition_, 1, character);
    setCaretPosition(caretPosition_ + 1);
    updateTextLayout();
    triggerTextChange();
    setDirty(true);
}

void NUITextInput::triggerTextChange()
{
    if (onTextChangeCallback_)
    {
        onTextChangeCallback_(text_);
    }
}

void NUITextInput::triggerReturnKey()
{
    if (onReturnKeyCallback_)
    {
        onReturnKeyCallback_();
    }
}

void NUITextInput::triggerEscapeKey()
{
    if (onEscapeKeyCallback_)
    {
        onEscapeKeyCallback_();
    }
}

void NUITextInput::drawEnhancedBackground(NUIRenderer& renderer)
{
    NUIRect bounds = getBounds();
    
    // Focus glow effect
    if (isFocused_)
    {
        NUIRect glowRect = bounds;
        glowRect.x -= 2;
        glowRect.y -= 2;
        glowRect.width += 4;
        glowRect.height += 4;
        renderer.fillRoundedRect(glowRect, borderRadius_ + 2, 
            NUIColor::fromHex(0x0078d4).withAlpha(0.3f));
    }
    
    // Inner shadow effect
    NUIRect innerShadowRect = bounds;
    innerShadowRect.x += 1;
    innerShadowRect.y += 1;
    innerShadowRect.width -= 2;
    innerShadowRect.height -= 2;
    renderer.fillRoundedRect(innerShadowRect, borderRadius_ - 1, 
        NUIColor(0, 0, 0, 0.1f));
    
    // Main background with gradient
    NUIColor topColor = backgroundColor_.lightened(0.05f);
    NUIColor bottomColor = backgroundColor_.darkened(0.05f);
    
    for (int i = 0; i < 2; ++i)
    {
        float factor = static_cast<float>(i);
        NUIColor gradientColor = NUIColor::lerp(topColor, bottomColor, factor);
        NUIRect gradientRect = bounds;
        gradientRect.y += i;
        gradientRect.height -= i;
        renderer.fillRoundedRect(gradientRect, borderRadius_, gradientColor);
    }
    
    // Border with validation highlighting
    NUIColor borderColor = borderColor_;
    float borderWidth = 1.0f;
    
    if (isFocused_)
    {
        borderColor = NUIColor::fromHex(0x0078d4);
        borderWidth = 2.0f;
    }
    else if (hasValidationError_)
    {
        borderColor = NUIColor::fromHex(0xff4444); // Red for errors
        borderWidth = 2.0f;
    }
    else if (hasValidationSuccess_)
    {
        borderColor = NUIColor::fromHex(0x44ff44); // Green for success
        borderWidth = 2.0f;
    }
    
    renderer.strokeRoundedRect(bounds, borderRadius_, borderWidth, borderColor);
}

void NUITextInput::drawAnimatedCaret(NUIRenderer& renderer)
{
    if (!isFocused_ || !showCaret_) return;
    
    // Animated blinking caret
    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime);
    
    // Blink every 500ms
    bool shouldShow = (elapsed.count() % 1000) < 500;
    if (!shouldShow) return;
    
    NUIRect bounds = getBounds();
    float caretX = bounds.x + 10 + caretPosition_ * 8; // Default positioning
    float caretY = bounds.y + (bounds.height - 14) * 0.5f; // Default font size
    
    // Enhanced caret with glow
    NUIRect glowRect(caretX - 1, caretY - 1, 3, 16);
    renderer.fillRoundedRect(glowRect, 1.0f, NUIColor::fromHex(0x0078d4).withAlpha(0.3f));
    
    // Main caret
    NUIRect caretRect(caretX, caretY, 2, 14);
    renderer.fillRoundedRect(caretRect, 1.0f, NUIColor::fromHex(0x0078d4));
}

} // namespace NomadUI
