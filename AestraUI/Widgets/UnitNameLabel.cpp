// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UnitNameLabel.h"
#include "NUIThemeSystem.h"
#include "NUIRenderer.h"
#include "AestraLog.h"
#include <algorithm>
#include <chrono>

namespace AestraUI {

namespace {
std::string fitText(NUIRenderer& renderer, const std::string& text, float fontSize, float maxWidth) {
    if (text.empty() || renderer.measureText(text, fontSize).width <= maxWidth) {
        return text;
    }

    constexpr const char* ellipsis = "...";
    std::string fitted = text;
    while (!fitted.empty() && renderer.measureText(fitted + ellipsis, fontSize).width > maxWidth) {
        fitted.pop_back();
    }
    return fitted.empty() ? ellipsis : fitted + ellipsis;
}
} // namespace

UnitNameLabel::UnitNameLabel(const std::string& name, Aestra::Audio::UnitType type)
    : m_unitName(name), m_unitType(type) {
}

void UnitNameLabel::setUnitName(const std::string& name) {
    if (m_unitName == name) {
        return;
    }
    m_unitName = name;
    repaint();
}

void UnitNameLabel::setUnitType(Aestra::Audio::UnitType type) {
    if (m_unitType == type) {
        return;
    }
    m_unitType = type;
    repaint();
}

void UnitNameLabel::onRender(NUIRenderer& renderer) {
    if (m_isRenaming && m_textInput) {
        renderChildren(renderer);
        return;
    }

    auto& theme = NUIThemeManager::getInstance();
    auto bounds = getBounds();

    const std::string displayName = fitText(renderer, m_unitName.empty() ? "Unit" : m_unitName,
                                            12.0f, std::max(0.0f, bounds.width - 20.0f));

    renderer.setClipRect(bounds);

    renderer.drawText(displayName,
                      NUIPoint(bounds.x + 10.0f, bounds.y + 3.0f),
                      12.0f,
                      theme.getColor("textPrimary").withAlpha(0.92f));

    if (m_compact) {
        // Compact representation: name only. The type line returns at Full
        // density (responsive contract: available width -> density ->
        // representation).
        renderer.clearClipRect();
        return;
    }

    std::string typeLabel;
    switch (m_unitType) {
    case Aestra::Audio::UnitType::Sampler:
        typeLabel = "Sampler";
        break;
    case Aestra::Audio::UnitType::PitchedSampler:
        typeLabel = "808";
        break;
    case Aestra::Audio::UnitType::Instrument:
        typeLabel = "MIDI";
        break;
    case Aestra::Audio::UnitType::Audio:
        typeLabel = "Audio";
        break;
    default:
        typeLabel = "Sampler";
        break;
    }

    renderer.drawText(typeLabel,
                      NUIPoint(bounds.x + 10.0f, bounds.y + 19.0f),
                      9.0f,
                      theme.getColor("textSecondary").withAlpha(0.5f));
    renderer.clearClipRect();
}

bool UnitNameLabel::onMouseEvent(const NUIMouseEvent& event) {
    if (m_isRenaming && m_textInput) {
        if (m_textInput->onMouseEvent(event))
            return true;
    }

    if (!containsPoint(event.position))
        return false;

    if (event.pressed && event.button == NUIMouseButton::Left) {
        auto now = std::chrono::steady_clock::now();
        long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        bool isDoubleClick = (nowMs - m_lastClickTimeMs < 400) || event.doubleClick;
        m_lastClickTimeMs = nowMs;

        if (isDoubleClick) {
            if (m_onOpenEditor)
                m_onOpenEditor();
            return true;
        }
    }

    // Right-click falls through to UnitRow, whose row context menu already
    // offers Rename (a label-local menu shown here was positioned in
    // row-local coordinates and never appeared on screen).
    return false;
}

bool UnitNameLabel::onKeyEvent(const NUIKeyEvent& event) {
    if (m_isRenaming && m_textInput)
        return m_textInput->onKeyEvent(event);
    return false;
}

void UnitNameLabel::beginRename() {
    if (m_isRenaming && m_textInput)
        return;

    m_isRenaming = true;

    m_textInput = std::make_shared<NUITextInput>(m_unitName);
    m_textInput->setBounds(getBounds());
    m_textInput->setTextColor(NUIThemeManager::getInstance().getColor("textPrimary"));
    m_textInput->setBackgroundColor(NUIThemeManager::getInstance().getColor("inputBgDefault"));
    m_textInput->setBorderColor(NUIThemeManager::getInstance().getColor("inputBorderFocus"));
    m_textInput->setBorderWidth(1.0f);
    m_textInput->setBorderRadius(3.0f);
    m_textInput->setFocusedBorderColor(NUIThemeManager::getInstance().getColor("accentPrimary"));

    m_textInput->setOnReturnKey([this]() { commitRename(); });
    m_textInput->setOnEscapeKey([this]() { cancelRename(); });
    m_textInput->setOnFocusLost([this]() { commitRename(); });

    addChild(m_textInput);
    m_textInput->setFocused(true);
    m_textInput->setCaretPosition(static_cast<int>(m_unitName.length()));
    m_textInput->selectAll();
}

void UnitNameLabel::commitRename() {
    if (!m_isRenaming)
        return;

    std::string newName = m_textInput ? m_textInput->getText() : m_unitName;
    m_isRenaming = false;

    if (m_textInput) {
        removeChild(m_textInput);
        m_textInput.reset();
    }

    if (!newName.empty() && newName != m_unitName) {
        m_unitName = newName;
        if (m_onRename)
            m_onRename(newName);
    }

    repaint();
}

void UnitNameLabel::cancelRename() {
    if (!m_isRenaming)
        return;

    m_isRenaming = false;
    if (m_textInput) {
        removeChild(m_textInput);
        m_textInput.reset();
    }

    repaint();
}

} // namespace AestraUI
