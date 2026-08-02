// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUITextInput.h"
#include "NUIThemeSystem.h"
#include "NUIRenderer.h"
#include "../../AestraPlat/include/AestraPlatform.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cctype>

namespace AestraUI {

NUITextInput::NUITextInput(const std::string& text)
    : NUIComponent()
    , text_(text)
    , blinkStartTime_(std::chrono::steady_clock::now())
{
    auto& theme = NUIThemeManager::getInstance();
    const auto& props = theme.getCurrentTheme();
    textColor_ = props.textPrimary;
    backgroundColor_ = props.inputBgDefault;
    borderColor_ = props.borderSubtle;
    focusedBorderColor_ = props.focusRing;
    placeholderColor_ = props.textMuted;
    selectionColor_ = props.selected;
    caretColor_ = props.textPrimary;
    borderRadius_ = props.radiusM;
    padding_ = props.spacingS;
    setSize(200, props.layout.standardControlHeight);
    invalidateLayout();
}

void NUITextInput::onRender(NUIRenderer& renderer)
{
    if (!isVisible()) return;

    // Calculate layout if needed (requires renderer)
    if (needsStructuralLayoutUpdate_) {
        updateTextLayout();
    }
    
    // Complete layout measurements when renderer is available
    if (needsMeasurementUpdate_) {
        auto& themeManager = NUIThemeManager::getInstance();
        float fontSize = themeManager.getFontSize("m");
        
        totalTextHeight_ = 0.0f;
        
        for (auto& line : layoutLines_)
        {
            // Calculate line height
            if (line.endIndex > line.startIndex)
            {
                std::string lineText = text_.substr(line.startIndex, line.endIndex - line.startIndex);
                if (inputType_ == InputType::Password) {
                    lineText = std::string(line.endIndex - line.startIndex, passwordCharacter_);
                }
                auto size = renderer.measureText(lineText, fontSize);
                line.height = size.height;
            }
            else
            {
                // Empty line
                auto metrics = renderer.getFontMetrics(fontSize);
                line.height = metrics.lineHeight;
            }
            
            line.y = totalTextHeight_;
            
            // Build per-line character X positions (O(n) per line, not O(n²))
            line.charX.clear();
            line.charX.push_back(0.0f);  // INVARIANT: always at least position 0.0f
            
            if (line.endIndex > line.startIndex)
            {
                std::string lineText = text_.substr(line.startIndex, line.endIndex - line.startIndex);
                if (inputType_ == InputType::Password) {
                    lineText = std::string(line.endIndex - line.startIndex, passwordCharacter_);
                }
                
                float cumulativeX = 0.0f;
                
                // Measure each character individually to avoid O(n²) substring remeasurement
                for (int j = line.startIndex; j < line.endIndex; ++j)
                {
                    std::string singleChar = lineText.substr(j - line.startIndex, 1);
                    auto size = renderer.measureText(singleChar, fontSize);
                    cumulativeX += size.width;
                    line.charX.push_back(cumulativeX);
                }
            }
            
            totalTextHeight_ += line.height;
        }
        
        needsMeasurementUpdate_ = false;
    }

    // Enhanced background with inner shadows and focus glow
    if (backgroundVisible_) {
        drawEnhancedBackground(renderer);
    }
    
    if (hasSelection_)
    {
        drawSelection(renderer);
    }
    
    // Placeholder should disappear on interaction (click/focus), not only after typing.
    bool showPlaceholder = text_.empty() && !placeholderText_.empty()
                           && (!isFocused() || showPlaceholderWhenFocused_)
                           && !isPressed_;
    if (showPlaceholder)
    {
        drawPlaceholder(renderer);
    }
    else
    {
        drawText(renderer);
    }
    
    if (isFocused() && showCaret_)
    {
        drawAnimatedCaret(renderer);
    }
}

void NUITextInput::onThemeChanged(const NUIThemeProperties& theme)
{
    if (!customColors_) {
        textColor_ = theme.textPrimary;
        backgroundColor_ = theme.inputBgDefault;
        borderColor_ = theme.borderSubtle;
        focusedBorderColor_ = theme.focusRing;
        placeholderColor_ = theme.textMuted;
        selectionColor_ = theme.selected;
        caretColor_ = theme.textPrimary;
        borderRadius_ = theme.radiusM;
        padding_ = theme.spacingS;
    }
    invalidateLayout();
    NUIComponent::onThemeChanged(theme);
}

bool NUITextInput::onMouseEvent(const NUIMouseEvent& event)
{
    if (!isVisible() || !isEnabled()) return false;

    NUIRect bounds = getBounds();
    if (!bounds.contains(event.position)) return false;

    if (event.pressed && event.button == NUIMouseButton::Left)
    {
        isPressed_ = true;
        
        // Set caret position based on mouse click
        int newCaretPos = getCharacterIndexAtPosition(event.position);
        setCaretPosition(newCaretPos);
        
        // Request focus on click
        if (!isFocused()) {
            setFocused(true);
        }
        
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
    if (!isFocused() || !isVisible()) return false;

    if (event.pressed)
    {
        handleKeyInput(event);
        return true;
    }

    return false;
}

void NUITextInput::onFocusGained()
{
    NUIComponent::onFocusGained();
    showCaret_ = true;
    blinkStartTime_ = std::chrono::steady_clock::now();

    if (onFocusGainedCallback_)
    {
        onFocusGainedCallback_();
    }

    setDirty(true);
}

void NUITextInput::onFocusLost()
{
    NUIComponent::onFocusLost();
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
        invalidateLayout();
        setCaretPosition(static_cast<int>(text_.length()));
        clearSelection();
        triggerTextChange();
        setDirty(true);
    }
}

void NUITextInput::setPlaceholderText(const std::string& placeholder)
{
    placeholderText_ = placeholder;
    invalidateLayout();
    setDirty(true);
}

void NUITextInput::setShowPlaceholderWhenFocused(bool show)
{
    showPlaceholderWhenFocused_ = show;
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
    invalidateLayout();
    setDirty(true);
}

void NUITextInput::setMultiline(bool multiline)
{
    multiline_ = multiline;
    invalidateLayout();
    setDirty(true);
}

void NUITextInput::setWordWrap(bool wordWrap)
{
    wordWrap_ = wordWrap;
    invalidateLayout();
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
    blinkStartTime_ = std::chrono::steady_clock::now();
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
        invalidateLayout();
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
    customColors_ = true;
    setDirty(true);
}

void NUITextInput::setBackgroundColor(const NUIColor& color)
{
    backgroundColor_ = color;
    customColors_ = true;
    setDirty(true);
}

void NUITextInput::setBackgroundVisible(bool visible)
{
    backgroundVisible_ = visible;
    setDirty(true);
}

void NUITextInput::setBorderColor(const NUIColor& color)
{
    borderColor_ = color;
    customColors_ = true;
    setDirty(true);
}

void NUITextInput::setFocusedBorderColor(const NUIColor& color)
{
    focusedBorderColor_ = color;
    customColors_ = true;
    setDirty(true);
}

void NUITextInput::setPlaceholderColor(const NUIColor& color)
{
    placeholderColor_ = color;
    customColors_ = true;
    setDirty(true);
}

void NUITextInput::setSelectionColor(const NUIColor& color)
{
    selectionColor_ = color;
    customColors_ = true;
    setDirty(true);
}

void NUITextInput::setCaretColor(const NUIColor& color)
{
    caretColor_ = color;
    customColors_ = true;
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
    invalidateLayout();
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
    
    invalidateLayout();
    triggerTextChange();
    setDirty(true);
}

void NUITextInput::deleteSelectedText()
{
    if (!hasSelection_) return;
    
    text_.erase(selectionStart_, selectionEnd_ - selectionStart_);
    setCaretPosition(selectionStart_);
    clearSelection();
    invalidateLayout();
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
        invalidateLayout();
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
    NUIColor borderColor = isFocused() ? focusedBorderColor_ : borderColor_;
    renderer.strokeRoundedRect(bounds, borderRadius_, borderWidth_, borderColor);
}

void NUITextInput::drawText(NUIRenderer& renderer)
{
    if (text_.empty()) return;

    NUIRect bounds = getBounds();
    auto& themeManager = NUIThemeManager::getInstance();
    float fontSize = themeManager.getFontSize("m");

    if (!multiline_ || layoutLines_.size() <= 1)
    {
        // Single-line fast path: use the same widget-space Y mapping as caret/hit-testing.
        NUIRect textRect(bounds.x + padding_, bounds.y,
                         bounds.width - padding_ * 2, bounds.height);

        std::string displayText = text_;
        if (inputType_ == InputType::Password)
        {
            displayText = std::string(text_.length(), passwordCharacter_);
        }

        float textY = std::round(renderer.calculateTextY(textRect, fontSize));

        // Calculate text X position based on justification
        float textX = std::round(textRect.x);
        if (justification_ == Justification::Center)
        {
            auto textSize = renderer.measureText(displayText, fontSize);
            textX = std::round(textRect.x + (textRect.width - textSize.width) / 2.0f);
        }
        else if (justification_ == Justification::Right)
        {
            auto textSize = renderer.measureText(displayText, fontSize);
            textX = std::round(textRect.right() - textSize.width);
        }

        const NUIColor color = isEnabled() ? textColor_ : NUIThemeManager::getInstance().getColor("textDisabled");
        renderer.drawText(displayText, NUIPoint(textX, textY), fontSize, color);
    }
    else
    {
        // Multiline path: render each logical line at its local Y offset
        for (const auto& line : layoutLines_)
        {
            if (line.startIndex >= line.endIndex)
                continue; // Empty lines occupy space but render nothing

            std::string lineText = text_.substr(line.startIndex, line.endIndex - line.startIndex);
            if (inputType_ == InputType::Password)
            {
                lineText = std::string(line.endIndex - line.startIndex, passwordCharacter_);
            }

            float textX = std::round(bounds.x + padding_);
            float textY = std::round(getLineRenderY(line));

            const NUIColor color = isEnabled() ? textColor_ : NUIThemeManager::getInstance().getColor("textDisabled");
            renderer.drawText(lineText, NUIPoint(textX, textY), fontSize, color);
        }
    }
}

void NUITextInput::drawSelection(NUIRenderer& renderer)
{
    if (!hasSelection_ || layoutLines_.empty()) return;
    
    NUIRect bounds = getBounds();
    NUIRect textRect(bounds.x + padding_, bounds.y,
                    bounds.width - padding_ * 2, bounds.height);

    int start = std::max(0, std::min(selectionStart_, selectionEnd_));
    int end = std::max(0, std::max(selectionStart_, selectionEnd_));

    // Single-line vertical metrics must mirror drawText(), which positions the
    // glyphs via renderer.calculateTextY() using the real font line height.
    // getLineRenderY() approximates that centering with lineHeight = fontSize,
    // so on renderers whose lineHeight exceeds fontSize the highlight otherwise
    // lands below the text and covers only its lower half.
    const bool singleLine = (!multiline_ || layoutLines_.size() <= 1);
    const float fontSize = NUIThemeManager::getInstance().getFontSize("m");
    const float singleLineTop = std::round(renderer.calculateTextY(textRect, fontSize));
    float singleLineHeight = renderer.getFontMetrics(fontSize).lineHeight;
    if (singleLineHeight <= 0.0f)
        singleLineHeight = fontSize;

    // Find lines that intersect with selection
    for (const auto& line : layoutLines_)
    {
        // Skip lines that don't intersect with selection
        if (end < line.startIndex || start > line.endIndex)
            continue;
        
        // Calculate intersection for this line
        int lineStart = std::max(start, line.startIndex);
        int lineEnd = std::min(end, line.endIndex);
        
        if (lineStart >= lineEnd)
            continue;
        
        // Get X positions for this line
        int startCol = lineStart - line.startIndex;
        int endCol = lineEnd - line.startIndex;
        
        float startX = (startCol >= 0 && startCol < static_cast<int>(line.charX.size())) 
                      ? line.charX[startCol] : 0.0f;
        float endX = (endCol >= 0 && endCol < static_cast<int>(line.charX.size())) 
                    ? line.charX[endCol] : line.charX.back();
        
        NUIRect selectionRect;
        selectionRect.x = textRect.x + justificationOffsetForLine(line, textRect.width) + startX;
        selectionRect.y = singleLine ? singleLineTop : std::round(getLineRenderY(line));
        selectionRect.width = endX - startX;
        selectionRect.height = singleLine ? singleLineHeight : line.height;
        
        // Draw selection highlight
        renderer.fillRoundedRect(selectionRect, 2.0f, selectionColor_.withAlpha(0.4f));
    }
}

float NUITextInput::justificationOffsetForLine(const TextLine& line, float availableWidth) const
{
    if (justification_ == Justification::Left)
        return 0.0f;

    const float lineWidth = line.charX.empty() ? 0.0f : line.charX.back();
    const float slack = availableWidth - lineWidth;
    if (slack <= 0.0f)
        return 0.0f;

    return (justification_ == Justification::Center) ? slack * 0.5f : slack;
}

void NUITextInput::drawCaret(NUIRenderer& renderer)
{
    if (!isFocused() || !showCaret_) return;
}

void NUITextInput::drawPlaceholder(NUIRenderer& renderer)
{
    if (placeholderText_.empty()) return;

    NUIRect bounds = getBounds();
    // Use full height for vertical centering; horizontal padding for icon clearance
    NUIRect textRect(bounds.x + padding_, bounds.y,
                    bounds.width - padding_ * 2, bounds.height);

    auto& themeManager = NUIThemeManager::getInstance();
    float fontSize = themeManager.getFontSize("m");
    float textY = std::round(renderer.calculateTextY(textRect, fontSize));

    // Always center placeholder regardless of justification setting
    // This allows placeholder to be centered while typing remains left-aligned
    auto textSize = renderer.measureText(placeholderText_, fontSize);
    float textX = std::round(textRect.x + (textRect.width - textSize.width) / 2.0f);

    // Draw placeholder with reduced opacity if not already handled by color
    const NUIColor color = isEnabled() ? placeholderColor_ : NUIThemeManager::getInstance().getColor("textDisabled");
    renderer.drawText(placeholderText_, NUIPoint(textX, textY), fontSize, color);
}

void NUITextInput::invalidateLayout()
{
    needsStructuralLayoutUpdate_ = true;
    needsMeasurementUpdate_ = true;
}

void NUITextInput::updateTextLayout()
{
    // This method only rebuilds structural layout (line splits)
    // Measurement is deferred to onRender() when renderer is available
    if (!needsStructuralLayoutUpdate_) return;
    
    layoutLines_.clear();
    totalTextHeight_ = 0.0f;
    
    int textLength = static_cast<int>(text_.length());
    int lineStart = 0;
    
    // Split by explicit \n, preserve empty lines
    for (int i = 0; i <= textLength; ++i)
    {
        bool isBreak = (i == textLength) || (text_[i] == '\n');
        
        if (isBreak)
        {
            TextLine line;
            line.startIndex = lineStart;
            line.endIndex = i;  // exclusive, excludes newline itself
            line.y = totalTextHeight_;  // will be calculated in onRender
            
            // Reserve space for charX, will be populated in onRender
            // INVARIANT: Always push at least position 0.0f
            line.charX.push_back(0.0f);
            line.charX.reserve(line.endIndex - line.startIndex + 1);
            
            layoutLines_.push_back(line);
            lineStart = i + 1;  // skip the newline itself
        }
    }
    
    needsStructuralLayoutUpdate_ = false;
    // Note: needsMeasurementUpdate_ remains true for onRender to complete
}

NUIPoint NUITextInput::getTextPosition(int characterIndex) const
{
    if (layoutLines_.empty()) return NUIPoint(0, 0);

    NUIRect bounds = getBounds();
    NUIRect textRect(bounds.x + padding_, bounds.y,
                     bounds.width - padding_ * 2, bounds.height);

    // Find which line contains this character
    for (const auto& line : layoutLines_)
    {
        if (characterIndex >= line.startIndex && characterIndex < line.endIndex)
        {
            float x = 0.0f;
            int column = characterIndex - line.startIndex;

            if (column >= 0 && column < static_cast<int>(line.charX.size()))
            {
                x = line.charX[column];
            }

            // Apply justification offset for single-line
            float baseX = bounds.x + padding_;
            if (!multiline_ || layoutLines_.size() <= 1)
            {
                if (justification_ == Justification::Center)
                {
                    // Get total text width for this line
                    float totalWidth = line.charX.empty() ? 0.0f : line.charX.back();
                    baseX = std::round(textRect.x + (textRect.width - totalWidth) / 2.0f);
                }
                else if (justification_ == Justification::Right)
                {
                    float totalWidth = line.charX.empty() ? 0.0f : line.charX.back();
                    baseX = std::round(textRect.right() - totalWidth);
                }
            }

            return NUIPoint(baseX + x, getLineRenderY(line));
        }
    }

    // EOF or line end position
    if (!layoutLines_.empty())
    {
        const auto& lastLine = layoutLines_.back();
        // Check if caret is at the very end of the last line
        if (characterIndex == lastLine.endIndex)
        {
            float x = lastLine.charX.empty() ? 0.0f : lastLine.charX.back();

            // Apply justification offset for single-line
            float baseX = bounds.x + padding_;
            if (!multiline_ || layoutLines_.size() <= 1)
            {
                if (justification_ == Justification::Center)
                {
                    float totalWidth = lastLine.charX.empty() ? 0.0f : lastLine.charX.back();
                    baseX = std::round(textRect.x + (textRect.width - totalWidth) / 2.0f);
                }
                else if (justification_ == Justification::Right)
                {
                    float totalWidth = lastLine.charX.empty() ? 0.0f : lastLine.charX.back();
                    baseX = std::round(textRect.right() - totalWidth);
                }
            }

            return NUIPoint(baseX + x, getLineRenderY(lastLine));
        }
    }

    return NUIPoint(0, 0);
}

int NUITextInput::getCharacterIndexAtPosition(const NUIPoint& position) const
{
    if (layoutLines_.empty()) return 0;

    NUIRect bounds = getBounds();
    NUIRect textRect(bounds.x + padding_, bounds.y,
                     bounds.width - padding_ * 2, bounds.height);

    float relativeX = position.x - (bounds.x + padding_);
    float relativeY = position.y - (bounds.y + padding_);

    // Keep single-line hit testing aligned with the same widget-space mapping used
    // by getTextPosition()/drawText().
    if (!multiline_ || layoutLines_.size() <= 1) {
        if (!layoutLines_.empty()) {
            relativeY = position.y - getLineRenderY(layoutLines_[0]);
        }
    }

    // Adjust relativeX for justification (single-line only)
    if (!multiline_ || layoutLines_.size() <= 1)
    {
        if (justification_ == Justification::Center)
        {
            // Calculate text offset
            float totalWidth = 0.0f;
            if (!layoutLines_.empty())
            {
                totalWidth = layoutLines_[0].charX.empty() ? 0.0f : layoutLines_[0].charX.back();
            }
            float textOffset = (textRect.width - totalWidth) / 2.0f;
            relativeX -= textOffset;
        }
        else if (justification_ == Justification::Right)
        {
            float totalWidth = 0.0f;
            if (!layoutLines_.empty())
            {
                totalWidth = layoutLines_[0].charX.empty() ? 0.0f : layoutLines_[0].charX.back();
            }
            float textOffset = textRect.width - totalWidth;
            relativeX -= textOffset;
        }
    }

    // Find which line we're on vertically
    for (const auto& line : layoutLines_)
    {
        if (relativeY >= line.y && relativeY < line.y + line.height)
        {
            // Horizontal clamping for correctness and performance
            if (relativeX <= 0.0f)
                return line.startIndex;

            if (line.charX.size() > 1 && relativeX >= line.charX.back())
                return line.endIndex;

            // Find closest character index in this line
            int bestIndex = line.startIndex;
            float minDiff = std::abs(relativeX - line.charX[0]);

            for (size_t i = 1; i < line.charX.size(); ++i)
            {
                float diff = std::abs(relativeX - line.charX[i]);
                if (diff < minDiff)
                {
                    minDiff = diff;
                    bestIndex = line.startIndex + static_cast<int>(i);
                }
            }

            return bestIndex;
        }
    }

    // Above the first line -> beginning of text
    if (relativeY < 0.0f && !layoutLines_.empty())
        return layoutLines_.front().startIndex;

    // Below the last line -> EOF
    return static_cast<int>(text_.length());
}

bool NUITextInput::isValidCharacter(char character) const
{
    const unsigned char uc = static_cast<unsigned char>(character);
    switch (inputType_)
    {
        case InputType::Number:
            return std::isdigit(uc) || character == '.' || character == '-';
        case InputType::Email:
            return std::isalnum(uc) || character == '@' || character == '.' || character == '_' || character == '-';
        case InputType::URL:
            return std::isalnum(uc) || character == '.' || character == '/' || character == ':' || character == '?' || character == '&' || character == '=';
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
    setDirty(true);
}

void NUITextInput::updateSelection()
{
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
                break;
            }
            [[fallthrough]];
            
        case NUIKeyCode::C:
            if (event.modifiers & NUIModifiers::Ctrl)
            {
                if (auto* utils = Aestra::Platform::getUtils()) {
                    utils->setClipboardText(getSelectedText());
                }
                break;
            }
            [[fallthrough]];
            
        case NUIKeyCode::V:
            if (event.modifiers & NUIModifiers::Ctrl)
            {
                if (!readOnly_) {
                    if (auto* utils = Aestra::Platform::getUtils()) {
                        insertText(utils->getClipboardText());
                    }
                }
                break;
            }
            [[fallthrough]];
            
        case NUIKeyCode::X:
            if (event.modifiers & NUIModifiers::Ctrl)
            {
                if (!readOnly_) {
                    if (auto* utils = Aestra::Platform::getUtils()) {
                        utils->setClipboardText(getSelectedText());
                    }
                    deleteSelectedText();
                }
                break;
            }
            [[fallthrough]];
            
        case NUIKeyCode::Z:
            if (event.modifiers & NUIModifiers::Ctrl)
            {
                // Don't consume Ctrl+Z/Ctrl+Shift+Z — let it bubble up to global undo/redo handler
                return;
            }
            [[fallthrough]];
            
        case NUIKeyCode::Y:
            if (event.modifiers & NUIModifiers::Ctrl)
            {
                // Don't consume Ctrl+Y — let it bubble up to global redo handler
                return;
            }
            [[fallthrough]];
            
        default:
            // Only handle character input if it wasn't a special key we handled above
            // AND it's a valid printable character
            auto c = static_cast<unsigned char>(event.character);

            if (c >= 32 && c != 127) // 32=Space, 127=DEL
            {
                insertCharacter(c);
            }
            break;
    }
}

