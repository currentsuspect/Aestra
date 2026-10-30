// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "SettingsForm.h"

#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"

#include <algorithm>
#include <cmath>

namespace Aestra {

namespace {
constexpr float kCaptionTop = 14.0f; // space above a caption
constexpr float kCaptionHeight = 22.0f;
constexpr float kHintGap = 12.0f;
constexpr float kBottomPadding = 16.0f;
} // namespace

void SettingsForm::clear() {
    m_items.clear();
}

void SettingsForm::section(std::string caption) {
    Item item;
    item.kind = Kind::Section;
    item.text = std::move(caption);
    item.height = (m_items.empty() ? 0.0f : kCaptionTop) + kCaptionHeight;
    m_items.push_back(std::move(item));
}

void SettingsForm::row(std::string label, std::shared_ptr<AestraUI::NUIComponent> control, std::string hint,
                       float controlWidth) {
    Item item;
    item.kind = Kind::Row;
    item.text = std::move(label);
    item.hint = std::move(hint);
    item.control = std::move(control);
    item.controlWidth = controlWidth;
    m_items.push_back(std::move(item));
}

size_t SettingsForm::block(std::shared_ptr<AestraUI::NUIComponent> component, float height) {
    Item item;
    item.kind = Kind::Block;
    item.control = std::move(component);
    item.height = height;
    m_items.push_back(std::move(item));
    return m_items.size() - 1;
}

void SettingsForm::setHint(const AestraUI::NUIComponent* control, std::string hint) {
    for (auto& item : m_items) {
        if (item.control.get() == control) {
            item.hint = std::move(hint);
            return;
        }
    }
}

float SettingsForm::contentHeight() const {
    float h = 0.0f;
    for (const auto& item : m_items) {
        h += item.height;
    }
    return h + kBottomPadding;
}

void SettingsForm::layout(const AestraUI::NUIRect& area) {
    m_area = area;
    const float maxScroll = std::max(0.0f, contentHeight() - area.height);
    m_scroll = std::clamp(m_scroll, 0.0f, maxScroll);

    const float x = area.x + kSidePadding;
    const float width = std::max(0.0f, area.width - kSidePadding * 2.0f);
    float y = area.y + 6.0f - m_scroll;
    for (auto& item : m_items) {
        item.bounds = AestraUI::NUIRect(x, y, width, item.height);
        if (item.control) {
            if (item.kind == Kind::Block) {
                item.controlBounds = item.bounds;
            } else {
                const float cw = std::min(item.controlWidth, std::max(0.0f, width - kLabelWidth));
                item.controlBounds =
                    AestraUI::NUIRect(x + kLabelWidth, y + (item.height - kControlHeight) * 0.5f, cw, kControlHeight);
            }
            item.control->setBounds(item.controlBounds);
            // A control scrolled out of the area must not take clicks; partly visible ones are clipped when drawn.
            const bool inside = item.controlBounds.bottom() > area.y && item.controlBounds.y < area.bottom();
            item.control->setVisible(inside);
        }
        y += item.height;
    }
}

void SettingsForm::paint(AestraUI::NUIRenderer& renderer) const {
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    const auto& props = theme.getCurrentTheme();
    const AestraUI::NUIColor caption = theme.getColor("textMuted");
    const AestraUI::NUIColor label = theme.getColor("textPrimary").withAlpha(0.88f);
    const AestraUI::NUIColor hint = theme.getColor("textMuted");
    const AestraUI::NUIColor line = theme.getColor("border").withAlpha(0.55f);
    const float captionFont = props.fontSizeXS;
    const float labelFont = 13.0f;
    const float hintFont = 11.5f;

    renderer.setClipRect(m_area);
    for (const auto& item : m_items) {
        const auto& b = item.bounds;
        if (b.bottom() < m_area.y || b.y > m_area.bottom()) {
            continue;
        }
        if (item.kind == Kind::Section) {
            AestraUI::NUIRect text(b.x, b.bottom() - kCaptionHeight, b.width, kCaptionHeight - 6.0f);
            renderer.drawText(item.text, {b.x, std::round(renderer.calculateTextY(text, captionFont))}, captionFont,
                              caption);
            continue;
        }
        // The board's hairline sits on top of every row.
        renderer.drawLine({b.x, b.y + 0.5f}, {b.right(), b.y + 0.5f}, 1.0f, line);
        if (item.kind == Kind::Block) {
            continue;
        }
        renderer.drawText(item.text, {b.x, std::round(renderer.calculateTextY(b, labelFont))}, labelFont, label);
        if (!item.hint.empty()) {
            const float hx = (item.control ? item.controlBounds.right() : b.x + kLabelWidth) + kHintGap;
            renderer.drawText(item.hint, {hx, std::round(renderer.calculateTextY(b, hintFont))}, hintFont, hint);
        }
    }
    renderer.clearClipRect();
}

bool SettingsForm::onMouseWheel(const AestraUI::NUIMouseEvent& event) {
    if (event.wheelDelta == 0.0f || !m_area.contains(event.position)) {
        return false;
    }
    const float maxScroll = std::max(0.0f, contentHeight() - m_area.height);
    if (maxScroll <= 0.0f) {
        return false;
    }
    const float before = m_scroll;
    m_scroll = std::clamp(m_scroll - event.wheelDelta * kRowHeight, 0.0f, maxScroll);
    if (m_scroll == before) {
        return false;
    }
    layout(m_area);
    return true;
}

} // namespace Aestra
