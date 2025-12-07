// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUICoreWidgets.h"

#include "../Graphics/NUIRenderer.h"
#include <algorithm>

namespace NomadUI {

NUIToggle::NUIToggle()
    : state_(State::Off), animated_(true), hovered_(false) {}

void NUIToggle::onRender(NUIRenderer& renderer)
{
    (void)renderer;
    // Rendering handled by theme system when available
}

bool NUIToggle::onMouseEvent(const NUIMouseEvent& event)
{
    if (!isEnabled() || state_ == State::Disabled)
        return false;

    // Check if event is within bounds
    if (!containsPoint(event.position)) {
        return false;
    }

    // Old widget code used event.type/enum; map to current event fields
    if (event.pressed && event.button == NUIMouseButton::Left)
    {
        setOn(!isOn());
        if (onToggle_)
        {
            onToggle_(isOn());
        }
        return true;
    }
    return false;
}

void NUIToggle::onMouseEnter()
{
    hovered_ = true;
    updateVisualState();
}

void NUIToggle::onMouseLeave()
{
    hovered_ = false;
    updateVisualState();
}

void NUIToggle::setState(State state)
{
    if (state_ == state)
        return;
    state_ = state;
    repaint();
}

void NUIToggle::setAnimated(bool animated)
{
    animated_ = animated;
}

void NUIToggle::setOnToggle(std::function<void(bool)> callback)
{
    onToggle_ = std::move(callback);
}

void NUIToggle::setOn(bool enabled)
{
    setState(enabled ? State::On : State::Off);
}

void NUIToggle::updateVisualState()
{
    repaint();
}

NUITextField::NUITextField()
{
    setPlaceholder("");
}

void NUITextField::setPlaceholder(const std::string& text)
{
    placeholder_ = text;
    repaint();
}

void NUITextField::onRender(NUIRenderer& renderer)
{
    NUITextInput::onRender(renderer);
    if (getText().empty() && !placeholder_.empty() && !isFocused())
    {
        (void)renderer;
        // Placeholder text rendering to be implemented with text renderer
    }
}

NUIMeter::NUIMeter()
    : decayRate_(0.75f), holdEnabled_(false)
{
    setChannelCount(2);
}

void NUIMeter::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void NUIMeter::onUpdate(double deltaTime)
{
    if (!holdEnabled_)
    {
        const float decayAmount = static_cast<float>(deltaTime) * decayRate_;
        for (auto& channel : channels_)
        {
            channel.peak = std::max(0.0f, channel.peak - decayAmount);
            channel.rms = std::max(0.0f, channel.rms - decayAmount);
        }
    }
}

void NUIMeter::setChannelCount(size_t count)
{
    channels_.resize(count);
    repaint();
}

void NUIMeter::setLevels(size_t channel, float peak, float rms)
{
    if (channel >= channels_.size())
        return;

    channels_[channel].peak = std::clamp(peak, 0.0f, 1.0f);
    channels_[channel].rms = std::clamp(rms, 0.0f, 1.0f);
    repaint();
}

NUIMeter::ChannelLevel NUIMeter::getLevels(size_t channel) const
{
    if (channel >= channels_.size())
        return {};
    return channels_[channel];
}

void NUIMeter::setDecayRate(float rate)
{
    decayRate_ = std::max(0.0f, rate);
}

void NUIMeter::setHoldEnabled(bool enabled)
{
    holdEnabled_ = enabled;
}

NUIScrollView::NUIScrollView()
    : contentSize_{0.0f, 0.0f}, scrollOffset_{0.0f, 0.0f}, direction_(Direction::Both)
{
}

void NUIScrollView::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

bool NUIScrollView::onMouseEvent(const NUIMouseEvent& event)
{
    // Map legacy scroll fields: use wheelDelta as vertical scroll amount
    if (event.wheelDelta != 0.0f)
    {
        NUIPoint newOffset = scrollOffset_;
        newOffset.x -= 0.0f; // horizontal wheel not available in core event
        newOffset.y -= event.wheelDelta;
        setScrollOffset(newOffset);
        return true;
    }
    return false;
}

void NUIScrollView::setContentSize(const NUISize& size)
{
    contentSize_ = size;
    setScrollOffset(scrollOffset_);
}

void NUIScrollView::setScrollOffset(const NUIPoint& offset)
{
    scrollOffset_ = clampOffset(offset);
    repaint();
}

void NUIScrollView::setDirection(Direction direction)
{
    direction_ = direction;
}

NUIPoint NUIScrollView::clampOffset(const NUIPoint& offset) const
{
    NUIPoint clamped = offset;
    const float maxX = std::max(0.0f, contentSize_.width - getWidth());
    const float maxY = std::max(0.0f, contentSize_.height - getHeight());

    if (direction_ == Direction::Vertical)
    {
        clamped.x = 0.0f;
    }
    else
    {
        clamped.x = std::clamp(clamped.x, 0.0f, maxX);
    }

    if (direction_ == Direction::Horizontal)
    {
        clamped.y = 0.0f;
    }
    else
    {
        clamped.y = std::clamp(clamped.y, 0.0f, maxY);
    }
    return clamped;
}

NUIPanel::NUIPanel()
    : backgroundColor_(NUIColor::fromHex(0x121216ff)),
      borderColor_(NUIColor::fromHex(0x1e1e24ff)),
      variant_(Variant::Plain)
{
}

void NUIPanel::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void NUIPanel::setBackgroundColor(const NUIColor& color)
{
    backgroundColor_ = color;
    repaint();
}

void NUIPanel::setBorderColor(const NUIColor& color)
{
    borderColor_ = color;
    repaint();
}

void NUIPanel::setVariant(Variant variant)
{
    variant_ = variant;
    repaint();
}

NUIPopupMenu::NUIPopupMenu() = default;

void NUIPopupMenu::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

bool NUIPopupMenu::onMouseEvent(const NUIMouseEvent& event)
{
    if (event.pressed && event.button == NUIMouseButton::Left)
    {
        const int index = static_cast<int>((event.position.y - getY()) / 24.0f);
        if (index >= 0 && index < static_cast<int>(items_.size()))
        {
            const auto& item = items_[index];
            if (item.enabled && onSelect_)
            {
                onSelect_(item);
            }
            return true;
        }
    }
    return false;
}

void NUIPopupMenu::setItems(std::vector<NUIPopupMenuItem> items)
{
    items_ = std::move(items);
    repaint();
}

void NUIPopupMenu::setOnSelect(std::function<void(const NUIPopupMenuItem&)> callback)
{
    onSelect_ = std::move(callback);
}

NUITabBar::NUITabBar() = default;

void NUITabBar::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

bool NUITabBar::onMouseEvent(const NUIMouseEvent& event)
{
    if (event.pressed && event.button == NUIMouseButton::Left)
    {
        const int index = hitTestTab(static_cast<int>(event.position.x), static_cast<int>(event.position.y));
        if (index >= 0 && index < static_cast<int>(tabs_.size()))
        {
            setActiveTab(tabs_[index].id);
            return true;
        }
    }
    return false;
}

void NUITabBar::addTab(const Tab& tab)
{
    tabs_.push_back(tab);
    if (activeTabId_.empty())
    {
        activeTabId_ = tab.id;
    }
    repaint();
}

void NUITabBar::removeTab(const std::string& id)
{
    tabs_.erase(std::remove_if(tabs_.begin(), tabs_.end(), [&](const Tab& t) { return t.id == id; }), tabs_.end());
    if (activeTabId_ == id)
    {
        activeTabId_.clear();
        if (!tabs_.empty())
        {
            activeTabId_ = tabs_.front().id;
        }
    }
    repaint();
}

void NUITabBar::clearTabs()
{
    tabs_.clear();
    activeTabId_.clear();
    repaint();
}

void NUITabBar::setActiveTab(const std::string& id)
{
    if (activeTabId_ == id)
        return;

    activeTabId_ = id;
    repaint();
    if (onTabChanged_)
    {
        onTabChanged_(id);
    }
}

void NUITabBar::setOnTabChanged(std::function<void(const std::string&)> callback)
{
    onTabChanged_ = std::move(callback);
}

int NUITabBar::hitTestTab(int x, int y) const
{
    (void)y;
    if (tabs_.empty())
        return -1;
    const float tabWidth = getWidth() / static_cast<float>(tabs_.size());
    const int index = static_cast<int>((x - getX()) / tabWidth);
    if (index < 0 || index >= static_cast<int>(tabs_.size()))
        return -1;
    return index;
}

} // namespace NomadUI