void NUITextInput::moveCaret(int direction, bool extendSelection)
{
    int newPos = caretPosition_ + direction;
    newPos = std::clamp(newPos, 0, static_cast<int>(text_.length()));
    
    // Update preferred column during horizontal movement
    if (layoutLines_.empty())
    {
        preferredColumn_ = newPos;
    }
    else
    {
        int currentLine = findLineForCaret(newPos);
        preferredColumn_ = getColumnInLine(newPos, currentLine);
    }
    
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

void NUITextInput::moveCaretToLine(int lineDelta, bool extendSelection)
{
    if (!multiline_ || layoutLines_.empty()) return;
    
    int currentLine = findLineForCaret(caretPosition_);
    int currentColumn = getColumnInLine(caretPosition_, currentLine);
    
    int targetLine = std::clamp(currentLine + lineDelta, 0, static_cast<int>(layoutLines_.size()) - 1);
    
    // Clamp column to target line length
    const auto& targetLineLayout = layoutLines_[targetLine];
    int targetLineLength = targetLineLayout.endIndex - targetLineLayout.startIndex;
    int targetColumn = std::min(preferredColumn_, targetLineLength);
    
    int newCaretPos = targetLineLayout.startIndex + targetColumn;
    newCaretPos = std::clamp(newCaretPos, 0, static_cast<int>(text_.length()));
    
    if (extendSelection)
    {
        if (selectionStart_ == selectionEnd_)
        {
            selectionStart_ = caretPosition_;
        }
        selectionEnd_ = newCaretPos;
        hasSelection_ = true;
    }
    else
    {
        clearSelection();
    }
    
    setCaretPosition(newCaretPos);
}

void NUITextInput::moveCaretToWord(int direction, bool extendSelection)
{
    int textLength = static_cast<int>(text_.length());
    int pos = caretPosition_;
    
    if (direction > 0)  // Ctrl+Right: move to next word start
    {
        // Skip current word chars
        while (pos < textLength && isWordChar(text_[pos]))
        {
            ++pos;
        }
        
        // Skip separators
        while (pos < textLength && !isWordChar(text_[pos]))
        {
            ++pos;
        }
    }
    else  // Ctrl+Left: move to previous word start
    {
        // Move backward over separators
        while (pos > 0 && !isWordChar(text_[pos - 1]))
        {
            --pos;
        }
        
        // Move backward over word chars
        while (pos > 0 && isWordChar(text_[pos - 1]))
        {
            --pos;
        }
    }
    
    pos = std::clamp(pos, 0, textLength);
    
    if (extendSelection)
    {
        if (selectionStart_ == selectionEnd_)
        {
            selectionStart_ = caretPosition_;
        }
        selectionEnd_ = pos;
        hasSelection_ = true;
    }
    else
    {
        clearSelection();
    }
    
    setCaretPosition(pos);
}

bool NUITextInput::isWordChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

int NUITextInput::findLineForCaret(int caretPos) const
{
    if (layoutLines_.empty()) return 0;
    
    for (size_t i = 0; i < layoutLines_.size(); ++i)
    {
        const auto& line = layoutLines_[i];
        
        // Use < for endIndex to avoid ambiguity at line boundaries
        // Special case: caret at line end (endIndex) is considered part of that line
        if (caretPos >= line.startIndex && caretPos <= line.endIndex)
        {
            return static_cast<int>(i);
        }
    }
    
    // EOF — return last line
    return static_cast<int>(layoutLines_.size()) - 1;
}

int NUITextInput::getColumnInLine(int caretPos, int lineIndex) const
{
    if (lineIndex < 0 || lineIndex >= static_cast<int>(layoutLines_.size()))
        return 0;

    const auto& line = layoutLines_[lineIndex];
    return caretPos - line.startIndex;
}

float NUITextInput::getLineRenderY(const TextLine& line) const
{
    NUIRect bounds = getBounds();
    NUIRect textRect(bounds.x + padding_, bounds.y,
                     bounds.width - padding_ * 2, bounds.height);

    // For single-line, vertically center within textRect (same as drawText)
    if (!multiline_ || layoutLines_.size() <= 1)
    {
        auto& themeManager = NUIThemeManager::getInstance();
        float fontSize = themeManager.getFontSize("m");
        
        // Inline calculateTextY logic since we don't have renderer access here
        // Standard fallback metrics: lineHeight = fontSize
        float lineHeight = fontSize;
        return std::round(textRect.y + (textRect.height - lineHeight) * 0.5f);
    }

    // For multiline, use the accumulated line offset
    return bounds.y + padding_ + line.y;
}

NUIPoint NUITextInput::getCaretRenderPosition() const
{
    return getTextPosition(caretPosition_);
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
    
    invalidateLayout();
    triggerTextChange();
    setDirty(true);
}

void NUITextInput::insertCharacter(char character)
{
    if (readOnly_) return;

    // Typing replaces the selection, exactly as insertText() already did.
    // Without this the per-character path inserted at the caret and left the
    // selected text in place: a select-all field seeded with "6.0" turned into
    // "06.0" when the user typed "0" over it, because the caret sits at 0.
    if (hasSelection_)
    {
        deleteSelectedText();
    }

    if (maxLength_ > 0 && static_cast<int>(text_.length()) >= maxLength_)
        return;

    text_.insert(caretPosition_, 1, character);
    setCaretPosition(caretPosition_ + 1);
    
    invalidateLayout();
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
    auto& theme = NUIThemeManager::getInstance();
    renderer.fillRoundedRect(bounds, borderRadius_,
                             isEnabled() ? backgroundColor_ : backgroundColor_.withAlpha(0.55f));
    
    // Border with validation highlighting
    NUIColor borderColor = borderColor_;
    float borderWidth = borderWidth_; // Use component's border width, not hardcoded
    
    if (isFocused())
    {
        borderColor = focusedBorderColor_;
        borderWidth = std::max(borderWidth, 1.5f);
    }
    else if (hasValidationError_)
    {
        borderColor = theme.getColor("error");
    }
    else if (hasValidationSuccess_)
    {
        borderColor = theme.getColor("success");
    }
    else if (!isEnabled())
    {
        borderColor = theme.getColor("borderSubtle").withAlpha(0.45f);
    }
    
    renderer.strokeRoundedRect(bounds, borderRadius_, borderWidth, borderColor);
}

void NUITextInput::drawAnimatedCaret(NUIRenderer& renderer)
{
    if (!isFocused() || !showCaret_) return;

    // Per-instance blinking caret using steady_clock for monotonic timing
    auto currentTime = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - blinkStartTime_);

    // Blink every 500ms
    bool shouldShow = (elapsed.count() % 1000) < 500;
    if (!shouldShow) return;

    NUIRect bounds = getBounds();
    NUIRect textRect(bounds.x + padding_, bounds.y,
                     bounds.width - padding_ * 2, bounds.height);

    auto& themeManager = NUIThemeManager::getInstance();
    const float fontSize = themeManager.getFontSize("m");
    const auto metrics = renderer.getFontMetrics(fontSize);
    const float caretHeight = std::max(1.0f, std::round(metrics.lineHeight));

    // Find which line contains the caret
    int currentLine = findLineForCaret(caretPosition_);
    if (currentLine < 0 || currentLine >= static_cast<int>(layoutLines_.size()))
        return;

    const auto& line = layoutLines_[currentLine];

    // Get caret X position for this line
    int column = getColumnInLine(caretPosition_, currentLine);
    const float justifyX = justificationOffsetForLine(line, textRect.width);
    float caretX = std::round(textRect.x + justifyX);

    if (column >= 0 && column < static_cast<int>(line.charX.size()))
    {
        caretX = std::round(textRect.x + justifyX + line.charX[column]);
    }

    // Use calculateTextY for proper baseline alignment in single-line mode
    float caretY;
    if (!multiline_ || layoutLines_.size() <= 1) {
        caretY = std::round(renderer.calculateTextY(textRect, fontSize));
    } else {
        caretY = std::round(getLineRenderY(line));
    }

    // Slim caret with subtle glow
    NUIRect glowRect(caretX - 0.5f, caretY - 1, 2, caretHeight + 2);
    renderer.fillRoundedRect(glowRect, 1.0f, focusedBorderColor_.withAlpha(0.22f));

    // Main caret
    NUIRect caretRect(caretX, caretY, 1.5f, caretHeight);
    renderer.fillRoundedRect(caretRect, 1.0f, focusedBorderColor_);
}

} // namespace AestraUI
